#include "graphics.hpp"
#include "model.hpp"
#include "process.hpp"
#include "qnx_adapter.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "graphics_main.cpp"
#include <exception>
using namespace voyemu::graphics;
using namespace voyemu;

static std::vector<std::byte> words(std::initializer_list<int> values) {
    std::vector<std::byte> result(values.size() * 4);
    std::size_t at = 0;

    for (auto value : values) {
        std::memcpy(result.data() + at, &value, 4);
        at += 4;
    }

    return result;
}

struct CaptureLogs {
    std::ostringstream output;
    std::streambuf *previous = std::cerr.rdbuf(output.rdbuf());

    ~CaptureLogs() {
        std::cerr.rdbuf(previous);
    }
};

static Json log_record(const std::string &text, std::string_view message) {
    std::istringstream lines(text);
    std::string line;
    Json found;

    while (std::getline(lines, line)) {
        auto value = Json::parse(line);

        if (value.at("message") == message) {
            assert(found.is_null());
            found = std::move(value);
        }
    }

    assert(!found.is_null());

    return found;
}

static void worker_terminal_reasons(const std::filesystem::path &parent,
                                    const std::filesystem::path &angle) {
    const auto directory = parent / "terminal";

    ensure_private_directory(directory);
    auto ram = directory / "host-fixture-ram.bin";
    {
        std::ofstream file(ram, std::ios::binary);
        file.seekp(ram_bytes - 1);
        file.put(0);
    }
    const auto address = ram_base + 4096;
    auto run = [&](std::string_view reason, std::uint32_t state, bool stop = false,
                   bool corrupt = false, bool bound = true) {
        std::array<std::uint32_t, 8> header{
                corrupt ? rpc_magic ^ 1 : rpc_magic, rpc_version, state, 0, 0, 0, 0, 0};
        {
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(4096);
            file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
            file.flush();
            assert(file);
        }

        if (stop) {
            std::ofstream(directory / "stop");
        }

        std::vector<std::string> values{"voyemu-graphics",  "--ram",      ram.string(),
                                        "--address",        "0x40001000", "--session",
                                        directory.string(), "--angle",    angle.string(),
                                        "--seconds",        "1",          "--no-controls"};
        const auto disposition = directory / "worker-disposition-73.json";
        std::filesystem::remove(disposition);

        if (bound) {
            values.insert(values.end(), {"--generation", "19", "--worker-nonce", "73",
                                         "--session-id", "terminal-session"});
        }

        std::vector<char *> args;

        for (auto &value : values) {
            args.push_back(value.data());
        }

        CaptureLogs capture;
        auto code = voyemu::graphics_main(int(args.size()), args.data());
        auto text = capture.output.str();

        if (code != (corrupt ? (bound ? rpc_channel_invalidated_exit_code : 1) : 0)) {
            throw std::runtime_error("worker terminal check failed for " + std::string(reason) +
                                     ": " + text);
        }

        assert(text.find("\"message\":\"rpc.profile\"") == std::string::npos);
        auto stopped = log_record(text, "producer.stopped");
        assert(stopped["fields"]["reason"] == reason && stopped["fields"]["address"] == address);

        if (corrupt) {
            assert(stopped["fields"]["error"] ==
                   (bound ? "Published RPC page header" : "GRAPHICS: Worker disposition identity"));
            auto failed = log_record(text, "rpc.header_failed").at("fields");
            assert(failed["producer_address"] == address);
            assert(failed["last_opcode"].is_null() && failed["last_sequence"].is_null() &&
                   failed["last_state"].is_null());
        } else {
            assert(stopped["fields"].contains("polls") &&
                   stopped["fields"].contains("identity_checks"));
        }

        if (corrupt && bound) {
            const auto receipt = read_private_json(disposition, 1024);
            assert(receipt.at("schema") == 1 && receipt.at("reason") == "rpc_channel_invalidated" &&
                   receipt.at("session_id") == "terminal-session" &&
                   receipt.at("generation") == 19 && receipt.at("nonce") == 73 &&
                   receipt.at("producer_address") == address &&
                   receipt.at("worker_pid").get<int>() > 0 && receipt.at("guest_pid") == 0 &&
                   receipt.at("producer_incarnation") == 0);
        } else {
            assert(!std::filesystem::exists(disposition));
        }

        if (stop) {
            std::filesystem::remove(directory / "stop");
        }
    };
    run("retired", 3);
    run("timeout", 0);
    run("stop", 0, true);
    run("rpc_channel_invalidated", 3, false, true);
    run("error", 3, false, true, false);
    // A transient producer must receive retirement acknowledgement before freeing RAM.
    {
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        std::uint64_t close = 0x3145534f4c434751ULL;
        file.seekp(4096 + 176);
        file.write(reinterpret_cast<const char *>(&close), 8);
        assert(file);
    }
    run("retired", 3);
    {
        std::ifstream file(ram, std::ios::binary);
        std::uint32_t ack = 0;
        file.seekg(4096 + 8);
        file.read(reinterpret_cast<char *>(&ack), 4);
        assert(ack == 4);
    }
    const auto receipt =
            read_private_json(directory / ("retired-" + std::to_string(address) + ".json"), 1024);
    assert(receipt.at("acknowledged") == true && receipt.at("producer_address") == address &&
           receipt.at("worker_pid").get<int>() > 0);
    run("rpc_channel_invalidated", 3, false,
        true); // A retirement hint never bypasses a corrupt live header.

    // A valid header at a reused address cannot admit another producer's requests.
    auto publish_owner = [&](std::uint64_t pid, std::uint64_t incarnation) {
        std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, 0, 0, 0, 0, 0, 0};
        std::array<std::uint64_t, 2> owner{pid, incarnation};
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(4096);
        file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
        file.seekp(4096 + 240);
        file.write(reinterpret_cast<const char *>(owner.data()), sizeof(owner));
        file.flush();
        assert(file);
    };
    publish_owner(41, 91);
    Options options{ram, directory, angle, {}, address, false};
    options.guest_pid = 41;
    options.producer_incarnation = 91;
    Backend backend(options);
    assert(!backend.service());

    for (const auto owner : {std::pair<std::uint64_t, std::uint64_t>{42, 91}, {41, 92}, {0, 0}}) {
        publish_owner(owner.first, owner.second);
        CaptureLogs capture;
        bool rejected = false;
        try {
            backend.service();
        } catch (const RpcChannelInvalidated &error) {
            rejected = std::string(error.what()) == "Published RPC page owner";
        }

        assert(rejected && !backend.finished());
        const auto fields = log_record(capture.output.str(), "rpc.owner_failed").at("fields");
        assert(fields.at("expected_guest_pid") == 41 &&
               fields.at("actual_guest_pid") == owner.first &&
               fields.at("expected_producer_incarnation") == 91 &&
               fields.at("actual_producer_incarnation") == owner.second);
    }
}

