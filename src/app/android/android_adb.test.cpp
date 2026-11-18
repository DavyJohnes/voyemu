#include "android_adb.hpp"
#include "android_adb_route.hpp"
#include "android_adb_trace.hpp"
#include "android_identity_probe.hpp"
#include "model.hpp"
#include "windows_startup.hpp"
#include <fstream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "private_json_reader.hpp"
#include <fcntl.h>
#include <io.h>
#endif
#include <algorithm>
#include <iostream>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace voyemu;
namespace {
void need(bool value) {
    if (!value) {
        throw std::runtime_error("owned ADB contract differs");
    }
}

template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::runtime_error &) {
        return;
    }

    throw std::runtime_error("unowned ADB accepted");
}
} // namespace

static void passive_trace_tests() {
    need(!android_adb_trace_enabled(nullptr) && android_adb_trace_enabled("1"));

    for (const char *value : {"", "0", "true", "1 "}) {
        rejects([&] {
            android_adb_trace_enabled(value);
        });
    }

    AndroidAdbTraceSanitizer parser(5584, 7);
    const std::string secret = "secret-nonce-0123456789",
                      uuid = "01234567-89ab-cdef-0123-456789abcdef",
                      key = "AUTH_PRIVATE_KEY_NEVER_PERSIST";
    const std::string frame(32, 'a');
    auto line = [](std::string value, int pid = 123) {
        return "10-18 14:00:00.001 " + std::to_string(pid) + " 17 V adb.exe : " + value + "\n";
    };
    const auto command = line("sockets.cpp:570 LS(441: connect(shell,v2,raw:echo VOYEMU_STARTUP_" +
                              frame + " " + secret + " " + uuid + " " + key + "))");
    std::string safe;

    for (std::size_t i = 0; i < command.size(); i += 3) {
        safe += parser(std::string_view(command).substr(i, 3), 123, false);
    }

    need(safe.empty());
    safe += parser(
            line("transport.cpp:573 emulator-5584: to remote: [OPEN] arg0=0x1b9 arg1=0 (len=600) " +
                 secret),
            123, false);
    safe += parser(
            line("transport.cpp:1154 emulator-5584: from remote: [OKAY] arg0=2 arg1=0x1b9 (len=0)"),
            123, false);
    safe += parser(
            line("transport.cpp:1154 emulator-5584: from remote: [WRTE] arg0=2 arg1=0x1b9 (len=32) " +
                 uuid),
            123, false);
    safe += parser(
            line("transport.cpp:1154 emulator-5584: from remote: [CLSE] arg0=2 arg1=0x1b9 (len=0)"),
            123, false);
    safe += parser(
            line("transport.cpp:573 emulator-5584: to remote: [AUTH] arg0=1 arg1=0 (len=64) " +
                 key),
            123, false);
    need(safe.find("framed_startup") != std::string::npos &&
         safe.find(sha256_bytes(frame)) != std::string::npos &&
         safe.find("stream_closed") != std::string::npos);

    for (const auto &forbidden :
         {secret, uuid, key, frame, std::string("shell,v2"), std::string("echo VOYEMU")}) {
        need(safe.find(forbidden) == std::string::npos);
    }

    need(parser(line("transport.cpp:573 emulator-5586: to remote: [OPEN] arg0=1 arg1=0 (len=1)"),
                123, false)
                 .empty());
    need(parser(line("transport.cpp:573 emulator-5584: to remote: [OPEN] arg0=1 arg1=0 (len=1)",
                     124),
                123, false)
                 .empty());
    need(parser(std::string(20000, 'X') + "\n", 123, false).empty());
    need(parser(line("transport.cpp:573 emulator-5584: to remote: [NOPE] arg0=1 arg1=0 (len=1)"),
                123, false)
                 .empty());

    for (const std::string program : {"adb", "adb.exe"}) {
        auto native = line(
                "transport.cpp:573 emulator-5584: from remote: [CNXN] arg0=1 arg1=0 (len=16) " +
                key);
        native.replace(native.find("adb.exe"), 7, program);
        const auto event = parser(native, 123, false);
        need(event.find("CNXN") != std::string::npos && event.find(key) == std::string::npos);
    }

    auto foreign = line(
            "transport.cpp:573 emulator-5584: from remote: [AUTH] arg0=1 arg1=0 (len=16) " + key);
    foreign.replace(foreign.find("adb.exe"), 7, "foreign");
    need(parser(foreign, 123, false).empty());
    auto eof = parser({}, 123, true);
    need(eof.find("adb_trace_eof") != std::string::npos &&
         eof.find("oversized_lines\":1") != std::string::npos);
}

