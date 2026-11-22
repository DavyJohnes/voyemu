#include "session.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <thread>
using namespace voyemu;

static void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv) try {
    if (argc != 3) {
        return 2;
    }

    auto cli = std::filesystem::canonical(argv[1]), fixture = std::filesystem::canonical(argv[2]);
    auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("voyemu-qnx-lifecycle-" + random_token());
    ensure_private_directory(root);
    try {
        for (const char *value : {"0", "3601", "-1", "1x", "4294967297"}) {
            ProcessSpec command;
            command.executable = cli;
            command.arguments = {"help", "--seconds", value};
            auto result = execute(command, 1000);
            require(result.exit_code != 0 && result.output.find("QNX_BOUND") != std::string::npos,
                    "explicit duration accepted an invalid CLI value");
        }

        auto spec = Json{
                {"schema", 1},
                {"pins",
                 Json::array({{{"path", fixture.string()}, {"sha256", sha256_file(fixture)}}})},
                {"ram", "${SESSION}/qnx-ram.bin"},
                {"qmp", "${RUNTIME}/${TOKEN}-qmp.sock"},
                {"qemu",
                 {{"executable", fixture.string()},
                  {"args", Json::array({"--runtime-path-fixture", "${RUNTIME}/${TOKEN}-qmp.sock"})},
                  {"env",
                   {{"VOYEMU_NATIVE_FIXTURE_ROOT", root.string()},
                    {"VOYEMU_FIXTURE_RUNTIME_PATH", "${RUNTIME}/${TOKEN}-qmp.sock"},
                    {"TMPDIR", (root / "foreign-project-tmp").string()}}}}}};

        for (bool bounded : {false, true}) {
            auto id = random_token();
            auto directory = root / ".local/machines/test-profile/sessions" / id;
            Session session(directory, id);

            if (bounded) {
                session.attach_cluster(spec, 1);
            } else {
                session.attach_cluster(spec);
            }

            auto status = [&] {
                return session.handle({{"op", "status"}, {"session_id", id}});
            };
            auto initial = status();
            require(initial["deliveries"]["qnx"]["bounded"] == bounded, "wrong deadline policy");
            require(initial["deliveries"]["qnx"]["bounded_seconds"] == (bounded ? 1 : 0),
                    "duration status changed");
            // QMP is deliberately absent. A transient observation error must not become lifetime
            // expiry.
            std::this_thread::sleep_for(std::chrono::milliseconds(1150));
            session.tick();
            auto after = status();

            if (bounded) {
                require(after["deliveries"]["qnx"]["status"] == "bounded_run_complete",
                        "explicit deadline did not stop its own QNX child");
            } else {
                require(after["deliveries"]["qnx"]["status"] == "booting" &&
                                after["qnx_pid"].get<int>() > 1,
                        "normal QNX closed on an absent deadline");
            }

            require(read_private_json(root / "runtime-path-temp.json")["TMPDIR"] ==
                            (directory / "host-tmp").string(),
                    "QNX inherited a foreign project temporary directory");
            session.stop();
            auto terminal = read_private_json(directory / "runtime.json");
            require(!terminal["deliveries"]["qnx"]["cleanup"].contains("error"),
                    "owned terminal cleanup changed");
        }

        {
            auto id = random_token();
            auto directory = root / ".local/machines/test-profile/sessions" / id;
            Session cycle(directory, id);
            cycle.configure_cluster(spec);
            const auto discovery = directory / "qnx-frame-grpc.json";
            const auto frame_token = [&](const Json &status, unsigned generation) {
                const auto connection = read_private_json(discovery, 4096);
                const auto &advertised = status.at("deliveries").at("qnx").at("frame_grpc");
                const auto token = connection.at("token").get<std::string>();
                require(connection.at("schema") == 1 && connection.at("session_id") == id &&
                                connection.at("generation") == generation,
                        "QNX frame endpoint has incorrect session or generation");
                require(connection.at("host") == "127.0.0.1" &&
                                connection.at("port").get<unsigned>() > 0,
                        "QNX frame endpoint is not bound to loopback");
                require(advertised.at("status") == "listening" &&
                                advertised.at("host") == connection.at("host") &&
                                advertised.at("port") == connection.at("port") &&
                                advertised.at("discovery") == discovery.string(),
                        "QNX frame endpoint discovery is missing from status");
                require(token.size() == 32 && token != id &&
                                status.dump().find(token) == std::string::npos,
                        "QNX frame endpoint leaked credentials in status");
                require(std::filesystem::status(discovery).permissions() ==
                                (std::filesystem::perms::owner_read |
                                 std::filesystem::perms::owner_write),
                        "QNX frame discovery permissions are not private");

                return token;
            };
            auto configured = cycle.handle({{"op", "status"}, {"session_id", id}});
            require(configured["qnx_pid"] == 0 &&
                            configured["deliveries"]["qnx"]["status"] == "off",
                    "configuration started a QNX process");
            require(!std::filesystem::exists(discovery), "disabled QNX started a frame endpoint");
            auto first =
                    cycle.handle({{"op", "cluster_power"}, {"session_id", id}, {"enabled", true}});
            require(first["ok"] == true, "configured disabled cluster did not start");
            require(first["deliveries"]["qnx"]["quickboot"]["reason"] == "explicit_cold_boot",
                    "manual initial power on did not request cold boot");
            const auto first_token = frame_token(first, 1);
            auto requested = cycle.handle(
                    {{"op", "patch"}, {"session_id", id}, {"values", {{"turn_left", 1}}}});
            require(requested["ok"] == true, "logical turn request rejected");
            cycle.tick_inputs();
            auto lit = read_private_json(directory / "mcu-state.json");
            auto logical = read_private_json(directory / "vehicle-state.json");
            const auto stamp = std::filesystem::last_write_time(directory / "vehicle-state.json");
            require(lit["raw_oem"]["turn_left"] == 1,
                    "MCU first phase must be lit before guest readiness");
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            cycle.tick_inputs();
            auto dark = read_private_json(directory / "mcu-state.json");
            require(dark["raw_oem"]["turn_left"] == 0 &&
                            dark["sequence"].get<unsigned>() > lit["sequence"].get<unsigned>(),
                    "MCU phase must advance even without wheel events or ready guest");
            require(read_private_json(directory / "vehicle-state.json") == logical &&
                            std::filesystem::last_write_time(directory / "vehicle-state.json") ==
                                    stamp,
                    "phase must not rewrite canonical requested state");
            auto after_phase = cycle.handle({{"op", "status"}, {"session_id", id}});
            require(after_phase["revision"] == requested["revision"] &&
                            after_phase["state"]["raw_oem"]["turn_left"] == 1,
                    "phase must not change Android source revision or checkbox");
            { std::ofstream(directory / "frame.bgra") << "preserved old frame"; }
            auto off =
                    cycle.handle({{"op", "cluster_power"}, {"session_id", id}, {"enabled", false}});
            require(off["ok"] == true && off["deliveries"]["qnx"]["status"] == "off",
                    "power off failed");
            require(!std::filesystem::exists(discovery) &&
                            !off["deliveries"]["qnx"].contains("frame_grpc"),
                    "powered-off QNX retained frame endpoint discovery");
            auto on =
                    cycle.handle({{"op", "cluster_power"}, {"session_id", id}, {"enabled", true}});
            require(on["ok"] == true && on["deliveries"]["qnx"]["generation"] == 2,
                    "power on did not create a fresh generation");
            const auto second_token = frame_token(on, 2);
            require(first_token != second_token, "powered-on QNX reused old frame credentials");
            require(std::filesystem::exists(directory / "cluster-history/1/frame.bgra") &&
                            !std::filesystem::exists(directory / "frame.bgra"),
                    "old frame was not preserved away from the new producer");
            auto restarted = cycle.handle({{"op", "cluster_restart"}, {"session_id", id}});
            require(restarted["ok"] == true && restarted["deliveries"]["qnx"]["generation"] == 3,
                    "restart reused stale generation");
            // Closing the cluster ends the startup sequence of its cold start: the READY power
            // state held at ACC returns with ign, ready_lamp and the charging battery voltage, the
            // reset keeps it, and no other value of the scenario changes.
            auto scenario = logical;
            scenario["raw_oem"]["ign"] = 1;
            scenario["raw_oem"]["ready_lamp"] = 1;
            scenario["raw_oem"]["ic_bat_voltage"] = 1380;
            scenario["sequence"] = restarted["state"]["sequence"];
            scenario["updated_utc"] = restarted["state"]["updated_utc"];
            require(restarted["deliveries"]["startup_ignition"] ==
                                    Json({{"ign", 1},
                                          {"pending", false},
                                          {"reason", "cluster_closed"},
                                          {"revision", restarted["state"]["sequence"]}}) &&
                            restarted["state"] == scenario &&
                            read_private_json(directory / "vehicle-state.json") == scenario,
                    "manual QNX restart changed current vehicle scenario");
            const auto third_token = frame_token(restarted, 3);
            require(third_token != second_token && third_token != first_token,
                    "QNX restart reused old frame credentials");
            require(!std::filesystem::exists(directory / "cluster-history/1" /
                                             discovery.filename()) &&
                            !std::filesystem::exists(directory / "cluster-history/2" /
                                                     discovery.filename()),
                    "QNX generation history retained frame credentials");
            cycle.stop();
            require(!std::filesystem::exists(discovery),
                    "stopped QNX retained frame endpoint discovery");
        }
        {
            const auto cache = root / "checkpoint-cache";
            ensure_private_directory(cache / "gpu");
            auto output = [&](const std::filesystem::path &path, const char *data,
                              std::size_t bytes) {
                std::ofstream file(path, std::ios::binary);
                file.write(data, bytes);
                file.close();
                std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                                           std::filesystem::perms::owner_write);
            };
            const char vm[] = {0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3};
            output(cache / "vmstate.bin", vm, 8);
            const std::array<std::uint64_t, 3> gpu{0x3130305550475156ULL, 0x40001000ULL,
                                                   8ULL * 1024 * 1024 * 1024};
            output(cache / "gpu/40001000.gpu", reinterpret_cast<const char *>(gpu.data()), 24);
            auto cached = spec;
            cached["qemu"]["args"] =
                    Json::array({"--qnx-checkpoint-fixture", "${SESSION}/qnx-ram.bin"});
            cached["graphics"] = {{"executable", fixture.string()}, {"angle", root.string()}};
            cached["quickboot"] = {{"directory", cache.string()}};
#ifdef _WIN32
            const std::array<const char *, 2> libraries{"libEGL.dll", "libGLESv2.dll"};
#else
            const std::array<const char *, 2> libraries{"libEGL.dylib", "libGLESv2.dylib"};
#endif

            for (auto name : libraries) {
                const auto path = root / name;
                output(path, "fixture-angle", 13);
                cached["pins"].push_back({{"path", path.string()}, {"sha256", sha256_file(path)}});
            }

            auto pin = [&](const std::filesystem::path &path) {
                return Json{{"bytes", std::filesystem::file_size(path)},
                            {"sha256", sha256_file(path)}};
            };
            write_private_json(cache / "manifest.json",
                               {{"schema", 1},
                                {"state", "ready"},
                                {"key", qnx_checkpoint_key(cached, "test-profile")},
                                {"state_sequence", 10},
                                {"theme_sequence", 1},
                                {"wheel_sequence", 0},
                                {"held_keys", 0},
                                {"producers", Json::array({0x40001000ULL})},
                                {"vm", pin(cache / "vmstate.bin")},
                                {"gpu", {{"40001000.gpu", pin(cache / "gpu/40001000.gpu")}}}});
            const auto manifest_hash = sha256_file(cache / "manifest.json"),
                       vm_hash = sha256_file(cache / "vmstate.bin"),
                       gpu_hash = sha256_file(cache / "gpu/40001000.gpu");
            require(inspect_qnx_checkpoint(cached, "test-profile").resume,
                    "manual lifecycle test checkpoint is invalid");
            auto id = random_token();
            auto directory = root / ".local/machines/test-profile/sessions" / id;
            Session cycle(directory, id);
            cycle.attach_cluster(cached);
            auto launches = [&](std::size_t count) {
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(1500);

                while (std::chrono::steady_clock::now() < deadline) {
                    if (std::filesystem::exists(root / "qnx-launches.json") &&
                        std::filesystem::exists(directory / "qnx-ram.bin") &&
                        std::filesystem::file_size(directory / "qnx-ram.bin") ==
                                8ULL * 1024 * 1024 * 1024) {
                        auto value = read_private_json(root / "qnx-launches.json");

                        if (value.size() == count) {
                            return value;
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                throw std::runtime_error(
                        "manual lifecycle fixture did not record its owned launch");
            };
            auto warm = cycle.handle({{"op", "status"}, {"session_id", id}});
            require(warm["deliveries"]["qnx"]["quickboot"]["mode"] == "restore_requested" &&
                            warm["deliveries"]["qnx"]["quickboot"]["reason"] ==
                                    "validated_checkpoint",
                    "default start stopped selecting the valid checkpoint");
            require(launches(1)[0]["incoming"] == true,
                    "default start omitted checkpoint restore arguments");
            auto restarted = cycle.handle({{"op", "cluster_restart"}, {"session_id", id}});
            require(restarted["ok"] == true && restarted["deliveries"]["qnx"]["generation"] == 2 &&
                            restarted["deliveries"]["qnx"]["quickboot"]["mode"] == "cold" &&
                            restarted["deliveries"]["qnx"]["quickboot"]["reason"] ==
                                    "explicit_cold_boot",
                    "manual restart restored the valid checkpoint");
            require(launches(2)[1]["incoming"] == false,
                    "manual restart retained checkpoint restore arguments");
            auto off =
                    cycle.handle({{"op", "cluster_power"}, {"session_id", id}, {"enabled", false}});
            require(off["ok"] == true && off["deliveries"]["qnx"]["status"] == "off",
                    "cached cluster power off failed");
            auto on =
                    cycle.handle({{"op", "cluster_power"}, {"session_id", id}, {"enabled", true}});
            require(on["ok"] == true && on["deliveries"]["qnx"]["generation"] == 3 &&
                            on["deliveries"]["qnx"]["quickboot"]["mode"] == "cold" &&
                            on["deliveries"]["qnx"]["quickboot"]["reason"] == "explicit_cold_boot",
                    "manual power cycle restored the valid checkpoint");
            require(launches(3)[2]["incoming"] == false,
                    "manual power cycle retained checkpoint restore arguments");
            auto repeated =
                    cycle.handle({{"op", "cluster_power"}, {"session_id", id}, {"enabled", true}});
            require(repeated["ok"] == true && repeated["deliveries"]["qnx"]["generation"] == 3,
                    "idempotent power on restarted the current generation");
            cycle.stop();
            require(!std::filesystem::exists(cache / "rejected.json") &&
                            inspect_qnx_checkpoint(cached, "test-profile").resume,
                    "manual cold boot quarantined the valid checkpoint");
            require(sha256_file(cache / "manifest.json") == manifest_hash &&
                            sha256_file(cache / "vmstate.bin") == vm_hash &&
                            sha256_file(cache / "gpu/40001000.gpu") == gpu_hash,
                    "manual cold boot rewrote the valid checkpoint");
        }
#ifdef __APPLE__
        {
            const auto cache = root / "prelaunch-cache";
            ensure_private_directory(cache);
            ensure_private_directory(cache / "gpu");
            const auto blocked = root / "non-executable-qemu";
            std::filesystem::copy_file(fixture, blocked);
            std::filesystem::permissions(blocked, std::filesystem::perms::owner_read |
                                                          std::filesystem::perms::owner_write);
            const auto ram = cache / "ram.bin";
            { std::ofstream(ram) << "fixture-RAM"; }
            std::filesystem::resize_file(ram, 8ULL * 1024 * 1024 * 1024);
            {
                std::ofstream f(cache / "vmstate.bin", std::ios::binary);
                const char bytes[] = {0x51, 0x45, 0x56, 0x4d, 0, 0, 0, 3};
                f.write(bytes, 8);
            }
            {
                std::ofstream f(cache / "gpu/40001000.gpu", std::ios::binary);
                const std::array<std::uint64_t, 3> words{0x3130305550475156ULL, 0x40001000ULL,
                                                         8ULL * 1024 * 1024 * 1024};
                f.write(reinterpret_cast<const char *>(words.data()), 24);
            }

            for (const auto &path : {ram, cache / "vmstate.bin", cache / "gpu/40001000.gpu"}) {
                std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                                           std::filesystem::perms::owner_write);
            }

            auto failed = spec;
            failed["guest_arch"] = "aarch64";
            failed["qemu"]["executable"] = blocked.string();
            failed["pins"].push_back(
                    {{"path", blocked.string()}, {"sha256", sha256_file(blocked)}});
            failed["graphics"] = {{"executable", fixture.string()}, {"angle", root.string()}};
            failed["quickboot"] = {{"directory", cache.string()}};

            for (const auto *name : {"libEGL.dylib", "libGLESv2.dylib"}) {
                failed["pins"].push_back(
                        {{"path", (root / name).string()}, {"sha256", sha256_file(root / name)}});
            }

            failed["qemu"]["args"] = Json::array(
                    {"-machine", "virt-11.0,memory-backend=qnxmem", "-m", "8192", "-object",
                     "memory-backend-file,id=qnxmem,size=8192M,mem-path=${SESSION}/qnx-ram.bin,share=on",
                     "-S"});
            auto pin = [](const auto &path) {
                return Json{{"bytes", std::filesystem::file_size(path)},
                            {"sha256", sha256_file(path)}};
            };
            write_private_json(
                    cache / "manifest.json",
                    {{"schema", 1},
                     {"state", "ready"},
                     {"key", qnx_checkpoint_key(failed, "test-profile")},
                     {"state_sequence", 1},
                     {"theme_sequence", 0},
                     {"wheel_sequence", 0},
                     {"held_keys", 0},
                     {"producers", Json::array({0x40001000ULL})},
                     {"vm", pin(cache / "vmstate.bin")},
                     {"gpu", {{"40001000.gpu", pin(cache / "gpu/40001000.gpu")}}},
                     {"memory",
                      {{"format", "shared-file-cpr-v1"},
                       {"ram", pin(ram)},
                       {"boot_roms", Json::array({{{"address", 0x40000000ULL}, {"bytes", 8}}})}}}});
            auto saved = inspect_qnx_checkpoint(failed, "test-profile");
            require(saved.resume && saved.external_ram &&
                            remember_qnx_checkpoint_verification(saved),
                    "prelaunch checkpoint fixture did not verify");
            const auto id = random_token();
            const auto directory = root / ".local/machines/test-profile/sessions" / id;
            Session no_child(directory, id);
            bool rejected = false;
            try {
                no_child.attach_cluster(failed, 1);
            } catch (const std::exception &) {
                rejected = true;
            }

            require(rejected, "non-executable QEMU unexpectedly started");
            const auto status = no_child.handle({{"op", "status"}});
            require(status["qnx_pid"] == 0 &&
                            status["deliveries"]["qnx"]["cleanup"]["ram_removed"] == true &&
                            !std::filesystem::exists(directory / "qnx-ram.bin"),
                    "failed process launch leaked its RAM clone");
            const auto evidence = std::filesystem::path(
                    status["deliveries"]["qnx"]["cleanup"]["handoff"].get<std::string>());
            require(std::filesystem::is_regular_file(evidence) && std::filesystem::exists(ram),
                    "failed launch deleted source or omitted durable handoff");
            no_child.stop();
        }
#endif
        auto id = random_token();
        Session guarded(root / ".local/machines/test-profile/sessions" / id, id);

        for (unsigned invalid : {0u, 3601u}) {
            bool rejected = false;
            try {
                guarded.attach_cluster(spec, invalid);
            } catch (const std::exception &) {
                rejected = true;
            }

            require(rejected, "Session accepted invalid explicit duration");
        }

        guarded.stop();
        std::filesystem::remove_all(root);
        std::cout
                << "normal QNX has no deadline; explicit deadline and CLI/Session bounds retained\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "; fixture retained at " << root << '\n';

        return 1;
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
} catch (...) {
    std::cerr << "unexpected non-standard exception\n";

    return 1;
}