static void owner_liveness_protocol(const std::filesystem::path &parent,
                                    const std::filesystem::path &angle,
                                    const std::filesystem::path &ram) {
    std::vector<std::byte> bmp(54 + frames::pixel_bytes);

    bmp[0] = std::byte{'B'};
    bmp[1] = std::byte{'M'};
    auto put = [&]<class T>(std::size_t offset, T value) {
        std::memcpy(bmp.data() + offset, &value, sizeof(value));
    };
    put(10, std::uint32_t(54));
    put(14, std::uint32_t(40));
    put(18, std::int32_t(frames::width));
    put(22, -std::int32_t(frames::height));
    put(26, std::uint16_t(1));
    put(28, std::uint16_t(32));
    const auto pixels = ram_base + 1024 * 1024;
    {
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(pixels - ram_base);
        file.write(reinterpret_cast<const char *>(bmp.data()), bmp.size());
        assert(file);
    }
#ifdef _WIN32
    const std::uint64_t worker_pid = GetCurrentProcessId();
#else
    const std::uint64_t worker_pid = getpid();
#endif

    for (bool legacy : {false, true}) {
        const auto directory = parent / (legacy ? "owner-liveness-legacy" : "owner-liveness");
        ensure_private_directory(directory);
        Options options{ram, directory, angle, {}, ram_base + 65536, false};
        options.session_id = "liveness-session";
        options.generation = 19;
        options.worker_nonce = 73;
        options.guest_pid = legacy ? 0 : 41;
        options.producer_incarnation = legacy ? 0 : 91;
        Backend backend(options);
        auto rejected = [&](auto action) {
            bool failed = false;
            try {
                action();
            } catch (const std::runtime_error &) {
                failed = true;
            }

            assert(failed);
        };
        rejected([&] {
            backend.dispatch(owner_liveness_request_opcode, {}, {});
        });
        auto publish = [&](Backend &target) {
            const auto blob = target.dispatch(40, {bmp.size(), 0}, {}).result;
            std::array<std::uint64_t, 2> run{pixels, bmp.size()};
            std::vector<std::byte> descriptor(sizeof(run));
            std::memcpy(descriptor.data(), run.data(), sizeof(run));
            assert(target.dispatch(41, {blob, 1}, descriptor).result == 1);
            assert(target.dispatch(354, {blob, bmp.size(), 1}, {}).result == 1);
        };
        publish(backend);
        const auto stream_receipt = directory / "producer-40010000" / "screen-stream.json";
        assert(std::filesystem::exists(stream_receipt) == !legacy);
        assert(!backend.dispatch(owner_liveness_request_opcode, {}, {}).result);
        Json observer{{"nonce", options.worker_nonce},
                      {"producer_address", options.address},
                      {"worker_pid", worker_pid},
                      {"guest_pid", options.guest_pid},
                      {"producer_incarnation", options.producer_incarnation}};
        Json target{{"nonce", 74u},
                    {"producer_address", ram_base + 69632},
                    {"worker_pid", worker_pid + 1},
                    {"guest_pid", 42u},
                    {"producer_incarnation", 92u}};
        Json old{{"nonce", 75u},
                 {"producer_address", ram_base + 73728},
                 {"worker_pid", worker_pid + 2},
                 {"guest_pid", 0u},
                 {"producer_incarnation", 0u}};
        Json census{{"schema", 1},
                    {"session_id", options.session_id},
                    {"generation", options.generation},
                    {"challenge", 100u},
                    {"owners", Json::array({observer, target, old})}};
        const auto active = directory / "active-owners.json",
                   receipt_path = directory / "owner-death-74.json";
        write_private_json(active, census);
        Options foreign = options;
        foreign.address = ram_base + 77824;
        foreign.worker_nonce = 76;
        Backend spoof(foreign);
        publish(spoof);
        rejected([&] {
            spoof.dispatch(owner_liveness_request_opcode, {}, {});
        });
        auto request = backend.dispatch(owner_liveness_request_opcode, {}, {});
        assert(request.result == 1 && request.payload.size() == 32 + (legacy ? 1 : 2) * 40);
        std::vector<std::uint64_t> wire(request.payload.size() / 8);
        std::memcpy(wire.data(), request.payload.data(), request.payload.size());
        assert(wire[0] == 1 && wire[1] == 19 && wire[2] == 100 && wire[3] == (legacy ? 1 : 2));
        Arguments report{19, 100, 74, ram_base + 69632, worker_pid + 1, 42, 92, UINT64_MAX, 3};
        auto wrong = report;
        wrong[5] = 43;
        rejected([&] {
            backend.dispatch(owner_liveness_report_opcode, wrong, {});
        });
        wrong = report;
        wrong[8] = 1;
        rejected([&] {
            backend.dispatch(owner_liveness_report_opcode, wrong, {});
        });
        wrong = report;
        wrong[1] = 99;
        assert(!backend.dispatch(owner_liveness_report_opcode, wrong, {}).result &&
               !std::filesystem::exists(receipt_path));
        assert(backend.dispatch(owner_liveness_report_opcode, report, {}).result == 1);
        const auto receipt = read_private_json(receipt_path, 2048);

        for (const auto &[key, value] : target.items()) {
            assert(receipt.at(key) == value);
        }

        assert(receipt.at("schema") == 1 && receipt.at("session_id") == options.session_id &&
               receipt.at("generation") == 19 && receipt.at("challenge") == 100 &&
               receipt.at("result") == -1 && receipt.at("errno") == 3 &&
               receipt.at("observer_producer_address") == options.address &&
               receipt.at("observer_worker_pid") == worker_pid &&
               receipt.at("observer_nonce") == 73 &&
               receipt.at("observer_guest_pid") == options.guest_pid &&
               receipt.at("observer_producer_incarnation") == options.producer_incarnation);
        rejected([&] {
            backend.dispatch(owner_liveness_report_opcode, report, {});
        });
        assert(!backend.dispatch(owner_liveness_request_opcode, {}, {}).result);
        census["challenge"] = 101;
        write_private_json(active, census);
        assert(backend.dispatch(owner_liveness_request_opcode, {}, {}).result);
        report[1] = 101;
        assert(!backend.dispatch(owner_liveness_report_opcode, report, {}).result &&
               read_private_json(receipt_path).at("challenge") == 100);
        std::filesystem::remove(receipt_path);
        assert(backend.dispatch(owner_liveness_report_opcode, report, {}).result);
        census["session_id"] = "wrong-session";
        write_private_json(active, census);
        rejected([&] {
            backend.dispatch(owner_liveness_request_opcode, {}, {});
        });
        census["session_id"] = options.session_id;
        census["owners"][0]["worker_pid"] = worker_pid + 99;
        write_private_json(active, census);
        rejected([&] {
            backend.dispatch(owner_liveness_request_opcode, {}, {});
        });
    }
}

static void configured_ram_geometry(const std::filesystem::path &parent,
                                    const std::filesystem::path &angle) {
    const auto directory = parent / "passion-ram";

    ensure_private_directory(directory);
    const auto ram = directory / "host-fixture-ram.bin", journals = directory / "journals";
    ensure_private_directory(journals);
    constexpr auto extent = 11713ULL << 20;
    const auto address = ram_base + extent - 4096;
    {
        std::ofstream file(ram, std::ios::binary);
        file.seekp(extent - 1);
        file.put(0);
    }
    {
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, 3, 0, 0, 0, 0, 0};
        file.seekp(extent - 4096);
        file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
        assert(file);
    }
    Options options{ram, directory, angle, {}, address, false};
    options.ram_extent = extent;
    options.record = journals;
    {
        Backend high(options);
        const auto blob = high.dispatch(40, {4, 0}, {}).result;
        assert(blob);
        std::array<std::uint64_t, 2> run{address, 4};
        std::vector<std::byte> descriptors(sizeof(run));
        std::memcpy(descriptors.data(), run.data(), sizeof(run));
        assert(high.dispatch(41, {blob, 1}, descriptors).result == 1);
        assert(high.dispatch(42, {blob}, {}).result == 1);
        assert(!high.service() && high.finished());
    }
    std::ostringstream name;
    name << std::hex << address << ".gpu";
    {
        std::ifstream file(journals / name.str(), std::ios::binary);
        std::array<std::uint64_t, 3> header{};
        file.read(reinterpret_cast<char *>(header.data()), 24);
        assert(file && header[2] == extent);
    }
    auto rejected = [&](Options invalid, const char *expected) {
        bool rejected = false;
        try {
            Backend backend(invalid);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()) == expected;
        }

        assert(rejected);
    };
    options.record.clear();
    options.ram_extent = ram_bytes;
    rejected(options, "GRAPHICS: RAM backing geometry");
    options.ram_extent = extent;
    options.address = ram_base + extent;
    rejected(options, "GRAPHICS: RPC page physical geometry");
    options.ram_extent = 12ULL << 30;
    rejected(options, "GRAPHICS: RAM configured geometry");
    std::vector<std::string> values{"voyemu-graphics",
                                    "--ram",
                                    ram.string(),
                                    "--ram-bytes",
                                    std::to_string(extent),
                                    "--address",
                                    std::to_string(address),
                                    "--session",
                                    directory.string(),
                                    "--angle",
                                    angle.string(),
                                    "--seconds",
                                    "1",
                                    "--no-controls"};
    std::vector<char *> args;

    for (auto &value : values) {
        args.push_back(value.data());
    }

    CaptureLogs capture;
    assert(voyemu::graphics_main(int(args.size()), args.data()) == 0);
    assert(log_record(capture.output.str(), "producer.stopped")["fields"]["reason"] == "retired");
}

