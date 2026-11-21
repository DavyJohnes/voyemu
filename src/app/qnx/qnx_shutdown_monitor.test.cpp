#include "qnx_shutdown_monitor.hpp"
#include "os/platform.hpp"
#include "os/process.hpp"
#include "process.hpp"
#include "shutdown_fixture.hpp"
#include <fstream>
#include <iostream>
#include <thread>
using namespace voyemu;

static void check(bool yes, const char *why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

int main(int argc, char **argv) {
    try {
        if (argc >= 8 && std::string_view(argv[1]) == "--qnx-shutdown-fixture") {
            return shutdown_fixture::run(argc, argv);
        }

        // The short /tmp keeps the POSIX Unix socket paths within their limit.
        const auto root = std::filesystem::canonical(
                                  voyemu::os::host_windows ? std::filesystem::temp_directory_path()
                                                           : std::filesystem::path("/tmp")) /
                          ("qsm-" + random_token().substr(0, 12));
        ensure_private_directory(root);
        const auto wait = [](const auto &predicate) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);

            while (!predicate() && std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            check(predicate(), "fixture wait expired");
        };

        if (voyemu::os::host_windows) {
            // The durable Windows channel is the owned loopback TCP endpoint; peer-pid and
            // socket-name ownership scenarios are unix-only and stay on the macOS branch.
            for (const char *scenario :
                 {"guest-reset", "fragmented", "false-reset", "guest-shutdown", "host-signal",
                  "host-qmp-quit", "none", "malformed", "truncated", "conflict", "version",
                  "capabilities"}) {
                const auto state = root / scenario;
                ensure_private_directory(state);
                ProcessSpec process;
                process.executable = std::filesystem::canonical(argv[0]);
                process.arguments = {
                        "--qnx-shutdown-fixture", scenario, state.string(), "-", "-", "0", "-"};
                process.output = state / "fixture.log";
                auto child = ChildProcess::launch(process);
                wait([&] {
                    return std::filesystem::exists(state / "ready");
                });
                unsigned port = 0;
                {
                    std::ifstream ready(state / "ready");
                    ready >> port;
                }
                check(port >= 1024 && port <= 65535, "fixture did not publish its owned port");
                auto monitor = std::make_unique<QnxShutdownMonitor>(
                        Json{{"host", "127.0.0.1"}, {"port", port}}, 7);
                const bool invalid = std::string_view(scenario) == "version" ||
                                     std::string_view(scenario) == "capabilities";
                bool rejected = false;
                try {
                    monitor->connect_owned(child.pid());
                } catch (...) {
                    rejected = true;
                }

                check(rejected == invalid, "subscription identity/capability policy differs");

                if (invalid) {
                    child.terminate(1000);
                    monitor->finish_owned_exit(child.pid(), 7);
                    continue;
                }

                {
                    std::ofstream release(state / "release-1");
                    release << "release";
                }
                wait([&] {
                    monitor->drain();

                    return !child.alive();
                });
                monitor->drain();
                const bool reset = std::string_view(scenario) == "guest-reset" ||
                                   std::string_view(scenario) == "fragmented";
                check(monitor->guest_reset(child.pid(), 7) == reset,
                      "shutdown cause classification differs");
                check(!monitor->guest_reset(child.pid(), 8) && !monitor->guest_reset(1, 7),
                      "stale generation/peer accepted");
                monitor->finish_owned_exit(child.pid(), 7);
            }

            std::filesystem::remove_all(root);
            std::cout
                    << "owned TCP QMP shutdown causes, fragmented/EOF drain and stale generations passed\n";
        } else {
            for (const char *scenario :
                 {"guest-reset", "fragmented", "false-reset", "guest-shutdown", "host-signal",
                  "host-qmp-quit", "none", "malformed", "truncated", "conflict", "version",
                  "capabilities", "swapped", "peer"}) {
                const auto state = root / scenario;
                ensure_private_directory(state);
                const auto socket = state / "events.sock";
                QnxShutdownMonitor monitor(Json(socket.string()), 7);
                ProcessSpec process;
                process.executable = std::filesystem::canonical(argv[0]);
                process.arguments = {"--qnx-shutdown-fixture", scenario, state.string(), "-", "-",
                                     socket.string(),          "-"};
                process.output = state / "fixture.log";
                auto child = ChildProcess::launch(process);
                wait([&] {
                    return std::filesystem::exists(state / "ready");
                });
                const bool invalid = std::string_view(scenario) == "version" ||
                                     std::string_view(scenario) == "capabilities" ||
                                     std::string_view(scenario) == "swapped" ||
                                     std::string_view(scenario) == "peer";
                bool rejected = false;
                try {
                    monitor.connect_owned(std::string_view(scenario) == "peer"
                                                  ? voyemu::os::current_process()
                                                  : child.pid());
                } catch (...) {
                    rejected = true;
                }

                check(rejected == invalid, "subscription identity/capability policy differs");

                if (invalid) {
                    child.terminate(1000);

                    if (std::string_view(scenario) == "swapped") {
                        std::filesystem::remove(socket);
                        std::filesystem::rename(socket.string() + ".retained", socket);
                    }

                    monitor.finish_owned_exit(child.pid(), 7);
                    continue;
                }

                {
                    std::ofstream release(state / "release-1");
                    release << "release";
                }
                wait([&] {
                    monitor.drain();

                    return !child.alive();
                });
                monitor.drain();
                const bool reset = std::string_view(scenario) == "guest-reset" ||
                                   std::string_view(scenario) == "fragmented";
                check(monitor.guest_reset(child.pid(), 7) == reset,
                      "shutdown cause classification differs");
                check(!monitor.guest_reset(child.pid(), 8) &&
                              !monitor.guest_reset(voyemu::os::current_process(), 7),
                      "stale generation/peer accepted");
                monitor.finish_owned_exit(child.pid(), 7);
                check(!std::filesystem::exists(socket), "owned socket not cleaned");
            }

            std::filesystem::remove_all(root);
            std::cout
                    << "owned QMP shutdown causes, fragmented/EOF drain, invalid peer/path/version/capability and stale generations passed\n";
        }

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
