#include "session.hpp"
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
using namespace voyemu;

int main(int argc, char **argv) try {
    if (argc != 2) {
        return 2;
    }

    const auto fixture = std::filesystem::canonical(argv[1]);
    const auto root = std::filesystem::temp_directory_path() /
                      ("voyemu-qnx-fallback-" + random_token());
    ensure_private_directory(root);
    const auto cache = root / "cache";
    ensure_private_directory(cache / "gpu");
    auto output = [&](const std::filesystem::path &p, const char *data, std::size_t bytes) {
        std::ofstream f(p, std::ios::binary);
        f.write(data, bytes);
        f.close();
        std::filesystem::permissions(p, std::filesystem::perms::owner_read |
                                                std::filesystem::perms::owner_write);
    };

    for (const char *name : {"libEGL.dylib", "libGLESv2.dylib"}) {
        output(root / name, "fixture", 7);
    }

    auto spec =
            Json{{"schema", 1},
                 {"ram", "${SESSION}/qnx-ram.bin"},
                 {"qmp", "${RUNTIME}/${TOKEN}-qmp.sock"},
                 {"qemu",
                  {{"executable", fixture.string()},
                   {"args", Json::array({"--qnx-checkpoint-fixture", "${SESSION}/qnx-ram.bin"})},
                   {"env", {{"VOYEMU_NATIVE_FIXTURE_ROOT", root.string()}}}}},
                 {"graphics", {{"executable", fixture.string()}, {"angle", root.string()}}},
                 {"pins", Json::array()},
                 {"quickboot", {{"directory", cache.string()}}}};

    for (const auto &p : {fixture, root / "libEGL.dylib", root / "libGLESv2.dylib"}) {
        spec["pins"].push_back({{"path", p.string()}, {"sha256", sha256_file(p)}});
    }

    const char vm[] = {0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3};
    output(cache / "vmstate.bin", vm, 8);
    const std::array<std::uint64_t, 3> gpu{0x3130305550475156ULL, 0x40001000ULL,
                                           8ULL * 1024 * 1024 * 1024};
    output(cache / "gpu/40001000.gpu", reinterpret_cast<const char *>(gpu.data()), 24);
    auto pin = [&](const std::filesystem::path &p) {
        return Json{{"bytes", std::filesystem::file_size(p)}, {"sha256", sha256_file(p)}};
    };
    write_private_json(cache / "manifest.json",
                       {{"schema", 1},
                        {"state", "ready"},
                        {"key", qnx_checkpoint_key(spec, "car-a")},
                        {"state_sequence", 10},
                        {"theme_sequence", 1},
                        {"wheel_sequence", 0},
                        {"held_keys", 0},
                        {"producers", Json::array({0x40001000ULL})},
                        {"vm", pin(cache / "vmstate.bin")},
                        {"gpu", {{"40001000.gpu", pin(cache / "gpu/40001000.gpu")}}}});
    auto id = random_token();
    const auto directory = root / ".local/machines/car-a/sessions" / id;
    Session session(directory, id);
    session.attach_cluster(spec, 5);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    Json state;

    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        session.tick();
        state = session.handle({{"op", "status"}, {"session_id", id}});

        if (state["deliveries"]["qnx"]["generation"] == 2) {
            break;
        }
    }

    assert(state["deliveries"]["qnx"]["generation"] == 2);
    assert(state["deliveries"]["qnx"]["quickboot"]["mode"] == "cold");
    assert(state["deliveries"]["qnx"]["quickboot"]["reason"] == "restore_failed");

    while (std::chrono::steady_clock::now() < deadline &&
           read_private_json(root / "qnx-launches.json").size() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto launches = read_private_json(root / "qnx-launches.json");
    assert(launches.size() == 2 && launches[0]["incoming"] == true &&
           launches[1]["incoming"] == false);
    assert(inspect_qnx_checkpoint(spec, "car-a").reason == "checkpoint_quarantined");
    assert(!std::filesystem::exists(directory / "cluster-history/1/qnx-ram.bin"));
    session.stop();
    assert(!std::filesystem::exists(directory / "qnx-ram.bin"));
    assert(read_private_json(directory /
                             "runtime.json")["deliveries"]["qnx"]["cleanup"]["ram_removed"] ==
           true);
    // Retain the tiny host-test handoffs; both sparse RAM files were removed by owned cleanup.
    write_private_json(root / "test-result.json",
                       {{"passed", true}, {"scope", "host fixture; no QEMU or guest frame"}});
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
} catch (...) {
    std::cerr << "unexpected non-standard exception\n";

    return 1;
}
