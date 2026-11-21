#include "graphics.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "program_modes.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
using namespace voyemu;
using namespace voyemu::graphics;
namespace {
using Bytes = std::vector<std::byte>;
using Clock = std::chrono::steady_clock;

void need(bool yes, const char *message) {
    if (!yes) {
        throw std::runtime_error(message);
    }
}

template <class T> Bytes encoded(std::initializer_list<T> values) {
    Bytes bytes(values.size() * sizeof(T));

    std::memcpy(bytes.data(), values.begin(), bytes.size());

    return bytes;
}

template <class T> T value(const Bytes &bytes, std::size_t at = 0) {
    need(at + sizeof(T) <= bytes.size(), "short binary reply");
    T result;
    std::memcpy(&result, bytes.data() + at, sizeof(T));

    return result;
}

template <class T> void patch(Bytes &bytes, std::size_t at, T v) {
    need(at + sizeof(T) <= bytes.size(), "short journal fixture");
    std::memcpy(bytes.data() + at, &v, sizeof(T));
}

Bytes text(std::string_view s) {
    Bytes b(s.size());

    std::memcpy(b.data(), s.data(), s.size());

    return b;
}

void write_at(const std::filesystem::path &p, std::uint64_t at, const Bytes &bytes) {
    std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);

    f.seekp(static_cast<std::streamoff>(at));
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.flush();
    need(bool(f), "fixture write failed");
}

Bytes read_at(const std::filesystem::path &p, std::uint64_t at, std::size_t count) {
    Bytes b(count);
    std::ifstream f(p, std::ios::binary);

    f.seekg(static_cast<std::streamoff>(at));
    f.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(count));
    need(bool(f), "fixture read failed");

    return b;
}

struct Worker {
    std::filesystem::path ram, log;
    ChildProcess child;
    std::uint32_t sequence = 0;

    Worker(const std::filesystem::path &executable, const std::filesystem::path &angle,
           const std::filesystem::path &root, const std::filesystem::path &memory, std::string mode,
           const std::filesystem::path &journal)
        : ram(memory) {
        const auto session = root / (mode + "-" + random_token());
        ensure_private_directory(session);
        log = session / "stderr.log";
        write_at(ram, 4096, Bytes(4096));
        write_at(ram, 4096, encoded<std::uint32_t>({rpc_magic, rpc_version, 0, 0, 0, 0, 0, 0}));
        ProcessSpec spec;
        spec.executable = executable;
        spec.arguments = {
                "--ram",         ram.string(),     "--address", std::to_string(ram_base + 4096),
                "--session",     session.string(), "--angle",   angle.string(),
                "--no-controls", "--seconds",      "25",        "--checkpoint-" + mode,
                journal.string()};
        spec.directory = root;
        spec.output = log;
        spec.environment["TMPDIR"] = root.string();
        program_mode(spec.executable, "_graphics", spec.arguments);
        child = ChildProcess::launch(spec);
    }