static void qinggan11_theme_requests(const std::filesystem::path &parent,
                                     const std::filesystem::path &angle,
                                     const std::filesystem::path &ram) {
    const auto directory = parent / "qinggan11-611-theme";

    ensure_private_directory(directory);
    const auto address = ram_base + 65536;
    Backend backend({ram, directory, angle, {}, address, true, false});
    auto command = [&](unsigned seq, unsigned mode, unsigned skin) {
        write_private_json(directory / "theme-command.json",
                           {{"seq", seq}, {"theme_id", mode}, {"theme_skin", skin}});
    };
    std::ostringstream name;
    name << "producer-" << std::hex << address;
    const auto producer = directory / name.str();
    Arguments report{};
    report[1] = 1;
    report[7] = 1;
    command(1, 2, 1001);
    // Any other guest takes the mode of the same command.
    auto mode_only = backend.dispatch(355, report, {});
    assert(mode_only.result == 1 && mode_only.payload == std::vector<std::byte>{std::byte{2}});
    report[6] = 0x6110002;
    assert(backend.dispatch(355, report, {}).result == 1);
    report[6] = 0x6110001;
    write_private_json(directory / "theme-command.json", {{"seq", 1u}, {"theme_id", 2}});
    assert(!backend.dispatch(355, report, {}).result);
    command(1, 2, 1001);
    auto first = backend.dispatch(355, report, {});
    assert(first.result == 1 && first.payload.size() == 8);
    unsigned values[2];
    std::memcpy(values, first.payload.data(), 8);
    assert(values[0] == 2 && values[1] == 1001);
    report[1] = 2;
    report[7] = 1001;
    report[8] = 1;
    report[9] = 2;
    assert(!backend.dispatch(355, report, {}).result);
    assert(!std::filesystem::exists(producer / "theme-ack.json"));
    assert(read_private_json(producer / "theme-observed.json").at("native_outcome") ==
           "skin_ipc_failed");
    command(2, 4, 3001);
    report[8] = report[9] = 0;
    auto second = backend.dispatch(355, report, {});
    assert(second.result == 2 && second.payload.size() == 8);
    report[0] = 2;
    report[1] = 4;
    report[7] = 3001;
    report[8] = 2;
    report[9] = 4;
    assert(!backend.dispatch(355, report, {}).result);
    const auto ack = read_private_json(producer / "theme-ack.json");
    assert(ack.at("sequence") == 2 && ack.at("theme_skin") == 3001 &&
           ack.at("observed_requested_theme") == 4 && ack.at("observed_skin") == 3001);
    command(3, 4, 3002);
    auto next = backend.dispatch(355, report, {});
    assert(next.result == 3 && next.payload.size() == 8);
    std::memcpy(values, next.payload.data(), 8);
    assert(values[1] == 3002);
    report[8] = 3;
    report[9] = 3;
    report[1] = 4;
    report[7] = 3002;
    assert(!backend.dispatch(355, report, {}).result);
    auto partial = read_private_json(producer / "theme-observed.json");
    assert(!partial.at("request_suppressed").get<bool>() &&
           partial.at("native_outcome") == "getter_mismatch");
    assert(read_private_json(producer / "theme-ack.json") == ack);

    for (const auto &bad : {Json{{"seq", 4u}, {"theme_id", 4}, {"theme_skin", 1002}},
                            Json{{"seq", 4u}, {"theme_id", 2}, {"theme_skin", 3001}},
                            Json{{"seq", 4u}, {"theme_id", 4}, {"theme_skin", 4294970297ULL}}}) {
        write_private_json(directory / "theme-command.json", bad);
        bool denied = false;
        try {
            backend.dispatch(355, report, {});
        } catch (...) {
            denied = true;
        }

        assert(denied && read_private_json(producer / "theme-ack.json") == ack);
    }
}

static void theme_request_logging(const std::filesystem::path &parent,
                                  const std::filesystem::path &angle,
                                  const std::filesystem::path &ram) {
    const auto directory = parent / "theme-requests";

    ensure_private_directory(directory);
    const auto address = ram_base + 32768;
    Backend backend({ram, directory, angle, {}, address, true, false});
    write_private_json(directory / "theme-command.json", {{"seq", 1u}, {"theme_id", 2}});
    {
        CaptureLogs capture;
        auto first = backend.dispatch(355, {0, 6}, {});
        assert(first.result == 1 && first.payload == std::vector<std::byte>{std::byte{2}});
        assert(backend.dispatch(355, {0, 2}, {}).result == 1);
        assert(!backend.dispatch(355, {1, 2}, {}).result);
        auto record = log_record(capture.output.str(), "theme.request");
        assert(record["level"] == "info");
        auto fields = record.at("fields");
        assert(fields["producer_address"] == address && fields["sequence"] == 1 &&
               fields["observed_display_theme"] == 6 && fields["requested_theme"] == 2);
    }
    std::ostringstream producer;
    producer << "producer-" << std::hex << address;
    assert(read_private_json(directory / producer.str() / "theme-ack.json")["sequence"] == 1);
    write_private_json(directory / "theme-command.json", {{"seq", 2u}, {"theme_id", 4}});
    {
        CaptureLogs capture;
        assert(backend.dispatch(355, {1, 2}, {}).result == 2);
        assert(!backend.dispatch(355, {2, 4}, {}).result);
        auto fields = log_record(capture.output.str(), "theme.request").at("fields");
        assert(fields["sequence"] == 2 && fields["observed_display_theme"] == 2 &&
               fields["requested_theme"] == 4);
    }
    const auto cold = parent / "theme-cold";
    ensure_private_directory(cold);
    Backend fresh({ram, cold, angle, {}, address, true, false});
    write_private_json(cold / "theme-command.json", {{"seq", 1u}, {"theme_id", 2}});
    {
        CaptureLogs capture;
        assert(!fresh.dispatch(355, {0, 5}, {}).result);
        auto fields = log_record(capture.output.str(), "theme.request").at("fields");
        assert(fields["action"] == "already_selected");
        auto observed = read_private_json(cold / producer.str() / "theme-observed.json");
        assert(observed["request_suppressed"] == true && observed["observed_display_theme"] == 5);
        assert(!std::filesystem::exists(cold / producer.str() / "theme-ack.json"));
        assert(fresh.dispatch(355, {0, 6}, {}).result == 1);
        assert(fresh.dispatch(355, {0, 5}, {}).result ==
               1); // A matching getter cannot replace an outstanding guest ACK.
        assert(!fresh.dispatch(355, {1, 5}, {}).result);
    }
    const auto replay = parent / "theme-replay";
    ensure_private_directory(replay);
    std::ostringstream journal_name;
    journal_name << std::hex << address << ".gpu";
    {
        std::ofstream file(replay / journal_name.str(), std::ios::binary);
        std::array<std::uint64_t, 3> header{0x3130305550475156ULL, address, ram_bytes};
        file.write(reinterpret_cast<const char *>(header.data()), 24);
    }
    const auto warm = parent / "theme-restored";
    ensure_private_directory(warm);
    Backend restored({ram, warm, angle, {}, address, true, false, {}, replay});
    write_private_json(warm / "theme-command.json", {{"seq", 1u}, {"theme_id", 2}});
    {
        CaptureLogs capture;
        assert(restored.dispatch(355, {0, 5}, {}).result == 0);
        assert(restored.dispatch(355, {0, 5}, {}).result == 0);
        auto fields = log_record(capture.output.str(), "theme.request").at("fields");
        assert(fields["action"] == "already_selected");
        auto observed = read_private_json(warm / producer.str() / "theme-observed.json");
        assert(observed["sequence"] == 1 && observed["observed_display_theme"] == 5 &&
               observed["request_suppressed"] == true);
        assert(!std::filesystem::exists(warm / producer.str() / "theme-ack.json"));
    }
    write_private_json(warm / "theme-command.json", {{"seq", 2u}, {"theme_id", 4}});
    {
        CaptureLogs capture;
        assert(restored.dispatch(355, {0, 2}, {}).result == 2);
        assert(restored.dispatch(355, {0, 4}, {}).result ==
               2); // A changed getter without ACK must not suppress a previously issued request.
        auto fields = log_record(capture.output.str(), "theme.request").at("fields");
        assert(fields["action"] == "request_change");
    }
    write_private_json(warm / "theme-command.json", {{"seq", 3u}, {"theme_id", 4}});
    {
        CaptureLogs capture;
        assert(restored.dispatch(355, {0, 4}, {}).result ==
               3); // A newer same-mode request must not hide an unacknowledged earlier issue.
        assert(!restored.dispatch(355, {3, 4}, {}).result);
        auto fields = log_record(capture.output.str(), "theme.request").at("fields");
        assert(fields["action"] == "request_change" && fields["sequence"] == 3);
        assert(read_private_json(warm / producer.str() / "theme-ack.json")["sequence"] == 3);
    }
}

