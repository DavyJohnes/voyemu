#include "qnx_capture.hpp"
#include "process.hpp"
#include "qnx_checkpoint.hpp"
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

int main(int argc, char **argv) try {
    using namespace voyemu;
    auto root = std::filesystem::temp_directory_path() / "owned-capture-fixture";
    auto session = root / ".local/machines/test-profile/sessions/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    auto output = root / ".local/build/tmp/result";
    assert(qnx_capture_project(session, output) == root);
    auto rejects = [](const std::function<void()> &action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception &) {
            failed = true;
        }

        assert(failed);
    };
    rejects([&] {
        qnx_capture_project(session, root / "outside");
    });
    rejects([&] {
        qnx_capture_project(session, root / ".local/build/tmp");
    });
    rejects([&] {
        qnx_capture_project(session.parent_path() / "../foreign", output);
    });
    rejects([&] {
        qnx_capture_project(root / "other/.local/machines/test-profile/sessions/not-a-token",
                            output);
    });
    qnx_capture_journal(root, "test-profile",
                        root / ".local/machines/test-profile/checkpoints/fresh-candidate/gpu");
    rejects([&] {
        qnx_capture_journal(root, "test-profile",
                            root / ".local/machines/test-profile/checkpoints");
    });
    rejects([&] {
        qnx_capture_journal(
                root, "test-profile",
                root / ".local/machines/test-profile/checkpoints/other-profile/fresh-candidate/gpu");
    });
    rejects([&] {
        qnx_capture_journal(root, "test-profile",
                            root / ".local/machines/test-profile/checkpoints/../gpu");
    });
    rejects([&] {
        qnx_capture_journal(root, "other-profile",
                            root / ".local/machines/test-profile/checkpoints/fresh-candidate/gpu");
    });
    rejects([&] {
        qnx_capture_project(
                root / ".local/machines/.hidden/sessions/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", output);
    });
    auto seed = qnx_capture_initial_state(
            Json{{"initial_state", {{"gear", 0}, {"speed", 0}, {"aeb_sts", 1}}}});
    assert(seed["raw_oem"]["aeb_sts"] == 1 && seed["raw_oem"]["soc"] == 55 &&
           seed["raw_oem"]["speed10x"] == 0);
    rejects([&] {
        qnx_capture_initial_state(Json{{"initial_state", {{"speed", 1}}}});
    });
    rejects([&] {
        qnx_capture_initial_state(Json{{"initial_state", {{"gear", 3}}}});
    });
    frames::Frame frame;
    frame.bgra.assign(frames::pixel_bytes, std::byte{0});
    auto black = qnx_capture_scene_geometry(frame);
    assert(black[0] == 0 && black[1] == 0 && black[2] == 0);

    for (unsigned y = 190; y < 650; y++) {
        for (unsigned x = 100; x < 700; x++) {
            for (unsigned c = 0; c < 3; c++) {
                frame.bgra[(y * 1920 + x) * 4 + c] = std::byte{255};
            }
        }
    }

    auto partial = qnx_capture_scene_geometry(frame);
    assert(partial[0] == 1 && partial[1] == 0 && partial[2] == 0);
    frame.bgra.assign(frames::pixel_bytes, std::byte{255});
    auto full = qnx_capture_scene_geometry(frame);
    assert(full[0] == 1 && full[1] == 1 && full[2] == 1);
    frame.bgra.pop_back();
    rejects([&] {
        qnx_capture_scene_geometry(frame);
    });

    if (argc == 2) {
        const auto directory = std::filesystem::temp_directory_path() /
                               ("qnx-capture-signal-" + random_token());
        ensure_private_directory(directory);
        const auto output = directory / "stdout.log";
        ProcessSpec spec;
        spec.executable = argv[1];
        spec.arguments = {"--calibrate"};
        spec.output = output;
        auto child = ChildProcess::launch(spec);
        bool started = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (child.alive() && std::chrono::steady_clock::now() < deadline) {
            std::ifstream file(output);
            std::string text{std::istreambuf_iterator<char>(file), {}};

            if (text.find("calibration_started") != std::string::npos) {
                started = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        assert(started);
        child.terminate(1000);
        assert(!child.alive() && child.exit_code() == 130);
        const auto cache = directory / "test-profile/cache";
        ensure_private_directory(cache);
        ensure_private_directory(cache / "gpu");
        {
            std::ofstream f(cache / "vmstate.bin", std::ios::binary);
            const char bytes[] = {0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3};
            f.write(bytes, 8);
        }
        {
            std::ofstream f(cache / "gpu/40001000.gpu", std::ios::binary);
            std::array<std::uint64_t, 3> header{0x3130305550475156ULL, 0x40001000ULL,
                                                8ULL * 1024 * 1024 * 1024};
            f.write(reinterpret_cast<const char *>(header.data()), 24);
        }

        for (const auto &p : {cache / "vmstate.bin", cache / "gpu/40001000.gpu"}) {
            std::filesystem::permissions(p, std::filesystem::perms::owner_read |
                                                    std::filesystem::perms::owner_write);
        }

        auto cold = Json{{"graphics", {{"executable", "/worker"}}},
                         {"qemu", {{"args", Json::array({"-S"})}}},
                         {"pins", Json::array()},
                         {"quickboot", {{"directory", cache.string()}}}};
        auto pin = [](const auto &p) {
            return Json{{"bytes", std::filesystem::file_size(p)}, {"sha256", sha256_file(p)}};
        };
        write_private_json(cache / "manifest.json",
                           {{"schema", 1},
                            {"state", "ready"},
                            {"key", qnx_checkpoint_key(cold, "test-profile")},
                            {"state_sequence", 1},
                            {"theme_sequence", 0},
                            {"wheel_sequence", 0},
                            {"held_keys", 0},
                            {"producers", Json::array({0x40001000ULL})},
                            {"vm", pin(cache / "vmstate.bin")},
                            {"gpu", {{"40001000.gpu", pin(cache / "gpu/40001000.gpu")}}}});
        const auto verify_path = directory / "verify-spec.json";
        write_private_json(verify_path, cold);
        ProcessSpec command;
        command.executable = argv[1];
        command.arguments = {"--verify", verify_path.string()};
        auto result = execute(command, 5000);
        assert(result.exit_code == 0 &&
               Json::parse(result.output).at("phase") == "checkpoint_verified" &&
               std::filesystem::exists(cache / "verified-files.json"));
        assert(!std::filesystem::exists(directory / "qnx-ram.bin"));
        const auto roms = directory / "boot-roms.txt";
        {
            std::ofstream f(roms);
            f << "addr=0000000040000000 size=0x8 mem=ram name=\"dtb\"\n";
        }
        command.arguments = {"--roms", verify_path.string(), roms.string()};
        result = execute(command, 5000);
        assert(result.exit_code == 0 &&
               Json::parse(result.output) ==
                       Json::array({{{"address", 0x40000000ULL}, {"bytes", 8}}}));
        std::filesystem::remove_all(directory);
    }

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
} catch (...) {
    std::cerr << "unexpected non-standard exception\n";

    return 1;
}