static void identity_probe_tests(const std::filesystem::path &self) {
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-identity-" + random_token());

    ensure_private_directory(root);
    const AndroidIdentityProbeOwner owner{std::string(32, 'a'), 1, 200, 300, {}};
    auto process = [&](const std::string &value) {
        ProcessSpec spec;
        spec.executable = self;
        spec.arguments = {"--identity-child", "1000", value};
        spec.directory = root;
        spec.output = root / (random_token() + ".log");

        return spec;
    };
    auto read = [](const std::filesystem::path &path) {
#ifdef _WIN32
        detail::PrivateJsonReader file(path, 4096);

        return file.text();
#else
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            throw std::runtime_error("missing probe output");
        }
        return std::string(std::istreambuf_iterator<char>(file), {});
#endif
    };
    AndroidIdentityProbe probe;
    auto start = AndroidIdentityProbe::Clock::now();
    need(probe.launch(owner, process("nonce"), start));
    const auto pid = probe.pid();

    for (int i = 0; i < 100; ++i) {
        need(!probe.launch(owner, process("duplicate"), start));
        need(!probe.poll(owner, read, start));
        need(probe.pid() == pid);
    }

    need(AndroidIdentityProbe::Clock::now() - start < std::chrono::milliseconds(500));
    std::optional<ProcessResult> result;
    const auto end = AndroidIdentityProbe::Clock::now() + std::chrono::seconds(5);

    while (!result && AndroidIdentityProbe::Clock::now() < end) {
        result = probe.poll(owner, read);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    need(result && result->exit_code == 0 && !result->timed_out && result->output == "nonce");
    need(!probe.eligible());
    const auto completed_slot = probe.output().parent_path();
    probe.cancel();
    need(!std::filesystem::exists(completed_slot));
    need(probe.launch(owner, process("timeout"), start));
    result = probe.poll(owner, read, start + std::chrono::seconds(121));
    need(result && result->timed_out && !probe.pending() &&
         !probe.eligible(start + std::chrono::seconds(122)));
    probe.cancel();
    need(probe.launch(owner, process("finite"), start, std::chrono::seconds(15), 128 * 1024));
    result = probe.poll(owner, read, start + std::chrono::seconds(16));
    need(result && result->timed_out && !probe.pending());
    probe.cancel();
    auto delayed = process("slow-complete");
    delayed.arguments[1] = "1800";
    const auto slow_start = AndroidIdentityProbe::Clock::now();
    need(probe.launch(owner, std::move(delayed), slow_start, std::chrono::seconds(60), 128 * 1024));
    result.reset();
    const auto slow_end = slow_start + std::chrono::seconds(8);

    while (!result && AndroidIdentityProbe::Clock::now() < slow_end) {
        result = probe.poll(owner, read);

        if (AndroidIdentityProbe::Clock::now() - slow_start < std::chrono::milliseconds(1500)) {
            need(!result);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    need(result && !result->timed_out && result->exit_code == 0 &&
         result->output == "slow-complete");
    // A complete fenced tail can arrive before retained-client EOF. It grants no completion.
    const std::string frame(32, 'c'), nonce(32, 'd'), boot = "12345678-1234-1234-1234-123456789abc",
                                                      marker = "VOYEMU_STARTUP_" + frame;
    auto framed = [&](std::int64_t utc) {
        return marker + ":b1:" + boot + "\n" + marker + ":n1:" + nonce + "\n" + marker +
               ":body\nbody\n" + marker + ":exit:0\n" + marker + ":n2:" + nonce + "\n" + marker +
               ":b2:" + boot + "\n" + marker + ":utc:" + std::to_string(utc) + "\n";
    };
    auto complete = [&](const std::string &text) {
        try {
            return !parse_windows_startup_frame({0, text, false}, frame, nonce, boot).boot_changed;
        } catch (...) {
            return false;
        }
    };
    probe.cancel();
    auto unreadable = process("unreadable");
    unreadable.arguments[1] = "5000";
    need(probe.launch(owner, std::move(unreadable)));
    rejects([&] {
        probe.poll(
                owner,
                [](const auto &) -> std::string {
                    throw std::runtime_error("owned synthetic read failure");
                },
                AndroidIdentityProbe::Clock::now(), complete);
    });
    need(!probe.pending());

    for (int scenario = 0; scenario < 5; ++scenario) {
        probe.cancel();
        const auto utc = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        auto text = framed(utc - (scenario == 4 ? 55 : 0));

        if (scenario == 1) {
            text.replace(text.find(nonce), nonce.size(), std::string(32, 'e'));
        }

        if (scenario == 2) {
            text.pop_back();
        }

        auto child = process(text);
        child.arguments[1] = "3100";

        if (scenario == 3) {
            child.arguments.push_back("append");
        }

        need(probe.launch(owner, std::move(child)));
        result.reset();
        bool saw_pending_arrival = false;
        const auto finish = AndroidIdentityProbe::Clock::now() + std::chrono::seconds(5);

        while (!result && AndroidIdentityProbe::Clock::now() < finish) {
            result = probe.poll(owner, read, AndroidIdentityProbe::Clock::now(), complete);

            if (!result && probe.arrival()) {
                saw_pending_arrival = true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!result || result->timed_out || result->exit_code) {
            throw std::runtime_error("arrival child terminal scenario=" + std::to_string(scenario));
        }

        if (scenario == 0 || scenario == 4) {
            need(saw_pending_arrival && probe.arrival());
            const auto arrival = *probe.arrival();
            need(arrival.before_terminal);
            need(AndroidIdentityProbe::Clock::now() - arrival.steady >
                 std::chrono::milliseconds(2900));
            const auto parsed = parse_windows_startup_frame(*result, frame, nonce, boot);
            need(android_clock_current(parsed.utc_seconds,
                                       std::chrono::duration_cast<std::chrono::milliseconds>(
                                               arrival.utc.time_since_epoch())
                                               .count()) == (scenario == 0));
            need(!android_clock_current(parsed.utc_seconds,
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count()));
            need(!android_clock_current(parsed.utc_seconds,
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                                arrival.utc.time_since_epoch())
                                                        .count() +
                                                55000));
        } else {
            need(!probe.arrival());
        }
    }

    probe.cancel();
    auto terminal_only = process(framed(1700000000));
    terminal_only.arguments[1] = "10";
    need(probe.launch(owner, std::move(terminal_only)));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    result = probe.poll(owner, read, AndroidIdentityProbe::Clock::now(), complete);
    need(result && probe.arrival() && !probe.arrival()->before_terminal);
    std::cout
            << "Arrival vs delayed EOF regression: old timestamp rejects, validated arrival accepts; nonce/partial/append/wrong-clock negatives passed\n";
    probe.cancel();
    auto held = process("cancelled");
    held.arguments[1] = "5000";
    need(probe.launch(owner, std::move(held), AndroidIdentityProbe::Clock::now(),
                      std::chrono::seconds(60), 128 * 1024));
    const auto cancel_started = AndroidIdentityProbe::Clock::now();
    probe.cancel();
    need(!probe.pending() &&
         AndroidIdentityProbe::Clock::now() - cancel_started < std::chrono::milliseconds(1500));
    probe.cancel();
    rejects([&] {
        probe.launch(owner, process("invalid-budget"), start, std::chrono::milliseconds(0));
    });
    rejects([&] {
        probe.launch(owner, process("invalid-limit"), start, std::chrono::seconds(15),
                     512 * 1024 + 1);
    });
    auto routed = owner;
    routed.route = "private-route-a";
    need(probe.launch(routed, process("route")));
    auto other_route = routed;
    other_route.route = "private-route-b";
    rejects([&] {
        probe.poll(other_route, read);
    });
    need(!probe.pending());
    probe.cancel();
    need(probe.launch(owner, process("stale")));
    auto changed = owner;
    changed.generation++;
    rejects([&] {
        probe.poll(changed, read);
    });
    need(!probe.pending());

    for (auto part : {0, 1, 2}) {
        probe.cancel();
        need(probe.launch(owner, process("stale-owner")));
        changed = owner;

        if (part == 0) {
            changed.epoch = std::string(32, 'b');
        } else if (part == 1) {
            changed.sdk_pid++;
        } else {
            changed.server_pid++;
        }

        rejects([&] {
            probe.poll(changed, read);
        });
        need(!probe.pending());
    }

    probe.cancel();
    need(probe.launch(owner, process("missing")));
    bool rejected = false;
    const auto missing_end = AndroidIdentityProbe::Clock::now() + std::chrono::seconds(5);

    while (AndroidIdentityProbe::Clock::now() < missing_end && !rejected) {
        try {
            probe.poll(owner, [&](const auto &file) {
                std::filesystem::remove(file);

                return read(file);
            });
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    need(rejected && !probe.pending());
    probe.cancel();
    need(probe.launch(owner, process(std::string(5000, 'x'))));
    rejected = false;
    const auto bound = AndroidIdentityProbe::Clock::now() + std::chrono::seconds(5);

    while (AndroidIdentityProbe::Clock::now() < bound && !rejected) {
        try {
            probe.poll(owner, read);
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    need(rejected && !probe.pending());
    probe.cancel();
    need(std::filesystem::is_empty(root));
    {
        AndroidIdentityProbe replaced;
        need(replaced.launch(owner, process("replaced")));
#ifdef _WIN32
        // The inherited output handle prevents renaming the probe slot until the child exits.
        const HANDLE child_handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(replaced.pid()));
        need(child_handle != nullptr);
        const auto terminal = WaitForSingleObject(child_handle, 5000);
        CloseHandle(child_handle);
        need(terminal == WAIT_OBJECT_0);
#endif
        const auto slot = replaced.output().parent_path();
        const auto old = root / "retained-original-slot";
        std::filesystem::rename(slot, old);
        ensure_private_directory(slot);
        {
            std::ofstream foreign(slot / "output.log");
            foreign << "foreign";
        }
        rejects([&] {
            replaced.cancel();
        });
        need(!replaced.launch(owner, process("must-not-launch")));
        need(std::filesystem::exists(slot / "output.log") &&
             std::filesystem::exists(old / "output.log"));
    }
    unsigned depth = 0;
    std::uint64_t operation = 0;
    {
        AndroidIdentityScope outer(depth, operation);
        need(depth == 1 && operation == 1);
        {
            AndroidIdentityScope inner(depth, operation);
            need(depth == 2 && operation == 1);
        }
        need(depth == 1);
    }
    need(depth == 0);
    {
        AndroidIdentityScope next(depth, operation);
        need(operation == 2);
    }
    need(depth == 0);
    std::filesystem::remove_all(root);
}

#ifndef _WIN32
static int listener_child(const char *path, unsigned port, bool pair) {
    std::vector<int> sockets;

    for (unsigned i = 0; i < (pair ? 2u : 1u); ++i) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

        if (fd < 0) {
            return 2;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port + i);

        if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
            ::listen(fd, 2)) {
            return 3;
        }

        sockets.push_back(fd);
    }

    {
        std::ofstream ready(path);
        ready << "ready";
    }
    std::this_thread::sleep_for(std::chrono::seconds(15));

    return 0;
}

static void retained_owner_tests(const std::filesystem::path &self) {
    const auto root = std::filesystem::current_path() / ".local/build/tmp" /
                      ("adb-lease-" + random_token());

    ensure_private_directory(root);
    unsigned port = 5554;

    for (; port <= 5682; port += 2) {
        if (port != 5562 && tcp_port_free(port) && tcp_port_free(port + 1) &&
            tcp_port_free(port + 20000)) {
            break;
        }
    }

    need(port <= 5682);
    auto spawn = [&](std::string name, unsigned number, bool pair) {
        ProcessSpec process;
        process.executable = self;
        process.arguments = {"--listener-child", (root / name).string(), std::to_string(number),
                             pair ? "pair" : "single"};
        process.directory = root;
        process.output = root / (name + ".log");

        return ChildProcess::launch(process);
    };
    auto sdk = spawn("sdk", port, true), server = spawn("server", port + 20000, false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while ((!std::filesystem::exists(root / "sdk") || !std::filesystem::exists(root / "server")) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto owner = random_token();
    Json spec{{"adb", self.string()},
              {"port", port},
              {"owned_adb",
               {{"schema", 1},
                {"owner", owner},
                {"server_pid", server.pid()},
                {"server_port", port + 20000},
                {"grpc_port", port + 30000}}}};
    auto fence = [&](const Json &value, const std::string &sid, std::int64_t pid,
                     unsigned generation) {
        android_adb_owner_fence(value, spec, sid, sdk, server, pid, generation, 7, true);
    };
    fence(spec, owner, sdk.pid(), 7);
    need(android_adb_process(spec, {"shell", "true"}).arguments[3] == std::to_string(port + 20000));
    rejects([&] {
        fence(spec, random_token(), sdk.pid(), 7);
    });
    rejects([&] {
        fence(spec, owner, sdk.pid() + 1, 7);
    });
    rejects([&] {
        fence(spec, owner, sdk.pid(), 8);
    });
    auto wrong = spec;
    wrong["owned_adb"]["server_pid"] = sdk.pid();
    rejects([&] {
        fence(wrong, owner, sdk.pid(), 7);
    });
    wrong = spec;
    wrong["owned_adb"]["server_port"] = 5037;
    rejects([&] {
        android_adb_process(wrong, {"shell", "true"});
    });
    wrong = spec;
    wrong["owned_adb"]["server_port"] = port + 21000;
    rejects([&] {
        android_adb_owner_fence(wrong, wrong, owner, sdk, server, sdk.pid(), 7, 7, true);
    });
    server.terminate(100);
    rejects([&] {
        fence(spec, owner, sdk.pid(), 7);
    });
    need(sdk.alive() && sdk.owns_tcp_listener(port));
    sdk.terminate(100);
    need(!sdk.alive() && !server.alive());
    std::filesystem::remove_all(root);
}
#endif

static void console_auth_environment_tests(const std::filesystem::path &self) {
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-console-auth-" + random_token());
    const auto owned = root / "owned", foreign = root / "foreign";

    ensure_private_directory(owned / "emulator-home");
    ensure_private_directory(foreign / "emulator-home");
    const auto token = random_token();
    {
        std::ofstream(owned / "emulator-home/.emulator_console_auth_token") << token;
        std::ofstream(foreign / "emulator-home/.emulator_console_auth_token") << random_token();
    }
    ProcessSpec process;
    process.executable = self;
    process.arguments = {"--console-auth-child", sha256_bytes(token), "owned-avd"};
    // Model an inherited client home belonging to another session. The console
    // may return exit zero with no name when its authentication was rejected.
    android_session_environment(process, foreign);
    const auto denied = execute(process, 2000, 4096);
    need(!denied.timed_out && denied.exit_code == 0 && denied.output.empty());
    android_session_environment(process, owned);

    for (const char *name :
         {"HOME", "ANDROID_EMULATOR_HOME", "ANDROID_USER_HOME", "USERPROFILE", "LOCALAPPDATA"}) {
        need(process.environment.at(name) == (owned / "emulator-home").string());
    }

    for (const char *name : {"ANDROID_TMP", "TMPDIR", "TMP", "TEMP", "XDG_RUNTIME_DIR"}) {
        need(process.environment.at(name) == (owned / "tmp").string());
    }

    const auto accepted = execute(process, 2000, 4096);
    need(!accepted.timed_out && accepted.exit_code == 0 && accepted.output == "owned-avd\n");
    android_session_environment(process, foreign);
    const auto changed = execute(process, 2000, 4096);
    need(changed.exit_code == 0 && changed.output.empty() && changed.output != "owned-avd\n");
    std::filesystem::remove_all(root);
}

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--console-auth-child") {
#ifdef _WIN32
            const auto home = std::getenv("USERPROFILE");
#else
            const auto home = std::getenv("HOME");
#endif

            if (!home) {
                return 1;
            }

            std::ifstream input(std::filesystem::path(home) / ".emulator_console_auth_token");
            const std::string token((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());

            if (!token.empty() && sha256_bytes(token) == argv[2]) {
#ifdef _WIN32
                // The modeled console prints LF; Windows text mode would add CR.
                _setmode(_fileno(stdout), _O_BINARY);
#endif
                std::cout << argv[3] << '\n';
            }

            return 0;
        }

#ifndef _WIN32
        if (argc == 5 && std::string(argv[1]) == "--listener-child") {
            return listener_child(argv[2], std::stoul(argv[3]), std::string(argv[4]) == "pair");
        }
#endif

        if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--identity-child") {
            const auto delay = std::stoi(argv[2]);
            need(delay >= 1 && delay <= 5000);
            std::cout << argv[3] << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));

            if (argc == 5) {
                std::cout << argv[4] << std::flush;
            }

            return 0;
        }

#ifndef _WIN32
        retained_owner_tests(std::filesystem::absolute(argv[0]));
#endif
        passive_trace_tests();
        console_auth_environment_tests(std::filesystem::absolute(argv[0]));
        identity_probe_tests(std::filesystem::absolute(argv[0]));
        const std::string owner(32, 'a');
        const auto b = android_owned_adb(5574, 35574, owner);
        need(b.server == 25574 && b.endpoint() == "127.0.0.1:5575");
        Json spec{{"adb", "/private/pinned/adb"},
                  {"port", 5574},
                  {"owned_adb",
                   {{"schema", 1},
                    {"owner", owner},
                    {"server_pid", 1234},
                    {"server_port", 25574},
                    {"grpc_port", 35574}}}};
        const auto routed = android_adb_process(spec, {"shell", "dumpsys", "window"});
        need(routed.arguments ==
             android_owned_adb_arguments(b, {"shell", "dumpsys", "window"}, true));
        need(routed.environment.at("ADB_SERVER_SOCKET") == b.socket());
        need(android_adb_serial(spec) == "emulator-5574");
        spec["owned_adb"]["server_pid"] = 0;
        rejects([&] {
            android_adb_process(spec, {"shell", "true"});
        });
        spec["owned_adb"]["server_pid"] = 1234;
        spec["owned_adb"]["server_port"] = 5037;
        rejects([&] {
            android_adb_process(spec, {"shell", "true"});
        });

        need(android_owned_adb_server_arguments(b) ==
             std::vector<std::string>{"-L", "tcp:25574", "--one-device", "voyemu-owned-" + owner,
                                      "server", "nodaemon"});
        need(android_owned_adb_arguments(b, {"shell", "getprop", "qemu.voyemu.token"}, true) ==
             std::vector<std::string>{"-H", "127.0.0.1", "-P", "25574", "-s", "emulator-5574",
                                      "shell", "getprop", "qemu.voyemu.token"});
        need(android_owned_adb_registration_arguments(b, true, true) ==
             std::vector<std::string>{"-H", "127.0.0.1", "-P", "25574", "-s", "emulator-5574",
                                      "get-state"});
        need(android_owned_adb_arguments(b, {"emu", "avd", "name"}, true) ==
             std::vector<std::string>({"-H", "127.0.0.1", "-P", "25574", "-s", "emulator-5574",
                                       "emu", "avd", "name"}));
        need(b.serial() == "emulator-5574");

        for (const auto *state : {"device", "device\n", "device\r\r\n", " device\r\n"}) {
            need(android_owned_adb_registration_ready(state));
        }

        for (const auto *state :
             {"", "offline", "unauthorized", "unknown", "device\ndevice", "device\noffline",
              "device emulator-5576", "List of devices attached\nemulator-5576 device"}) {
            need(!android_owned_adb_registration_ready(state));
        }

        for (const auto &command :
             std::vector<std::vector<std::string>>{{"shell", "true"},
                                                   {"push", "local", "remote"},
                                                   {"root"},
                                                   {"reboot"},
                                                   {"forward", "--list"}}) {
            const auto args = android_owned_adb_arguments(b, command, true);
            need(args[5] == b.serial() &&
                 std::find(args.begin(), args.end(), "connect") == args.end() &&
                 std::find(args.begin(), args.end(), "-e") == args.end());
        }

        const auto sdk_env = android_owned_adb_sdk_environment(b);
        need(sdk_env.at("ADB_TRACE").empty() && sdk_env.at("ADB_SERVER_SOCKET") == b.socket());
        const auto env = android_owned_adb_environment(b);
        need(env.at("ADB_USB") == "0" && env.at("ADB_EMU") == "1" && env.at("ADB_MDNS") == "0" &&
             env.at("ADB_LOCAL_TRANSPORT_MAX_PORT") == "1" &&
             env.at("ADB_MDNS_AUTO_CONNECT") == "0" && env.at("ADB_SERVER_SOCKET") == b.socket());

        for (unsigned port : {0U, 5553U, 5555U, 5562U, 5684U, 65535U}) {
            rejects([&] {
                android_owned_adb(port, 35574, owner);
            });
        }

        for (unsigned port : {0U, 1023U, 5574U, 5575U, 65536U}) {
            rejects([&] {
                android_owned_adb(5574, port, owner);
            });
        }

        for (unsigned port : {1U, 5037U, 5574U, 5575U, 35574U, 65536U}) {
            rejects([&] {
                android_owned_adb(5574, 35574, owner, port);
            });
        }

        for (const auto &bad : std::vector<std::string>{
                     "", std::string(31, 'a'), std::string(32, 'G'), std::string(32, ';')}) {
            rejects([&] {
                android_owned_adb(5574, 35574, bad);
            });
        }

        rejects([&] {
            android_owned_adb_arguments(b, {"shell", "true"}, false);
        });
        rejects([&] {
            android_owned_adb_registration_arguments(b, false, true);
        });
        rejects([&] {
            android_owned_adb_registration_arguments(b, true, false);
        });

        for (const auto &verb : {"kill-server", "start-server", "server", "connect", "disconnect",
                                 "reconnect", "devices", "-P"}) {
            rejects([&] {
                android_owned_adb_arguments(b, {verb}, true);
            });
        }

        rejects([&] {
            android_owned_adb_arguments(b, {}, true);
        });
        need(android_owned_adb_arguments(
                     b, {"forward", "tcp:0", "localabstract:voyemu-ui-" + owner}, true)
                     .back() == "localabstract:voyemu-ui-" + owner);
        need(android_owned_adb_arguments(b, {"forward", "--remove", "tcp:12345"}, true).back() ==
             "tcp:12345");
        need(android_owned_adb_arguments(b, {"forward", "--list"}, true).back() == "--list");

        for (const auto &command :
             std::vector<std::vector<std::string>>{{"forward", "--remove-all"},
                                                   {"forward", "tcp:12345", "tcp:5555"},
                                                   {"forward", "tcp:0", "localabstract:foreign"},
                                                   {"forward", "--remove", "tcp:80"},
                                                   {"forward", "--remove", "tcp:12345junk"}}) {
            rejects([&] {
                android_owned_adb_arguments(b, command, true);
            });
        }

        need(android_owned_adb(5682, 65535, std::string(64, 'f')).server == 25682);
        std::cout
                << "Owned ADB endpoint, scan suppression, collisions and dead-owner rejection passed; host contracts and retained fake TCP children only; no SDK or ADB executed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