static void qinggan11_unprojected_fields(const std::filesystem::path &parent,
                                         const std::filesystem::path &angle,
                                         const std::filesystem::path &ram) {
    const auto &fields = catalog(VehicleCatalog::qinggan11);
    auto position = [&](std::string_view name) {
        const auto at = std::find_if(fields.begin(), fields.end(), [&](const Field &field) {
            return field.name == name;
        });
        assert(at != fields.end());

        return static_cast<std::size_t>(at - fields.begin());
    };
    auto run = [&](std::string_view name, const QnxAdapterSpec &adapter, unsigned language) {
        const auto directory = parent / name;
        ensure_private_directory(directory);
        auto state = apply_patch(initial_state(VehicleCatalog::qinggan11),
                                 {{"ivi_languageSet", language},
                                  {"set_byte0", 7},
                                  {"acc_mode", 3},
                                  {"mpc_icaSts", 1},
                                  {"time_gap_set", 1},
                                  {"ADCU_TargetType_1", 5},
                                  {"speed", 80}});
        write_private_json(directory / "vehicle-state.json", state);
        Options options{ram, directory, angle, {}, ram_base + 73728, true, false};
        options.mcu_adapter = adapter.name;
        Backend backend(options);
        auto reply = backend.dispatch(364, {0, 650, 736, adapter.mcu_projected_fields}, {});
        assert(reply.result == state.at("sequence").get<std::uint64_t>() &&
               reply.payload.size() == (fields.size() + 2) * 4);
        std::vector<unsigned> values(fields.size() + 2);
        std::memcpy(values.data(), reply.payload.data(), values.size() * 4);
        assert(values[0] == 1 && values[1] == fields.size());
        // Both qinggan11 layouts carry the language of the model in the MCU word.
        assert(values[2 + position("ivi_languageSet")] == language);
        // A settings byte, which the settings feedback owns, reaches the guest with its initial
        // value; the ADAS states, a lane target and the speed pass.
        assert(values[2 + position("set_byte0")] == 0);
        assert(values[2 + position("acc_mode")] == 3 && values[2 + position("mpc_icaSts")] == 1 &&
               values[2 + position("time_gap_set")] == 1 &&
               values[2 + position("ADCU_TargetType_1")] == 5);
        assert(values[2 + position("speed")] == 80 && values[2 + position("speed10x")] == 800);
        assert(read_private_json(directory / "vehicle-state.json") == state);
        // A state of the qinggan09 catalogue does not reach a qinggan11 guest.
        write_private_json(directory / "vehicle-state.json",
                           apply_patch(initial_state(VehicleCatalog::qinggan09), {{"speed", 80}}));
        bool rejected = false;
        try {
            Backend foreign(options);
            (void)foreign.dispatch(364, {0, 650, 736, adapter.mcu_projected_fields}, {});
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    };

    const auto &qinggan11_611 = qnx_adapter(QnxGuestAdapter::sa8155_qinggan11_611);

    run("qinggan11-611-language-en", qinggan11_611, 1);
    run("qinggan11-611-language-other", qinggan11_611, 2);
    run("qinggan11-731-language", qnx_adapter(QnxGuestAdapter::sa8155_qinggan11_731), 1);
}

static void input_wake_protocol(const std::filesystem::path &parent,
                                const std::filesystem::path &angle,
                                const std::filesystem::path &ram) {
    constexpr std::uint64_t magic = 0x31454b41574e4951ULL;
    const auto directory = parent / "input-wake";

    ensure_private_directory(directory);
    auto page = [&](std::size_t offset, std::uint64_t cookie) {
        std::array<std::byte, 4096> bytes{};
        std::array<std::uint32_t, 2> header{rpc_magic, rpc_version};
        std::memcpy(bytes.data(), header.data(), 8);
        std::memcpy(bytes.data() + 176, &cookie, 8);
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(offset);
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        assert(file);
    };
    auto word = [&](std::size_t offset) {
        std::uint64_t value = 0;
        std::ifstream file(ram, std::ios::binary);
        file.seekg(offset);
        file.read(reinterpret_cast<char *>(&value), 8);
        assert(file);

        return value;
    };
    auto cookie = [&](std::uint64_t value) {
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(8192 + 176);
        file.write(reinterpret_cast<const char *>(&value), 8);
        assert(file);
    };
    auto tick = [&](Backend &backend) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        assert(!backend.service());
    };
    page(8192, magic);
    Backend owner({ram, directory, angle, {}, ram_base + 8192, true, false});
    assert(!owner.service() && !word(8192 + 184));
    auto state = initial_state(VehicleCatalog::qinggan09);
    write_private_json(directory / "vehicle-state.json", state);
    assert(owner.dispatch(364, {}, {}).result == 1);
    cookie(0);
    state = apply_patch(state, {{"speed", 114}});
    write_private_json(directory / "vehicle-state.json", state);
    tick(owner);
    assert(!word(8192 + 184) && !word(8192 + 192));
    cookie(magic);
    tick(owner);
    assert(word(8192 + 184) == magic &&
           word(8192 + 192) == (std::uint64_t(state["sequence"].get<unsigned>()) << 32));
    auto sequence = state["sequence"].get<unsigned>();
    owner.dispatch(364, {1}, {});
    tick(owner);
    assert(!word(8192 + 192));
    write_private_json(directory / "wheel-command.json",
                       {{"sequence", 1u}, {"index", 322}, {"value", 1}});
    tick(owner);
    assert(word(8192 + 192) == 1);
    owner.dispatch(363, {}, {});
    tick(owner);
    assert(!word(8192 + 192));
    auto invalid = state;
    invalid["sequence"] = 0x100000000ULL;
    write_private_json(directory / "vehicle-state.json", invalid);
    tick(owner);
    assert(!word(8192 + 192));
    write_private_json(directory / "vehicle-state.json", state);
    tick(owner);
    page(12288, magic);
    Backend foreign({ram, directory, angle, {}, ram_base + 12288, true, false});
    assert(foreign.dispatch(364, {}, {}).result == 0);
    tick(foreign);
    assert(!word(12288 + 184) && !word(12288 + 192));
    page(8192, magic);
    {
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        std::uint32_t retired = 3;
        file.seekp(8192 + 8);
        file.write(reinterpret_cast<const char *>(&retired), 4);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    assert(!owner.service() && owner.finished() && !word(8192 + 184));
    assert(sequence == 2);
}

static void control_idle_polling(const std::filesystem::path &parent,
                                 const std::filesystem::path &angle,
                                 const std::filesystem::path &ram) {
    const auto directory = parent / "control-idle";

    ensure_private_directory(directory);
    write_private_json(directory / "vehicle-state.json", initial_state(VehicleCatalog::qinggan09));
    std::array<std::byte, 4096> page{};
    std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, 1, 364, 1, 0, 0, 0};
    std::memcpy(page.data(), header.data(), sizeof(header));
    {
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(16384);
        file.write(reinterpret_cast<const char *>(page.data()), page.size());
        assert(file);
    }
    std::vector<std::string> values{
            "voyemu-graphics",  "--ram",   ram.string(),   "--address", "0x40004000", "--session",
            directory.string(), "--angle", angle.string(), "--seconds", "1"};
    std::vector<char *> args;

    for (auto &value : values) {
        args.push_back(value.data());
    }

    CaptureLogs capture;
    assert(voyemu::graphics_main(int(args.size()), args.data()) == 0);
    auto stopped = log_record(capture.output.str(), "producer.stopped").at("fields");
    assert(stopped["reason"] == "timeout");
    auto polls = stopped["polls"].get<unsigned>();
    std::cout << "control idle polls per bounded second=" << polls << '\n';
    assert(polls >= 100 && polls < 1800);
    assert(std::filesystem::exists(directory / "control-owner.json"));
}

static void control_idle_mode_guards(const std::filesystem::path &parent,
                                     const std::filesystem::path &angle,
                                     const std::filesystem::path &ram) {
    const auto directory = parent / "idle-mode";

    ensure_private_directory(directory);
    write_private_json(directory / "vehicle-state.json", initial_state(VehicleCatalog::qinggan09));
    auto request = [&](Backend &backend, std::size_t offset, unsigned opcode, unsigned sequence) {
        std::array<std::byte, 4096> bytes{};
        std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, 1, opcode, sequence, 0, 0, 0};
        std::memcpy(bytes.data(), header.data(), sizeof(header));
        std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(offset);
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        file.flush();
        assert(file);
        assert(backend.service());
    };
    Backend owner({ram, directory, angle, {}, ram_base + 20480, true, false});
    assert(owner.idle_wait_us() == 100);
    request(owner, 20480, 364, 1);
    assert(owner.idle_wait_us() == 1000);
    request(owner, 20480, 363, 2);
    assert(owner.idle_wait_us() == 1000);
    request(owner, 20480, 9999, 3);
    assert(owner.idle_wait_us() == 100);
    request(owner, 20480, 364, 4);
    assert(owner.idle_wait_us() == 100);
    Backend foreign({ram, directory, angle, {}, ram_base + 24576, true, false});
    request(foreign, 24576, 364, 1);
    assert(foreign.idle_wait_us() == 100);
    const auto mixed_directory = parent / "idle-mixed";
    ensure_private_directory(mixed_directory);
    write_private_json(mixed_directory / "vehicle-state.json",
                       initial_state(VehicleCatalog::qinggan09));
    Backend mixed({ram, mixed_directory, angle, {}, ram_base + 28672, true, false});
    request(mixed, 28672, 42, 1);
    assert(mixed.idle_wait_us() == 100);
    request(mixed, 28672, 364, 2);
    assert(mixed.idle_wait_us() == 100);
    assert(!mixed.service());
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    assert(!mixed.service());
    assert(mixed.idle_wait_us() == 500);
    request(mixed, 28672, 364, 3);
    assert(mixed.idle_wait_us() == 100);
}