    Reply call(std::uint32_t op, const Arguments &args = {}, const Bytes &data = {}) {
        need(data.size() <= rpc_payload_bytes, "oversized fixture payload");
        Bytes packet(4096);
        const std::array<std::uint32_t, 8> h{rpc_magic,  rpc_version,
                                             0,          op,
                                             ++sequence, static_cast<std::uint32_t>(data.size()),
                                             0,          0};
        std::memcpy(packet.data(), h.data(), 32);
        std::memcpy(packet.data() + 32, args.data(), 128);
        std::copy(data.begin(), data.end(), packet.begin() + 256);
        write_at(ram, 4096, packet);
        write_at(ram, 4104, encoded<std::uint32_t>({1}));
        const auto deadline = Clock::now() + std::chrono::seconds(5);

        while (child.alive() && Clock::now() < deadline) {
            packet = read_at(ram, 4096, 4096);

            if (value<std::uint32_t>(packet, 8) == 2) {
                need(value<std::uint32_t>(packet, 16) == sequence, "RPC sequence mismatch");
                const auto count = value<std::uint32_t>(packet, 24);
                need(count <= rpc_payload_bytes && value<std::uint32_t>(packet, 28) == 0,
                     "RPC fault");

                return {value<std::uint64_t>(packet, 160),
                        Bytes(packet.begin() + 256, packet.begin() + 256 + count),
                        {}};
            }

            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        throw std::runtime_error("worker did not acknowledge opcode " + std::to_string(op));
    }

    void finish(bool success = true) {
        if (success) {
            write_at(ram, 4104, encoded<std::uint32_t>({3}));
        }

        const auto deadline = Clock::now() + std::chrono::seconds(5);

        while (child.alive() && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        need(!child.alive(), "worker failed to terminate");
        need((child.exit_code() == 0) == success, "unexpected worker exit");
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        need(argc == 4, "usage: graphics_replay_test WORKER ANGLE DIRECTORY");
        const auto executable = std::filesystem::canonical(argv[1]);
        const std::filesystem::path angle = argv[2];

        if (!std::filesystem::is_directory(angle)) {
            std::cerr << "Pinned ANGLE SDK required; run voyemu deps\n";

            return 77;
        }

        ensure_private_directory(argv[3]);
        const auto root = std::filesystem::canonical(argv[3]) /
                          ("gpu-replay-test-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const auto ram = root / "host-fixture-ram.bin", journals = root / "journals";
        ensure_private_directory(journals);
        {
            std::ofstream f(ram, std::ios::binary);
            need(bool(f), "RAM fixture create failed");
        }
        // Grows sparse on Windows too.
        voyemu::os::resize_file(ram, ram_bytes);
        Worker source(executable, angle, root, ram, "record", journals);
        const auto display = source.call(1).result;
        need(source.call(2, {display}).result, "EGL initialize failed");
        const auto config = value<std::uint64_t>(
                source.call(6, {display, 1},
                            encoded<std::uint32_t>({0x3033, 1, 0x3040, 0x40, 0x3038}))
                        .payload,
                4);
        const auto context = source.call(7, {display, config, 0},
                                         encoded<std::uint32_t>({0x3098, 3, 0x3038}))
                                     .result;
        const auto surface = source.call(8, {display, config},
                                         encoded<std::uint32_t>({0x3057, 16, 0x3056, 16, 0x3038}))
                                     .result;
        need(source.call(9, {display, surface, surface, context}).result, "make current failed");
        need(source.call(70, {0x3059}).result == surface &&
                     source.call(70, {0x305a}).result == surface,
             "record current surfaces differ");
        const auto texture = value<std::uint32_t>(source.call(142, {1, 0}).payload),
                   framebuffer = value<std::uint32_t>(source.call(140, {1, 0}).payload);
        source.call(105, {0xde1, texture});
        source.call(103, {0x8d40, framebuffer});
        source.call(138, {0x8d40, 0x8ce0, 0xde1, texture, 0});
        auto pixels = [&](Worker &w) {
            return w.call(24, {0, 0, 2, 2, 0x1908, 0x1401, 0}).payload;
        };
        const auto texels = encoded<std::uint8_t>({31, 63, 127, 255, 31, 63, 127, 255, 31, 63, 127,
                                                   255, 31, 63, 127, 255}),
                   updated = encoded<std::uint8_t>(
                           {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255});
        auto upload = [&](const Bytes &b) {
            write_at(ram, 32768, Bytes(b.begin(), b.begin() + 8));
            write_at(ram, 40960, Bytes(b.begin() + 8, b.end()));
        };
        const auto blob = source.call(40, {16, 0}).result;
        upload(texels);
        source.call(41, {blob, 2}, encoded<std::uint64_t>({0x40008000, 8, 0x4000a000, 8}));
        source.call(178, {0xde1, 0, 0x8058, 2, 2, 0, 0x1908, 0x1401, blob});
        need(pixels(source) == texels, "initial uploaded pixels differ");
        upload(updated);
        source.call(43, {blob, 16});
        source.call(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, blob});
        need(pixels(source) == updated, "updated uploaded pixels differ");
        source.call(42, {blob});
        auto historical = [&] {
            const auto id = source.call(40, {16, 1}).result;
            source.call(41, {id, 1}, encoded<std::uint64_t>({0x4000d000, 16}));
            source.call(24, {0, 0, 2, 2, 0x1908, 0x1401, id});
            source.call(42, {id});
        };
        historical();
        need(read_at(ram, 53248, 16) == updated, "bulk framebuffer output differs");
        auto bits = [](float f) {
            return std::bit_cast<std::uint32_t>(f);
        };
        source.call(103, {0x8d40, 0});
        source.call(22, {bits(.2f), bits(.3f), bits(.4f), bits(1.f)});
        source.call(23, {0x4000});
        const auto expected_default = pixels(source);
        historical();
        need(read_at(ram, 53248, 16) == expected_default, "default framebuffer output differs");
        source.call(103, {0x8d40, framebuffer});
        const auto output = source.call(40, {4, 1}).result;
        source.call(41, {output, 1}, encoded<std::uint64_t>({0x4000c000, 4}));
        source.call(139, {1, output});
        need(read_at(ram, 49152, 4) != Bytes(4), "object output missing");
        source.call(42, {output});
        need(source.call(20).result == 0, "record GL error");
        source.finish();
        const auto journal = journals / "40001000.gpu";
        need(std::filesystem::is_regular_file(journal), "checkpoint journal missing");
        write_at(ram, 32768, Bytes(8, std::byte{0xaa}));
        write_at(ram, 40960, Bytes(8, std::byte{0xbb}));
        write_at(ram, 49152, text("SAFE"));
        write_at(ram, 53248, text("CHECKPOINT_SAFE!"));
        Worker dest(executable, angle, root, ram, "replay", journals);
        need(dest.call(62).result == context, "context not restored");
        need(dest.call(70, {0x3059}).result == surface && dest.call(70, {0x305a}).result == surface,
             "current surfaces not restored");
        need(read_at(ram, 49152, 4) == text("SAFE") &&
                     read_at(ram, 32768, 8) == Bytes(8, std::byte{0xaa}),
             "replay overwrote historical allocation");
        need(pixels(dest) == updated, "resources not restored after input reuse");
        need(dest.call(9, {display, surface, surface, context}).result,
             "replay make current failed");
        dest.call(103, {0x8d40, 0});
        need(pixels(dest) == expected_default, "default framebuffer not restored");
        dest.call(103, {0x8d40, framebuffer});
        dest.call(22, {bits(0.f), bits(0.f), bits(1.f), bits(1.f)});
        dest.call(23, {0x4000});
        need(pixels(dest) == encoded<std::uint8_t>({0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                                                    0, 0, 255, 255}),
             "live GPU commands after replay failed");
        need(dest.call(20).result == 0, "replay GL error");
        dest.finish();
        need(read_at(ram, 53248, 16) == text("CHECKPOINT_SAFE!"),
             "historical readback overwritten");
        unsigned replayed = 0;
        std::ifstream logs(dest.log);

        for (std::string line; std::getline(logs, line);) {
            auto item = Json::parse(line);

            if (item.value("message", std::string{}) == "checkpoint.replayed") {
                ++replayed;
                const auto &cost = item.at("fields");
                need(cost.at("readbacks_skipped") == 1 && cost.at("readback_bytes_skipped") == 16 &&
                             cost.at("elapsed_ms").get<double>() >= 0 &&
                             cost.at("bytes") == std::filesystem::file_size(journal),
                     "replay accounting differs");
            }
        }

        need(replayed == 1, "replay receipt missing");
        const auto original = read_at(journal, 0, std::filesystem::file_size(journal));
        auto rejects = [&](std::string name, const Bytes &b) {
            const auto directory = root / name;
            ensure_private_directory(directory);
            {
                std::ofstream f(directory / journal.filename(), std::ios::binary);
                f.write(reinterpret_cast<const char *>(b.data()),
                        static_cast<std::streamsize>(b.size()));
                need(bool(f), "corrupt fixture write failed");
            }
            Worker invalid(executable, angle, root, ram, "replay", directory);
            invalid.finish(false);
        };
        rejects("truncated", Bytes(original.begin(), original.end() - 1));
        auto bad = original;
        bool is_default = false, changed = false;

        for (std::size_t at = 24; at < bad.size();) {
            const auto size = value<std::uint32_t>(bad, at), op = value<std::uint32_t>(bad, at + 4);

            if (op == 103) {
                is_default = value<std::uint64_t>(bad, at + 36) == 0;
            }

            if (op == 24 && is_default && value<std::uint64_t>(bad, at + 76)) {
                patch<std::uint64_t>(bad, at + 76, 0xdead);
                changed = true;
                break;
            }

            at += 4 + size;
        }

        need(changed, "historical output fixture missing");
        rejects("bad-output", bad);
        bad = original;
        changed = false;

        for (std::size_t at = 24; at < bad.size();) {
            const auto size = value<std::uint32_t>(bad, at), op = value<std::uint32_t>(bad, at + 4);

            if (op == 20) {
                patch<std::uint64_t>(bad, at + 20, 1);
                changed = true;
                break;
            }

            at += 4 + size;
        }

        need(changed, "GL result fixture missing");
        rejects("wrong-result", bad);
        write_private_json(root / "result.json", {{"passed", true},
                                                  {"fresh_process_pixels_and_live_input", true},
                                                  {"historical_output_writes_suppressed", true},
                                                  {"truncated_rejected", true},
                                                  {"unknown_historical_output_rejected", true},
                                                  {"GL_result_mismatch_rejected", true}});
        std::cout
                << "C++ graphics replay: fresh worker pixels, captured uploads, live input, historical output suppression and three corrupt journal guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