int main(int argc, char **argv) try {
    if (argc != 2) {
        std::cerr << "test needs pinned ANGLE directory\n";

        return 2;
    }

    if (!std::filesystem::is_directory(argv[1])) {
        std::cerr << "ANGLE integration requires the pinned SDK; run voyemu deps\n";

        return 77;
    }

    auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                     ("voyemu-angle-native-" + random_token());
    ensure_private_directory(directory);
    auto ram = directory / "host-fixture-ram.bin";
    struct Cleanup {
        std::filesystem::path p;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(p, ignored);
        }
    } cleanup{directory};
    {
        std::ofstream file(ram, std::ios::binary);
        file.seekp(ram_bytes - 1);
        file.put(0);
    }
    try {
#ifdef _WIN32
        const auto invalid_angle = directory / "invalid-angle";
        ensure_private_directory(invalid_angle);

        for (const char *name : {"libEGL.dll", "libGLESv2.dll"}) {
            std::ofstream(invalid_angle / name, std::ios::binary).put(0);
        }

        bool wrong_angle_rejected = false;
        try {
            Backend invalid({ram, directory, invalid_angle, {}, ram_base + 4096, false, true});
        } catch (const std::runtime_error &error) {
            wrong_angle_rejected = std::string(error.what()) ==
                                   "GRAPHICS: ANGLE Windows pin mismatch";
        }

        assert(wrong_angle_rejected);
#endif
        worker_terminal_reasons(directory, argv[1]);
        owner_liveness_protocol(directory, argv[1], ram);
        configured_ram_geometry(directory, argv[1]);
        qinggan11_unprojected_fields(directory, argv[1], ram);
        input_wake_protocol(directory, argv[1], ram);
        control_idle_polling(directory, argv[1], ram);
        control_idle_mode_guards(directory, argv[1], ram);
        theme_request_logging(directory, argv[1], ram);
        qinggan11_theme_requests(directory, argv[1], ram);
        Options options{ram, directory, argv[1], {}, ram_base + 4096, false, true};
        Backend backend(options);
        auto invoke = [&](unsigned op, Arguments values = {}, std::vector<std::byte> data = {}) {
            return backend.dispatch(op, values, data);
        };
        auto display = invoke(1).result;
        assert(display);
        auto initialized = invoke(2, {display});
        assert(initialized.result && initialized.payload.size() == 8);
        // Enumerate the driver's complete list before allocating the guest-side array.
        auto counted = invoke(60, {display, 0});
        assert(counted.result && counted.payload.size() == 4);
        int config_count = 0;
        std::memcpy(&config_count, counted.payload.data(), 4);
        assert(config_count > 0 && config_count <= 256);
        auto all = invoke(60, {display, std::uint64_t(config_count)});
        assert(all.result && all.payload.size() == 4 + std::size_t(config_count) * 8);
        int fetched = 0;
        std::memcpy(&fetched, all.payload.data(), 4);
        assert(fetched == config_count);
        std::set<std::uint64_t> enumerated;

        for (int i = 0; i < config_count; i++) {
            std::uint64_t handle = 0;
            std::memcpy(&handle, all.payload.data() + 4 + static_cast<std::size_t>(i) * 8, 8);
            assert(handle && enumerated.insert(handle).second);
        }

        auto capacity = invoke(60, {display, 256});
        assert(capacity.result && capacity.payload == all.payload);

        for (unsigned op : {6u, 60u}) {
            bool rejected = false;
            try {
                invoke(op, {display, 257}, op == 6 ? words({0x3038}) : std::vector<std::byte>{});
            } catch (const std::runtime_error &error) {
                rejected = std::string(error.what()) == "GRAPHICS: EGL config capacity";
            }

            assert(rejected);
        }

        auto matched_count = invoke(6, {display, 0},
                                    words({0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8,
                                           0x3021, 8, 0x3025, 24, 0x3026, 8, 0x3038}));
        int matching = 0;
        assert(matched_count.result && matched_count.payload.size() == 4);
        std::memcpy(&matching, matched_count.payload.data(), 4);
        assert(matching > 0 && matching <= config_count);
        auto matched = invoke(6, {display, std::uint64_t(matching)},
                              words({0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021,
                                     8, 0x3025, 24, 0x3026, 8, 0x3038}));
        assert(matched.result && matched.payload.size() == 4 + std::size_t(matching) * 8);

        for (int i = 0; i < matching; i++) {
            std::uint64_t handle = 0;
            std::memcpy(&handle, matched.payload.data() + 4 + static_cast<std::size_t>(i) * 8, 8);
            assert(enumerated.contains(handle));
        }

        auto selected = invoke(6, {display, 1},
                               words({0x3033, 1, 0x3040, 0x40, 0x3024, 8, 0x3023, 8, 0x3022, 8,
                                      0x3021, 8, 0x3038}));
        assert(selected.result && selected.payload.size() >= 12);
        std::uint64_t config = 0;
        std::memcpy(&config, selected.payload.data() + 4, 8);
        assert(config);

        // The color assertions below require eight bits per channel, not an arbitrary RGB565
        // config.
        for (unsigned attribute : {0x3024u, 0x3023u, 0x3022u, 0x3021u}) {
            const auto channel = invoke(61, {display, config, attribute});
            assert(channel.result && channel.payload.size() == 4);
            int depth = 0;
            std::memcpy(&depth, channel.payload.data(), 4);
            assert(depth >= 8);
        }

        auto context = invoke(7, {display, config, 0}, words({0x3098, 3, 0x3038})).result;
        assert(context);
        auto surface = invoke(8, {display, config}, words({0x3057, 16, 0x3056, 16, 0x3038})).result;
        assert(surface);
        {
            CaptureLogs capture;
            assert(invoke(9, {display, surface, surface, context}).result);
            const auto renderer =
                    log_record(capture.output.str(), "renderer.selected").at("fields");

            for (const auto &[name, key] : std::array<std::pair<const char *, unsigned>, 3>{
                         {{"vendor", 0x1f00}, {"renderer", 0x1f01}, {"version", 0x1f02}}}) {
                const auto value = invoke(21, {key});
                assert(value.result && !value.payload.empty());
                const std::string text(reinterpret_cast<const char *>(value.payload.data()),
                                       value.payload.size());
                assert(renderer.at(name) == text);
                std::cout << "ANGLE " << name << "=" << text << '\n';
            }

            const auto logged = capture.output.str();
            assert(invoke(9, {display, surface, surface, context}).result);
            assert(capture.output.str() == logged); // One diagnostic per producer, never per frame.
        }
        assert(invoke(70, {0x3059}).result == surface && invoke(70, {0x305a}).result == surface);
        assert(!invoke(70, {0}).result && invoke(3).result == 0x300c);
        const float red = .25f, green = .5f, blue = .75f, alpha = 1.f;
        auto bits = [](float value) {
            std::uint32_t out;
            std::memcpy(&out, &value, 4);

            return out;
        };
        invoke(22, {bits(red), bits(green), bits(blue), bits(alpha)});
        invoke(23, {0x4000});
        auto pixels = invoke(24, {0, 0, 16, 16, 0x1908, 0x1401, 0});
        assert(pixels.payload.size() == 16 * 16 * 4);
        auto byte = [](std::byte value) {
            return std::to_integer<int>(value);
        };

        if (byte(pixels.payload[0]) < 63 || byte(pixels.payload[0]) > 64) {
            throw std::runtime_error(
                    "RGBA readback mismatch: " + std::to_string(byte(pixels.payload[0])) + "," +
                    std::to_string(byte(pixels.payload[1])) + "," +
                    std::to_string(byte(pixels.payload[2])) + "," +
                    std::to_string(byte(pixels.payload[3])));
        }

        assert(byte(pixels.payload[1]) >= 127 && byte(pixels.payload[1]) <= 128);
        assert(byte(pixels.payload[2]) >= 191 && byte(pixels.payload[2]) <= 192);
        assert(byte(pixels.payload[3]) == 255);
        assert(invoke(20).result == 0);
        std::array<float, 4> clear_values{.75f, .25f, .5f, 1.f};
        std::vector<std::byte> clear_bytes(sizeof(clear_values));
        std::memcpy(clear_bytes.data(), clear_values.data(), clear_bytes.size());
        invoke(113, {0x1800, 0, 0}, clear_bytes);
        auto clear_pixels = invoke(24, {0, 0, 16, 16, 0x1908, 0x1401, 0});
        assert(byte(clear_pixels.payload[0]) >= 191 && byte(clear_pixels.payload[0]) <= 192 &&
               byte(clear_pixels.payload[1]) >= 63 && byte(clear_pixels.payload[1]) <= 64);
        clear_values = {.125f, .75f, .5f, 1.f};
        std::memcpy(clear_bytes.data(), clear_values.data(), clear_bytes.size());
        std::array<std::uint32_t, 4> clear_header{113, 3, 16, 0};
        std::array<std::uint64_t, 3> clear_arguments{0x1800, 0, 0};
        std::vector<std::byte> clear_batch(56);
        std::memcpy(clear_batch.data(), clear_header.data(), 16);
        std::memcpy(clear_batch.data() + 16, clear_arguments.data(), 24);
        std::memcpy(clear_batch.data() + 40, clear_bytes.data(), 16);
        auto bad_batch = clear_batch;
        auto bad_record = clear_batch;
        std::uint32_t bad_size = 15;
        std::memcpy(bad_record.data() + 8, &bad_size, 4);
        bad_record.back() = std::byte{0};
        bad_batch.insert(bad_batch.end(), bad_record.begin(), bad_record.end());
        bool clear_rejected = false;
        try {
            invoke(360, {2}, bad_batch);
        } catch (const std::runtime_error &error) {
            clear_rejected = std::string(error.what()) == "GRAPHICS: Inline clear extent";
        }

        assert(clear_rejected);
        auto unchanged = invoke(24, {0, 0, 16, 16, 0x1908, 0x1401, 0});
        assert(unchanged.payload == clear_pixels.payload);
        assert(invoke(360, {1}, clear_batch).result == 1);
        auto batched_pixels = invoke(24, {0, 0, 16, 16, 0x1908, 0x1401, 0});
        assert(byte(batched_pixels.payload[0]) >= 31 && byte(batched_pixels.payload[0]) <= 32 &&
               byte(batched_pixels.payload[1]) >= 191 && byte(batched_pixels.payload[1]) <= 192);
        assert(invoke(20).result == 0);

        for (unsigned object_op : {139u, 140u, 141u, 142u}) {
            auto generated = invoke(object_op, {4, 0});
            assert(generated.result == 16 && generated.payload.size() == 16);
            std::set<std::uint32_t> names;

            for (unsigned i = 0; i < 4; i++) {
                std::uint32_t id = 0;
                std::memcpy(&id, generated.payload.data() + static_cast<std::size_t>(i) * 4, 4);
                assert(id && names.insert(id).second);
            }

            auto maximum = invoke(object_op, {960, 0});
            assert(maximum.result == 3840 && maximum.payload.size() == 3840);
            auto empty = invoke(object_op, {0, 0});
            assert(!empty.result && empty.payload.empty());

            for (std::uint64_t invalid : {961ULL, 16777217ULL, 0x100000000ULL, ~0ULL}) {
                bool rejected = false;
                try {
                    invoke(object_op, {invalid, 0});
                } catch (const std::runtime_error &) {
                    rejected = true;
                }

                assert(rejected);
            }
        }

        auto output_blob = invoke(40, {3844, 1}).result;
        std::array<std::uint64_t, 2> output_run{ram_base + 16384, 3844};
        std::vector<std::byte> output_runs(16);
        std::memcpy(output_runs.data(), output_run.data(), 16);
        assert(invoke(41, {output_blob, 1}, output_runs).result);
        auto legacy = invoke(139, {961, output_blob});
        assert(legacy.result == 3844 && legacy.payload.empty());
        std::uint32_t legacy_id = 0;
        {
            std::ifstream file(ram, std::ios::binary);
            file.seekg(16384);
            file.read(reinterpret_cast<char *>(&legacy_id), 4);
            assert(file);
        }
        assert(legacy_id);
        invoke(42, {output_blob});
        assert(invoke(20).result == 0);
        // Inline texture bytes must obey actual GL unpack state and retain NULL storage semantics.
        auto object_name = [&](unsigned op) {
            auto reply = invoke(op, {1, 0});
            std::uint32_t value = 0;
            std::memcpy(&value, reply.payload.data(), 4);

            return value;
        };
        const auto texture = object_name(142), framebuffer = object_name(140);
        invoke(105, {0xde1, texture});
        // Original Passion Qt alpha atlas, advertised by the native ANGLE texture capability.
        invoke(178, {0xde1, 0, 0x1906, 10240, 558, 0, 0x1906, 0x1401, 0});
        assert(invoke(20).result == 0);

        // Passion's post-self-check storage requests have no payload or unpack buffer.
        for (const auto width : {760U, 1520U}) {
            invoke(178, {0xde1, 0, 0x8058, width, width == 760 ? 720U : 1440U, 0, 0, 0x1401, 0});
            assert(invoke(20).result == 0x0500 && invoke(20).result == 0);
        }

        const Arguments invalid_source{0xde1, 0, 0x8058, 760, 720, 0, 0, 0x1401, 0};
        bool rejected_upload = false;
        try {
            invoke(178, invalid_source, std::vector<std::byte>(4));
        } catch (const VoyemuError &error) {
            rejected_upload = std::string_view(error.what()).find("inline_bytes=4") !=
                              std::string_view::npos;
        }

        assert(rejected_upload);
        const auto unpack_buffer = object_name(139);
        invoke(102, {0x88ec, unpack_buffer});
        bool rejected_buffer = false;
        try {
            invoke(178, invalid_source);
        } catch (const VoyemuError &error) {
            rejected_buffer = std::string_view(error.what()).find("unpack_buffer=1") !=
                              std::string_view::npos;
        }

        invoke(102, {0x88ec, 0});
        assert(rejected_buffer && invoke(20).result == 0);

        const auto index_buffer = object_name(139);
        invoke(102, {0x8893, index_buffer});

        for (const auto bytes : {96U, 144U}) {
            invoke(109, {0x8893, bytes, 0, 0x88e4});
            bool rejected_index_range = false;
            try {
                invoke(352, {192, 0x1405, 312});
            } catch (const VoyemuError &error) {
                rejected_index_range =
                        std::string(error.what()) ==
                        "GRAPHICS: Element-buffer draw extent: indices=192 type=5125 "
                        "offset=312 buffer_bytes=" +
                                std::to_string(bytes);
            }

            assert(rejected_index_range && invoke(20).result == 0);
        }

        invoke(102, {0x8893, 0});

        // A rejected OEM texture must report bounded scalar arguments without copying its pixels.
        bool dimension_context = false;
        try {
            invoke(178, {0xde1, 0, 0x8058, 16385, 2, 0, 0x1908, 0x1401, 0});
        } catch (const std::runtime_error &error) {
            dimension_context =
                    std::string(error.what()) ==
                    "GRAPHICS: Texture dimensions: target=3553 level=0 width=16385 height=2 "
                    "format=6408 type=5121";
        }

        assert(dimension_context);

        // Geometry remains bounded independently of copied bytes and subimage offsets.
        bool upload_extent = false;
        try {
            invoke(178, {0xde1, 0, 0x8058, 16384, 16384, 0, 0x1908, 0x1401, 0});
        } catch (const std::runtime_error &error) {
            upload_extent = std::string(error.what()) == "GRAPHICS: Texture upload extent";
        }

        assert(upload_extent);

        for (auto offset : {16384ULL, ~0ULL}) {
            bool bounds = false;
            try {
                invoke(184, {0xde1, 0, offset, 0, 1, 1, 0x1906, 0x1401, 0},
                       std::vector<std::byte>(1));
            } catch (const std::runtime_error &error) {
                bounds = std::string(error.what()) == "GRAPHICS: Texture subimage bounds";
            }

            assert(bounds);
        }

        std::vector<std::byte> texels(16);

        for (unsigned i = 0; i < 4; i++) {
            texels[static_cast<std::size_t>(i) * 4] = std::byte{31};
            texels[i * 4 + 1] = std::byte{63};
            texels[i * 4 + 2] = std::byte{127};
            texels[i * 4 + 3] = std::byte{255};
        }

        invoke(178, {0xde1, 0, 0x8058, 2, 2, 0, 0x1908, 0x1401, 0}, texels);
        invoke(103, {0x8d40, framebuffer});
        invoke(138, {0x8d40, 0x8ce0, 0xde1, texture, 0});
        assert(invoke(111, {0x8d40}).result == 0x8cd5);
        auto texture_pixels = [&]() {
            return invoke(24, {0, 0, 2, 2, 0x1908, 0x1401, 0}).payload;
        };
        assert(texture_pixels() == texels);
        std::vector<std::byte> one_pixel{std::byte{255}, std::byte{0}, std::byte{0},
                                         std::byte{255}};
        invoke(184, {0xde1, 0, 0, 0, 1, 1, 0x1908, 0x1401, 0}, one_pixel);
        auto expected_texels = texels;
        std::copy(one_pixel.begin(), one_pixel.end(), expected_texels.begin());
        assert(texture_pixels() == expected_texels);

        for (auto [pointer, size] : std::initializer_list<std::pair<std::uint64_t, std::size_t>>{
                     {0, 3}, {0, 5}, {9, 4}, {0, 3841}}) {
            bool rejected = false;
            try {
                invoke(184, {0xde1, 0, 0, 0, 1, 1, 0x1908, 0x1401, pointer},
                       std::vector<std::byte>(size));
            } catch (const std::runtime_error &) {
                rejected = true;
            }

            assert(rejected);
            assert(texture_pixels() == expected_texels);
        }

        invoke(168, {0xcf5, 8});
        invoke(168, {0xcf2, 3});
        invoke(168, {0xcf3, 1});
        invoke(168, {0xcf4, 1});
        std::vector<std::byte> strided(44, std::byte{19});

        for (unsigned i = 0; i < 4; i++) {
            std::copy(texels.begin() + static_cast<std::ptrdiff_t>(i) * 4,
                      texels.begin() + static_cast<std::ptrdiff_t>(i) * 4 + 4,
                      strided.begin() + 20 + static_cast<std::ptrdiff_t>(i / 2) * 16 +
                              static_cast<std::ptrdiff_t>(i % 2) * 4);
        }

        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, 0}, strided);
        assert(texture_pixels() == texels);
        bool stride_rejected = false;
        try {
            invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, 0}, texels);
        } catch (const std::runtime_error &) {
            stride_rejected = true;
        }

        assert(stride_rejected && texture_pixels() == texels);
        invoke(168, {0xcf5, 4});
        std::vector<std::byte> rgb_strided(33, std::byte{19});

        for (unsigned i = 0; i < 4; i++) {
            for (unsigned c = 0; c < 3; c++) {
                rgb_strided[15 + (i / 2) * 12 + (i % 2) * 3 + c] = texels[i * 4 + c];
            }
        }

        invoke(178, {0xde1, 0, 0x8051, 2, 2, 0, 0x1907, 0x1401, 0});
        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1907, 0x1401, 0}, rgb_strided);
        assert(texture_pixels() == texels);
        assert(invoke(20).result == 0);
        const auto pbo = object_name(139);
        invoke(102, {0x88ec, pbo});
        bool pbo_rejected = false;
        try {
            invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, 0}, strided);
        } catch (const std::runtime_error &) {
            pbo_rejected = true;
        }

        assert(pbo_rejected);
        invoke(102, {0x88ec, 0});

        for (unsigned key : {0xcf2u, 0xcf3u, 0xcf4u}) {
            invoke(168, {key, 0});
        }

        invoke(178, {0xde1, 0, 0x8058, 2, 2, 0, 0x1908, 0x1401, 0});
        auto texture_blob = invoke(40, {16, 0}).result;
        {
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(32768);
            file.write(reinterpret_cast<const char *>(texels.data()), 16);
            assert(file);
        }
        std::array<std::uint64_t, 2> texture_run{ram_base + 32768, 16};
        std::vector<std::byte> texture_runs(16);
        std::memcpy(texture_runs.data(), texture_run.data(), 16);
        assert(invoke(41, {texture_blob, 1}, texture_runs).result);
        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, texture_blob});
        assert(texture_pixels() == texels);
        auto write_texture = [&](const std::vector<std::byte> &bytes) {
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(32768);
            file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            assert(file);
        };
        std::vector<std::byte> red_texels(16);

        for (unsigned i = 0; i < 4; i++) {
            std::copy(one_pixel.begin(), one_pixel.end(),
                      red_texels.begin() + static_cast<std::ptrdiff_t>(i) * 4);
        }

        write_texture(red_texels);
        assert(invoke(43, {texture_blob, 16}).result == 1);
        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, texture_blob});
        assert(texture_pixels() == red_texels);
        std::vector<std::byte> green_texel{std::byte{0}, std::byte{255}, std::byte{0},
                                           std::byte{255}};
        write_texture(green_texel);
        assert(invoke(43, {texture_blob, 4}).result == 1);
        bool stale_rejected = false;
        try {
            invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, texture_blob});
        } catch (const std::runtime_error &) {
            stale_rejected = true;
        }

        assert(stale_rejected && texture_pixels() == red_texels);
        invoke(184, {0xde1, 0, 0, 0, 1, 1, 0x1908, 0x1401, texture_blob});
        std::copy(green_texel.begin(), green_texel.end(), red_texels.begin());
        assert(texture_pixels() == red_texels);

        for (unsigned consumer : {109u, 353u, 354u}) {
            bool rejected = false;
            try {
                Arguments args{};

                if (consumer == 109) {
                    args = {0x8892, 4, texture_blob, 0x88e0};
                } else if (consumer == 353) {
                    args = {1, texture_blob, 16};
                } else {
                    args = {texture_blob, 16, 0};
                }

                invoke(consumer, args);
            } catch (const std::runtime_error &) {
                rejected = true;
            }

            assert(rejected);
        }

        for (auto [id, size] : std::initializer_list<std::pair<std::uint64_t, std::uint64_t>>{
                     {0, 4}, {texture_blob, 0}, {texture_blob, 17}, {99999, 4}}) {
            bool rejected = false;
            try {
                invoke(43, {id, size});
            } catch (const std::runtime_error &) {
                rejected = true;
            }

            assert(rejected);
        }

        auto refresh_output = invoke(40, {16, 1}).result;
        std::array<std::uint64_t, 2> refresh_run{ram_base + 32768, 16};
        std::vector<std::byte> refresh_runs(16);
        std::memcpy(refresh_runs.data(), refresh_run.data(), 16);
        invoke(41, {refresh_output, 1}, refresh_runs);
        bool direction_rejected = false;
        try {
            invoke(43, {refresh_output, 4});
        } catch (const std::runtime_error &) {
            direction_rejected = true;
        }

        assert(direction_rejected);
        invoke(42, {refresh_output});
        bool refresh_payload_rejected = false;
        try {
            invoke(43, {texture_blob, 4}, green_texel);
        } catch (const std::runtime_error &) {
            refresh_payload_rejected = true;
        }

        assert(refresh_payload_rejected);
        write_texture(texels);
        assert(invoke(43, {texture_blob, 16}).result == 1);
        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, texture_blob});
        assert(texture_pixels() == texels);
        invoke(42, {texture_blob});
        invoke(103, {0x8d40, 0});
        assert(invoke(20).result == 0);
        // Refresh a prefix spanning disjoint physical runs, then prove it with texture readback.
        const auto split_texture = object_name(142), split_framebuffer = object_name(140);
        invoke(105, {0xde1, split_texture});
        invoke(178, {0xde1, 0, 0x8058, 2, 2, 0, 0x1908, 0x1401, 0});
        invoke(103, {0x8d40, split_framebuffer});
        invoke(138, {0x8d40, 0x8ce0, 0xde1, split_texture, 0});
        std::vector<std::byte> split_pixels{
                std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
                std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
                std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
                std::byte{255}, std::byte{255}, std::byte{0},   std::byte{255}};
        auto write_split = [&]() {
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(36864);
            file.write(reinterpret_cast<const char *>(split_pixels.data()), 8);
            file.seekp(40960);
            file.write(reinterpret_cast<const char *>(split_pixels.data() + 8), 8);
            assert(file);
        };
        write_split();
        auto split_blob = invoke(40, {16, 0}).result;
        std::array<std::uint64_t, 4> split_runs{ram_base + 36864, 8, ram_base + 40960, 8};
        std::vector<std::byte> split_bytes(32);
        std::memcpy(split_bytes.data(), split_runs.data(), 32);
        invoke(41, {split_blob, 2}, split_bytes);
        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, split_blob});
        assert(texture_pixels() == split_pixels);
        std::swap_ranges(split_pixels.begin(), split_pixels.begin() + 8, split_pixels.begin() + 8);
        write_split();
        invoke(43, {split_blob, 12});
        invoke(184, {0xde1, 0, 0, 0, 2, 1, 0x1908, 0x1401, split_blob});
        auto partial = texture_pixels();
        assert(std::equal(split_pixels.begin(), split_pixels.begin() + 8, partial.begin()));
        // A valid 1x1 update uses only the first pixel; full refresh then updates all four.
        invoke(184, {0xde1, 0, 0, 0, 1, 1, 0x1908, 0x1401, split_blob});
        invoke(43, {split_blob, 16});
        invoke(184, {0xde1, 0, 0, 0, 2, 2, 0x1908, 0x1401, split_blob});
        assert(texture_pixels() == split_pixels);
        invoke(42, {split_blob});
        invoke(103, {0x8d40, 0});
        assert(invoke(20).result == 0);
        // A real ANGLE readback cannot be advertised as the original Screen.
        assert(!std::filesystem::exists(directory / "frame.bgra"));
        assert(!std::filesystem::exists(directory / "frame-owner.json"));
        // Publish a real mapped protocol page, then poll it without GL work.
        auto publish = [&](std::uint32_t state) {
            std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, state, 9999, 1, 0, 0, 0};
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(4096);
            file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
            file.flush();
            assert(file);
        };
        publish(0);
        const auto start = std::chrono::steady_clock::now();

        for (unsigned i = 0; i < 10000; ++i) {
            assert(!backend.service());
        }

        auto stats = backend.service_stats();
        assert(stats.polls == 10000 && stats.identity_checks < 1000);
        std::cout << "idle polls=" << stats.polls << " identity_checks=" << stats.identity_checks
                  << " elapsed_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count()
                  << '\n';
        publish(1);
        auto before = backend.service_stats().identity_checks;
        {
            const std::string secret = "never-log-payload";
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            std::uint32_t size = secret.size();
            file.seekp(4096 + 20);
            file.write(reinterpret_cast<const char *>(&size), 4);
            file.seekp(4096 + 256);
            file.write(secret.data(), secret.size());
            file.flush();
            CaptureLogs capture;
            assert(backend.service());
            const auto text = capture.output.str();
            assert(text.find(secret) == std::string::npos);
            const char *level = std::getenv("VOYEMU_LOG_LEVEL");

            if (level && std::string_view(level) == "debug") {
                auto fault = log_record(text, "rpc.dispatch_fault");
                assert(fault["fields"]["opcode"] == 9999 && fault["fields"]["category"] == "other");
            }
        }

        assert(backend.service_stats().identity_checks == before + 1);
        {
            std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, 1, 20, 2, 0, 0, 0};
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(4096);
            file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
            file.flush();
            assert(file);
        }
        assert(backend.service());
        {
            std::array<std::uint32_t, 8> header{rpc_magic, rpc_version, 1, 9999, 3, 0, 0, 0};
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(4096);
            file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
            file.flush();
            assert(file);
        }
        assert(backend.service());
        publish(0);
        assert(!backend.service());
        const auto profile = backend.rpc_profile();
        assert(profile.enabled && profile.first_received_ns &&
               profile.last_acknowledged_ns >= profile.first_received_ns);
        std::uint64_t recorded = 0;

        for (const auto &entry : profile.wire) {
            recorded += entry.calls;
            assert(entry.dispatch_ns >= entry.max_dispatch_ns);

            if (entry.opcode == 20) {
                assert(entry.calls == 1 && !entry.faults);
            } else {
                assert(entry.opcode == UINT32_MAX && entry.calls == 2 && entry.faults == 2);
            }
        }

        assert(recorded == 3 && profile.wire.size() == 2 && profile.unknown_api_calls == 2 &&
               !profile.api.empty());
        // Reused RAM with an incidental retirement word still fails strict header validation.
        auto fail_header = [&](std::array<std::uint32_t, 8> header) {
            {
                std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
                file.seekp(4096);
                file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
                file.flush();
                assert(file);
            }
            CaptureLogs capture;
            bool rejected = false;
            try {
                backend.service();
            } catch (const RpcChannelInvalidated &error) {
                rejected = std::string(error.what()) == "Published RPC page header";
            }

            assert(rejected && !backend.finished());
            auto text = capture.output.str();
            assert(text.size() < 2048);
            auto fields = log_record(text, "rpc.header_failed").at("fields");
            assert(fields["producer_address"] == options.address &&
                   fields["expected_magic"] == rpc_magic &&
                   fields["expected_version"] == rpc_version);
            assert(fields["actual_magic"] == header[0] && fields["actual_version"] == header[1] &&
                   fields["header_words"] == header);
            assert(fields["last_opcode"] == 20 && fields["last_sequence"] == 2 &&
                   fields["last_state"] == 0);
        };
        fail_header({0, 0, 0, 0, 0, 0, 0, 0});
        fail_header({rpc_magic ^ 1, rpc_version, 3, 0xabcdef, 77, 3841, 25, 1});
        fail_header({rpc_magic, rpc_version + 1, 1, 123, 78, 0, 0, 0});
        publish(0);
        assert(!backend.service());
#ifdef _WIN32
        // The mapped handle denies delete sharing, so the backing file cannot be replaced in place.
        // A same-size replacement reached through a path the OS still permits must be caught by the
        // re-opened identity guard rather than the mapped handle alone.
        auto backing_rejected = [&]() {
            bool rejected = false;
            try {
                backend.service();
            } catch (const std::runtime_error &e) {
                rejected = std::string(e.what()) == "GRAPHICS: RAM backing changed";
            }

            return rejected;
        };
        {
            std::uint32_t pending = 1;
            std::fstream file(ram, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(4096 + 8);
            file.write(reinterpret_cast<const char *>(&pending), sizeof(pending));
            file.flush();
            assert(file);
        }
        std::error_code replacement_error;
        std::filesystem::rename(ram, directory / "original-ram.bin", replacement_error);

        if (!replacement_error) {
            {
                std::ofstream replacement(ram, std::ios::binary);
                replacement.seekp(ram_bytes - 1);
                replacement.put(0);
            }
            assert(backing_rejected());
            std::filesystem::remove(ram);
            std::filesystem::rename(directory / "original-ram.bin", ram);
        } else {
            // Re-pointing the containing directory still bypasses the file's delete-sharing denial.
            const auto parked = std::filesystem::path(directory.string() + ".parked");
            std::error_code directory_error;
            std::filesystem::rename(directory, parked, directory_error);

            if (directory_error) {
                assert(directory_error);
            } else {
                std::filesystem::create_directory(directory);
                {
                    std::ofstream replacement(ram, std::ios::binary);
                    replacement.seekp(ram_bytes - 1);
                    replacement.put(0);
                }
                assert(backing_rejected());
                std::filesystem::remove_all(directory);
                std::filesystem::rename(parked, directory);
            }
        }
#else
        // Replacing the pathname cannot bypass the per-request inode guard.
        std::filesystem::rename(ram, directory / "original-ram.bin");
        {
            std::ofstream replacement(ram, std::ios::binary);
            replacement.seekp(ram_bytes - 1);
            replacement.put(0);
        }
        auto original = directory / "original-ram.bin";
        {
            std::uint32_t pending = 1;
            std::fstream file(original, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(4096 + 8);
            file.write(reinterpret_cast<const char *>(&pending), sizeof(pending));
            file.flush();
        }
        bool rejected = false;
        try {
            backend.service();
        } catch (const std::runtime_error &e) {
            rejected = std::string(e.what()) == "GRAPHICS: RAM backing changed";
        }

        assert(rejected);
        std::filesystem::remove(ram);
        std::filesystem::rename(original, ram);
        publish(0);
        assert(!backend.service());
        // Idle identity changes are also detected within the periodic check bound.
        std::filesystem::rename(ram, original);
        {
            std::ofstream replacement(ram, std::ios::binary);
            replacement.seekp(ram_bytes - 1);
            replacement.put(0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        rejected = false;
        try {
            backend.service();
        } catch (const std::runtime_error &e) {
            rejected = std::string(e.what()) == "GRAPHICS: RAM backing changed";
        }

        assert(rejected);
#endif
        std::cout
                << "native real ANGLE readback and mapped polling guards passed; no OEM frame claimed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
