#include "error.hpp"
#include "model.hpp"
#include "process.hpp"
#include "qnx_process_ram.hpp"
#include "windows_startup.hpp"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "runtime_fixture_media.hpp"
#include "runtime_fixture_wire.hpp"
namespace {
volatile std::sig_atomic_t stopped = 0;

void stop(int) {
    stopped = 1;
}
} // namespace
namespace {
thread_local unsigned framed_depth = 0;
std::uint64_t fixture_operation = 0;

std::vector<std::string> fixture_shell_words(std::string_view source) {
    std::vector<std::string> words;
    std::string word;
    bool quoted = false, started = false;

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];

        if (c == '\'') {
            quoted = !quoted;
            started = true;
            continue;
        }

        if (!quoted && c == '\\') {
            if (++i == source.size()) {
                throw voyemu::VoyemuError("FIXTURE", "fixture truncated shell escape");
            }

            word += source[i];
            started = true;
            continue;
        }

        if (!quoted && c == ' ') {
            if (started) {
                words.push_back(word);
                word.clear();
                started = false;
            }

            continue;
        }

        if (!quoted && (c == ';' || c == '\n' || c == '\r' || c == '$')) {
            throw voyemu::VoyemuError("FIXTURE", "fixture accepts closed quoted commands only");
        }

        word += c;
        started = true;
    }

    if (quoted) {
        throw voyemu::VoyemuError("FIXTURE", "fixture unmatched shell quote");
    }

    if (started) {
        words.push_back(word);
    }

    return words;
}
} // namespace
int fixture_main(int argc, char **argv);

int fixture_guest_command(const std::string &executable, const std::string &serial,
                          const std::string &command, std::string &output) {
    if (command == voyemu::windows_framework_probe_command()) {
        output += "activity:Service activity: found\npackage:Service package: found\nboot:1\n";

        return 0;
    }

    const bool clock = command.starts_with("voyemu_clock_caller()");
    const auto words = clock ? std::vector<std::string>{command} : fixture_shell_words(command);

    if (words.empty()) {
        throw voyemu::VoyemuError("FIXTURE", "fixture empty framed command");
    }

    if (words.size() == 3 && words[0] == "sh" && words[1] == "-c") {
        std::string_view body = words[2];
        constexpr std::string_view end = "; r=$?; [ \"$r\" -eq 0 ] || exit \"$r\"; ";

        while (!body.empty()) {
            constexpr std::string_view start = "print -r -- ";

            if (!body.starts_with(start)) {
                throw voyemu::VoyemuError("FIXTURE", "fixture unknown compound action");
            }

            body.remove_prefix(start.size());
            const auto separator = body.find("; ");

            if (separator == std::string_view::npos) {
                throw voyemu::VoyemuError("FIXTURE", "fixture missing compound marker");
            }

            const auto marker = fixture_shell_words(body.substr(0, separator));

            if (marker.size() != 1 || !marker[0].starts_with("VOYEMU_SECTION_")) {
                throw voyemu::VoyemuError("FIXTURE", "fixture invalid compound marker");
            }

            body.remove_prefix(separator + 2);
            const auto finish = body.find(end);

            if (finish == std::string_view::npos) {
                throw voyemu::VoyemuError("FIXTURE", "fixture missing compound status");
            }

            output += marker[0] + "\n";
            std::string captured;
            const auto result = fixture_guest_command(
                    executable, serial, std::string(body.substr(0, finish)), captured);
            output += captured;

            if (result) {
                return result;
            }

            body.remove_prefix(finish + end.size());
        }

        return 0;
    }

    if (words.size() == 1 && words[0] == "true") {
        return 0;
    }

    std::vector<std::string> args{executable, "-s", serial, "shell"};

    if (clock || (words.size() > 1 && words[0] == "content" && words[1] == "call") ||
        (words.size() > 2 && words[0] == "am" && words[1] == "startservice" &&
         command.find("'state_json'") != std::string::npos)) {
        args.push_back(command);
    } else {
        args.insert(args.end(), words.begin(), words.end());
    }

    std::vector<char *> pointers;

    for (auto &arg : args) {
        pointers.push_back(arg.data());
    }

    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    ++framed_depth;
    struct Restore {
        std::streambuf *value;

        ~Restore() {
            --framed_depth;
            std::cout.rdbuf(value);
        }
    } restore{previous};
    const auto result = fixture_main(static_cast<int>(pointers.size()), pointers.data());
    output += captured.str();

    return result;
}

int fixture_main(int argc, char **argv) {
    try {
        auto environment = std::getenv("VOYEMU_NATIVE_FIXTURE_ROOT");

        if (!environment) {
            throw voyemu::VoyemuError("FIXTURE", "fixture root missing");
        }

        auto root = std::filesystem::path(environment);

        if (argc > 2 && std::string(argv[1]) == "--session") {
            const auto session = std::filesystem::path(argv[2]);
            const auto owner = voyemu::read_private_json(session / "session-owner.json", 4096);

            if (owner.at("session_id") != session.filename().string()) {
                throw voyemu::VoyemuError("FIXTURE", "frontend fixture owner mismatch");
            }

            voyemu::Json args = voyemu::Json::array();

            for (int i = 1; i < argc; ++i) {
                args.push_back(argv[i]);
            }

            voyemu::write_private_json(root / "frontend-arguments.json", args);
            voyemu::ensure_private_directory(session / "media-native-proof");
            voyemu::write_private_json(session / "media-native-proof/result.json",
                                       {{"touches_released", false}});
            std::signal(SIGTERM, stop);
            std::signal(SIGINT, stop);

            while (!stopped) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            voyemu::write_private_json(session / "media-native-proof/result.json",
                                       {{"touches_released", true}});

            return 0;
        }

        if (argc > 2 && std::string(argv[1]) == "--qnx-checkpoint-fixture") {
            bool incoming = false;

            for (int i = 3; i < argc; ++i) {
                if (std::string(argv[i]) == "-incoming") {
                    incoming = true;
                }
            }

            const auto path = root / "qnx-launches.json";
            auto launches = std::filesystem::exists(path) ? voyemu::read_private_json(path)
                                                          : voyemu::Json::array();
            launches.push_back({{"incoming", incoming}});
            voyemu::write_private_json(path, launches);
            const auto ram = std::filesystem::path(argv[2]);
            {
                std::ofstream file(ram, std::ios::binary);
                file.seekp(8ULL * 1024 * 1024 * 1024 - 1);
                file.put(0);
            }
            std::filesystem::permissions(ram, std::filesystem::perms::owner_read |
                                                      std::filesystem::perms::owner_write);

            if (incoming) {
                return 4;
            }

            std::signal(SIGTERM, stop);
            std::signal(SIGINT, stop);

            while (!stopped) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            return 0;
        }

        if (argc > 1 && std::string(argv[1]) == "--runtime-path-fixture") {
            const char *runtime_path = std::getenv("VOYEMU_FIXTURE_RUNTIME_PATH");

            if (argc < 3 || !runtime_path) {
                throw voyemu::VoyemuError("FIXTURE", "fixture runtime path missing");
            }

            if (argc > 3) {
                const char *session = std::getenv("VOYEMU_FIXTURE_SESSION");

                if (!session) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture RAM session missing");
                }

                auto expected = voyemu::qnx_ram_launch(voyemu::host_qnx_engine).options;

                if (static_cast<std::size_t>(argc - 3) != expected.size()) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture RAM options differ");
                }

                for (std::size_t i = 0; i < expected.size(); ++i) {
                    const auto at = expected[i].find("${SESSION}");

                    if (at != std::string::npos) {
                        expected[i].replace(at, 10, session);
                    }

                    if (expected[i] != argv[i + 3]) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture RAM option differs");
                    }
                }
            }

            voyemu::write_private_json(root / "runtime-path-launch.json",
                                       {{"argument", argv[2]}, {"environment", runtime_path}});
            const char *temporary = std::getenv("TMPDIR");
            voyemu::write_private_json(root / "runtime-path-temp.json",
                                       {{"TMPDIR", temporary ? temporary : ""}});
            std::signal(SIGTERM, stop);
            std::signal(SIGINT, stop);

            while (!stopped) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            return 0;
        }

        if (framed_depth && argc > 3 && std::string_view(argv[1]) == "-s") {
            auto identity = voyemu::read_private_json(root / "identity.json", 4096);

            if (!identity.value("alive", false)) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK guest unavailable");
            }

            const auto restart_config = root / "root-restart-fixture.json",
                       restart_state = root / "root-restart-observation.json";

            if (framed_depth == 1 && argc == 5 && std::string_view(argv[3]) == "shell" &&
                std::string_view(argv[4]).starts_with("IFS= read -r b < ") &&
                std::filesystem::exists(restart_config) && std::filesystem::exists(restart_state)) {
                auto state = voyemu::read_private_json(restart_state);

                if (state.value("offline_remaining", 0) > 0) {
                    state["offline_remaining"] = state.at("offline_remaining").get<unsigned>() - 1;
                    state["offline_outer_requests"] = state.value("offline_outer_requests", 0) + 1;
                    voyemu::write_private_json(restart_state, state);
                    std::cout << "error: device offline\n";

                    return 1;
                }
            }

            if (framed_depth == 1 && argc > 3 && std::string_view(argv[3]) == "shell" &&
                std::filesystem::exists(root / "slow-startup-guest.json")) {
                const auto delay = voyemu::read_private_json(root / "slow-startup-guest.json", 1024)
                                           .at("milliseconds")
                                           .get<unsigned>();

                if (delay < 1501 || delay > 3000) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture slow startup bounds");
                }

                std::string_view command = argc > 4 ? argv[4] : "";
                std::string category = "other";

                if (command.starts_with("IFS= read -r b < ")) {
                    constexpr std::string_view marker = ":body; ( ";
                    const auto separator = command.find(marker);

                    if (separator == std::string_view::npos) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture missing slow framed body");
                    }

                    command.remove_prefix(separator + marker.size());
                }

                for (const char *verb : {"cat", "getprop", "id", "getenforce", "date", "cmd",
                                         "dumpsys", "am", "service", "sync"}) {
                    if (command == verb || command.starts_with("'" + std::string(verb) + "'")) {
                        category = verb;
                    }
                }

                if (command.starts_with("'content' 'call'")) {
                    category = "provider";
                }

                if (command.starts_with("'am' 'startservice'")) {
                    category = "state_submission";
                }

                const auto began = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                const auto receipts = root / "slow-startup-receipts";
                voyemu::ensure_private_directory(receipts);
                voyemu::write_private_json(
                        receipts / (voyemu::random_token() + ".json"),
                        {{"guest_command", true},
                         {"category", category},
                         {"boot_nonce_fenced",
                          std::string_view(argv[4]).starts_with("IFS= read -r b < ")},
                         {"minimum_delay_ms", delay},
                         {"actual_delay_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     std::chrono::steady_clock::now() - began)
                                                     .count()}});
            }

            if (argc == 5 && std::string_view(argv[3]) == "shell" &&
                std::string_view(argv[4]).starts_with(
                        "IFS= read -r b < /proc/sys/kernel/random/boot_id")) {
                const std::string script = argv[4];
                const auto marker_at = script.find("VOYEMU_STARTUP_");

                if (marker_at == std::string::npos) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture startup marker missing");
                }

                const auto frame = script.substr(marker_at + 15, 32);

                if (!voyemu::windows_startup_token(frame)) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture invalid startup marker");
                }

                auto literal = [&](const std::string &prefix, std::size_t begin = 0) {
                    const auto at = script.find(prefix, begin);

                    if (at == std::string::npos) {
                        return std::string{};
                    }

                    const auto first = at + prefix.size(), last = script.find('\'', first);

                    if (last == std::string::npos) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture incomplete startup identity");
                    }

                    return script.substr(first, last - first);
                };
                const auto nonce = literal("[ \"$n\" = '");
                const auto boot_pin = literal("[ \"$b\" = '");
                const auto first_nonce_at = script.find("[ \"$n\" = '");
                const auto alternate = literal("[ \"$n\" = '", first_nonce_at + 1);
                const auto marker = "VOYEMU_STARTUP_" + frame;
                const auto prefix = "print -r -- '" + marker + "':body; ( ";
                const auto begin = script.find(prefix), end = script.rfind(" ); r=$?; ");

                if (begin == std::string::npos || end == std::string::npos ||
                    end < begin + prefix.size()) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture unknown startup framing");
                }

                const auto body = script.substr(begin + prefix.size(), end - begin - prefix.size());

                if (script != voyemu::windows_startup_command({body}, frame, nonce, boot_pin,
                                                              !boot_pin.empty(), alternate, true)) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture startup framing changed");
                }

                auto snapshot = [&](std::pair<std::string, std::string> &value) {
                    std::string boot, token;
                    auto result = fixture_guest_command(
                            argv[0], argv[2], "'cat' '/proc/sys/kernel/random/boot_id'", boot);

                    if (result) {
                        return result;
                    }

                    result = fixture_guest_command(argv[0], argv[2],
                                                   "'getprop' 'qemu.voyemu.token'", token);

                    if (result) {
                        return result;
                    }

                    while (!boot.empty() && (boot.back() == '\n' || boot.back() == '\r')) {
                        boot.pop_back();
                    }

                    while (!token.empty() && (token.back() == '\n' || token.back() == '\r')) {
                        token.pop_back();
                    }

                    if (!voyemu::android_boot_id_valid(boot) ||
                        !voyemu::windows_startup_token(token)) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture malformed startup identity");
                    }

                    value = {boot, token};

                    return 0;
                };
                std::pair<std::string, std::string> before, after;
                const auto before_status = snapshot(before);

                if (before_status) {
                    return before_status;
                }

                std::cout << marker << ":b1:" << before.first << '\n'
                          << marker << ":n1:" << before.second << '\n';

                if ((before.second != nonce && before.second != alternate) ||
                    (!boot_pin.empty() && before.first != boot_pin)) {
                    return 79;
                }

                std::string result;
                const auto status = fixture_guest_command(argv[0], argv[2], body, result);

                if (snapshot(after)) {
                    return 78;
                }

                if (std::filesystem::exists(root / "slow-startup-guest.json") &&
                    body.find("'978' 'i32' '1195'") != std::string::npos &&
                    !std::filesystem::exists(root / "late-clock-injected.json")) {
                    voyemu::write_private_json(root / "clock-state.json",
                                               {{"offset_ms", -3600000}});
                    voyemu::write_private_json(root / "late-clock-injected.json",
                                               {{"injected", true}});
                }

                const auto clock = root / "clock-state.json";
                const auto offset = std::filesystem::exists(clock)
                                            ? voyemu::read_private_json(clock).value(
                                                      "offset_ms", std::int64_t{0})
                                            : 0;
                const auto utc = (std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count() +
                                  offset) /
                                 1000;
                std::cout << marker << ":body\n"
                          << result << '\n'
                          << marker << ":exit:" << status << '\n'
                          << marker << ":n2:" << after.second << '\n'
                          << marker << ":b2:" << after.first << '\n'
                          << marker << ":utc:" << utc << '\n';

                return 0;
            }

            auto probe = [&](const char *name) {
                if (!std::filesystem::exists(root / "readiness-probes.json")) {
                    return;
                }

                auto probes = voyemu::read_private_json(root / "readiness-probes.json");
                probes[name] = probes.value(name, 0) + 1;
                voyemu::write_private_json(root / "readiness-probes.json", probes);
            };
            auto gate = [&](const char *operation) {
                auto path = root / "adb-gate.json";

                if (!std::filesystem::exists(path) ||
                    std::filesystem::exists(root / "adb-gate-held")) {
                    return;
                }

                auto config = voyemu::read_private_json(path, 1024);

                if (config.at("operation") != operation) {
                    return;
                }

                auto milliseconds = config.value("milliseconds", 600);

                if (milliseconds < 1 || milliseconds > 1200) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture gate bounds");
                }

                voyemu::write_private_json(root / "adb-gate-held", {{"operation", operation}});
                auto until = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(milliseconds);

                while (!std::filesystem::exists(root / "adb-gate-release") &&
                       std::chrono::steady_clock::now() < until) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            };

            if (argc == 4 && std::string_view(argv[3]) == "root" &&
                std::filesystem::exists(restart_config)) {
                const auto config = voyemu::read_private_json(restart_config);
                auto state = std::filesystem::exists(restart_state)
                                     ? voyemu::read_private_json(restart_state)
                                     : voyemu::Json::object();
                state["root_calls"] = state.value("root_calls", 0) + 1;
                state["offline_remaining"] = 1;
                voyemu::write_private_json(restart_state, state);

                if (config.value("secret_negative", false)) {
                    std::cout
                            << "adbd cannot run as root in production builds\nfixture-root-private-"
                            << std::string(32, 'd') << '\n';

                    return 1;
                }

                std::cout << "restarting adbd as root\n";

                return 0;
            }

            if (argc == 4 &&
                (std::string(argv[3]) == "root" || std::string(argv[3]) == "wait-for-device")) {
                return 0;
            }

            if (argc == 6 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "id" &&
                std::string(argv[5]) == "-u") {
                if (std::filesystem::exists(restart_config)) {
                    const auto config = voyemu::read_private_json(restart_config);
                    auto state = std::filesystem::exists(restart_state)
                                         ? voyemu::read_private_json(restart_state)
                                         : voyemu::Json::object();
                    const auto uid = state.value("root_calls", 0) > 0
                                             ? config.at("after_uid").get<unsigned>()
                                             : 2000u;
                    state[uid == 0 ? "uid0_reads" : "uid2000_reads"] =
                            state.value(uid == 0 ? "uid0_reads" : "uid2000_reads", 0) + 1;
                    voyemu::write_private_json(restart_state, state);
                    std::cout << uid << '\n';

                    return 0;
                }

                std::cout << "0\n";

                return 0;
            }

            if (argc == 5 && std::string(argv[3]) == "shell" &&
                std::string(argv[4]) == "getenforce") {
                std::cout << "Enforcing\n";

                return 0;
            }

            if (argc == 6 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "cat" &&
                std::string(argv[5]) == "/proc/sys/kernel/random/boot_id") {
                if (std::filesystem::exists(root / "boot-id-fail-once")) {
                    std::filesystem::remove(root / "boot-id-fail-once");

                    return 7;
                }

                const auto token = identity.value("boot_token",
                                                  identity.at("token").get<std::string>());

                if (token.size() != 32) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture boot identity invalid");
                }

                std::cout << token.substr(0, 8) << '-' << token.substr(8, 4) << '-'
                          << token.substr(12, 4) << '-' << token.substr(16, 4) << '-'
                          << token.substr(20) << '\n';

                return 0;
            }

            if (argc > 5 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "date" &&
                std::string(argv[5]) == "+%s") {
                const auto clock = root / "clock-state.json";
                const auto offset = std::filesystem::exists(clock)
                                            ? voyemu::read_private_json(clock).value(
                                                      "offset_ms", std::int64_t{0})
                                            : 0;
                std::cout << (std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count() +
                              offset) /
                                     1000
                          << '\n';

                return 0;
            }

            if (argc == 7 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "service" &&
                std::string(argv[5]) == "check" && std::string(argv[6]) == "alarm") {
                const auto receipt = root / "alarm-service-probes.json";
                const auto count = std::filesystem::exists(receipt)
                                           ? voyemu::read_private_json(receipt).value("count", 0)
                                           : 0;
                const bool absent = std::filesystem::exists(root / "alarm-service-absent");
                voyemu::write_private_json(receipt, {{"count", count + 1}, {"available", !absent}});
                std::cout << (absent ? "Service alarm: not found\n" : "Service alarm: found\n");

                return 0;
            }

            const bool clock_caller =
                    argc == 5 && std::string(argv[3]) == "shell" &&
                    std::string(argv[4]).starts_with(
                            "voyemu_clock_caller() { printf 'VOYEMU_CLOCK_V1 begin operation=alarm.set-time ");

            if (clock_caller ||
                (argc > 7 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "cmd" &&
                 std::string(argv[5]) == "alarm" && std::string(argv[6]) == "set-time")) {
                if (std::filesystem::exists(root / "alarm-service-absent")) {
                    voyemu::write_private_json(root / "premature-clock-write.json",
                                               {{"submitted", true}});

                    return 20;
                }

                const std::string clock_script = clock_caller ? argv[4] : "";
                const auto target = std::stoll(
                        clock_caller
                                ? clock_script.substr(
                                          clock_script.find("/system/bin/cmd alarm set-time ") + 30)
                                : argv[7]);
                const bool applied = !std::filesystem::exists(root / "clock-deny");
                const bool observed =
                        std::filesystem::exists(root / "clock-observation-fixture.json");

                if (observed) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
                }

                const auto applied_host_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
                const auto receipt = root / "clock-set-receipt.json";
                const auto count = std::filesystem::exists(receipt)
                                           ? voyemu::read_private_json(receipt).value("count", 0)
                                           : 0;
                voyemu::write_private_json(
                        receipt,
                        {{"count", count + 1}, {"target_ms", target}, {"applied", applied}});

                if (observed) {
                    voyemu::write_private_json(root / "clock-apply-observation.json",
                                               {{"target_ms", target},
                                                {"actual_apply_host_ms", applied_host_ms},
                                                {"body_exit_code", 255}});
                }

                if (applied) {
                    voyemu::write_private_json(
                            root / "clock-state.json",
                            {{"offset_ms", observed ? target - applied_host_ms : 0}});
                }

                return 255; // Actual OEM AlarmManager returns this even after applying time.
            }

            if (argc > 5 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "getprop") {
                if (std::string(argv[5]) == "sys.boot_completed") {
                    probe("boot");
                    std::cout << "1\n";

                    return 0;
                }

                if (std::string(argv[5]) == "qemu.voyemu.token" &&
                    std::filesystem::exists(root / "identity-bootstrap-hold")) {
                    voyemu::ensure_private_directory(root / "identity-probe-clients");
                    const auto hold = voyemu::read_private_json(root / "identity-bootstrap-hold",
                                                                1024);
                    const auto phase = hold.at("phase").get<unsigned>();
                    voyemu::write_private_json(
                            root / "identity-probe-clients" /
                                    (std::to_string(fixture_operation) + ".json"),
                            {{"operation", fixture_operation},
                             {"phase", phase},
                             {"property", "qemu.voyemu.token"}});
                    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);

                    while (std::chrono::steady_clock::now() < until) {
                        const auto current =
                                voyemu::read_private_json(root / "identity-bootstrap-hold", 1024);

                        if (current.at("phase") != phase || current.value("released", false)) {
                            break;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }

                gate("identity");
                probe("token");

                if (std::filesystem::exists(root / "identity-hold")) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture identity endpoint not ready");
                }

                std::cout << (std::filesystem::exists(root / "stale-identity")
                                      ? std::string(32, 'f')
                                      : identity.at("token").get<std::string>())
                          << '\n';

                return 0;
            }

            if (argc > 5 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "cmd" &&
                std::string(argv[5]) == "package") {
                std::cout
                        << "com.qinggan.app.launcher/com.qinggan.app.launcher.activity.MainActivity\n";

                return 0;
            }

            if (argc > 5 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "dumpsys") {
                if (std::string(argv[5]) == "display") {
                    probe("display");

                    if (std::filesystem::exists(root / "repair-required")) {
                        std::cout << "Display 0:\n mPrimaryDisplayDevice=Built-in Screen\n";

                        return 0;
                    }

                    std::cout
                            << "Display 0:\n mPrimaryDisplayDevice=Built-in Screen\nDisplay 1:\n mPrimaryDisplayDevice=Emulator2D\n real 1920 x 720, state ON\n";

                    return 0;
                }

                if (std::string(argv[5]) == "window") {
                    probe("window");

                    if (std::filesystem::exists(root / "systemui-repair-required")) {
                        std::cout
                                << "Window #0 com.qinggan.app.launcher/com.qinggan.app.launcher.activity.MainActivity\n mDisplayId=0 isOnScreen=true\nWindow #1 com.qinggan.app.launcher/com.qinggan.secondlauncher.activity.SecondMainActivity\n mDisplayId=1 isOnScreen=true\n";

                        return 0;
                    }

                    std::cout
                            << "Window #0 com.qinggan.app.launcher/com.qinggan.app.launcher.activity.MainActivity\n mDisplayId=0 isOnScreen=true\nWindow #1 com.qinggan.app.launcher/com.qinggan.secondlauncher.activity.SecondMainActivity\n mDisplayId=1 isOnScreen="
                            << (std::filesystem::exists(root / "display-hold") ? "false" : "true")
                            << "\nWindow #2 Window{a u0 StatusBar1}:\n mDisplayId=0 package=com.qinggan.systemui appop=NONE\n mFrame=[0,0][1920,50] isOnScreen=true\nWindow #3 Window{b u0 StatusBar2}:\n mDisplayId=1 package=com.qinggan.systemui appop=NONE\n mFrame=[0,0][1920,50] isOnScreen=true\n";

                    return 0;
                }
            }

            if (argc > 5 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "am" &&
                std::string(argv[5]) == "broadcast") {
                probe("wake");

                if (std::filesystem::exists(root / "readiness-probes.json")) {
                    auto probes = voyemu::read_private_json(root / "readiness-probes.json");
                    auto times = probes.value("wake_times_ms", voyemu::Json::array());
                    times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count());
                    probes["wake_times_ms"] = times;
                    voyemu::write_private_json(root / "readiness-probes.json", probes);

                    if (std::filesystem::exists(root / "reject-duplicate-activation") &&
                        probes.value("wake", 0) >
                                (std::filesystem::exists(root / "allow-recovery") ? 2 : 1)) {
                        throw voyemu::VoyemuError("FIXTURE",
                                                  "fixture duplicate display activation rejected");
                    }
                }

                std::cout << "Broadcast completed\n";

                return 0;
            }

            if (argc > 7 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "am" &&
                std::string(argv[5]) == "startservice" &&
                std::string(argv[7]) == "com.qinggan.systemui/.SystemUIService") {
                probe("systemui_start");
                std::filesystem::remove(root / "systemui-repair-required");
                std::cout << "Starting service: fixture\n";

                return 0;
            }

            if (argc > 7 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "am" &&
                std::string(argv[5]) == "startservice" &&
                std::string(argv[7]) == "com.voyemu/com.voyemu.output.SecondScreenService") {
                std::filesystem::remove(root / "repair-required");
                std::cout << "Starting service: fixture\n";

                return 0;
            }

            if (argc > 4 && std::string(argv[3]) == "emu" &&
                std::string(argv[4]) == "multidisplay") {
                probe("resize");
                std::cout << "OK\n";

                return 0;
            }

            if (argc > 4 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "sync") {
                if (std::filesystem::exists(root / "refuse-sync")) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture guest sync rejected");
                }

                voyemu::write_private_json(
                        root / "guest-sync.json",
                        {{"session_id", identity.at("token")}, {"status", "flushed"}});

                return 0;
            }

            if (argc > 7 && std::string(argv[3]) == "emu" && std::string(argv[4]) == "avd" &&
                std::string(argv[5]) == "snapshot" && std::string(argv[6]) == "save") {
                if (voyemu::read_private_json(root / "guest-sync.json").at("session_id") !=
                    identity.at("token")) {
                    throw voyemu::VoyemuError("FIXTURE", "SDK snapshot occurred before guest sync");
                }

                if (std::filesystem::exists(root / "held-input.json") &&
                    !voyemu::read_private_json(root / "held-input.json").empty()) {
                    throw voyemu::VoyemuError("FIXTURE", "SDK snapshot retained held input");
                }

                auto path = std::filesystem::path(identity.at("machine").get<std::string>()) /
                            "snapshots" / argv[7];
                voyemu::ensure_private_directory(path);

                for (const char *name : {"snapshot.pb", "ram.bin", "hardware.ini"}) {
                    std::ofstream(path / name) << "complete fixture SDK snapshot";
                }

                voyemu::write_private_json(root / "snapshot-boot.json",
                                           {{"token", identity.at("token")}, {"name", argv[7]}});
                std::cout << "OK\n";

                return 0;
            }

            if (argc > 4 && std::string(argv[3]) == "emu" && std::string(argv[4]) == "kill") {
                if (voyemu::read_private_json(root / "guest-sync.json").at("session_id") !=
                    identity.at("token")) {
                    throw voyemu::VoyemuError("FIXTURE",
                                              "SDK kill occurred before this guest sync");
                }

                if (std::string(argv[2]) !=
                    "emulator-" + std::to_string(identity.at("port").get<unsigned>())) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture refused foreign serial kill");
                }

                voyemu::write_private_json(root / "emu-kill-receipt.json",
                                           {{"session_id", identity.at("token")},
                                            {"avd", identity.at("avd")},
                                            {"method", "identity-scoped emu kill"}});
                {
                    std::ofstream marker(
                            root /
                            ("emu-kill-" + std::to_string(identity.at("pid").get<std::int64_t>())));
                    marker << "stop only fixture SDK leaf\n";
                }
                std::cout << "OK: killing emulator\n";

                return 0;
            }

            if (argc > 5 && std::string(argv[3]) == "emu" && std::string(argv[4]) == "avd") {
                gate("name");
                probe("name");
                std::cout << identity.at("avd").get<std::string>() << "\nOK\n";

                return 0;
            }

            if (argc > 5 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "service") {
                probe("theme");
                bool skin = false;

                for (int i = 1; i < argc; ++i) {
                    if (std::string(argv[i]) == "978") {
                        skin = true;
                    }
                }

                std::cout << (skin ? "Result: Parcel(00000000 000003e9  '....')\n"
                                   : "Result: Parcel(00000000 00000002  '....')\n");

                return 0;
            }

            if (argc > 9 && std::string(argv[3]) == "shell" && std::string(argv[4]) == "content" &&
                std::string(argv[5]) == "query") {
                if (std::string(argv[7]) !=
                            "content://qinggan.settings/system/system_language_setting" ||
                    std::string(argv[9]) != "value") {
                    throw voyemu::VoyemuError("FIXTURE", "fixture rejected unknown OEM query");
                }

                probe("language");
                const auto language = root / "oem-language.json";
                std::cout << (std::filesystem::exists(language)
                                      ? voyemu::read_private_json(language, 4096)
                                                .at("output")
                                                .get<std::string>()
                                      : "No result found.\n");

                return 0;
            }

            if (argc > 4 && std::string(argv[3]) == "shell" &&
                std::string(argv[4]).starts_with("'content' 'call'")) {
                std::string command = argv[4];
                auto value = [&](const std::string &marker) {
                    auto start = command.find(marker);

                    if (start == std::string::npos) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture content argument missing");
                    }

                    start += marker.size();
                    auto end = command.find('\'', start);

                    if (end == std::string::npos) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture content argument malformed");
                    }

                    return command.substr(start, end - start);
                };
                auto method = value("'--method' '");

                if (value("'--uri' '") == "content://com.voyemu.output.vehicle") {
                    if (method != "status") {
                        throw voyemu::VoyemuError("FIXTURE", "fixture state provider is read-only");
                    }

                    if (!std::filesystem::exists(root / "full-input.json")) {
                        std::cout
                                << "Result: Bundle[{json={\"schema\":1,\"status\":\"not-ready\"}}]\n";

                        return 0;
                    }

                    if (std::filesystem::exists(root / "slow-startup-guest.json") &&
                        !std::filesystem::exists(root / "delayed-state-ack.json")) {
                        voyemu::write_private_json(root / "delayed-state-ack.json",
                                                   {{"pending_observed", true}});
                        std::cout
                                << "Result: Bundle[{json={\"schema\":1,\"status\":\"not-ready\"}}]\n";

                        return 0;
                    }

                    auto state = voyemu::read_private_json(
                            root / (std::filesystem::exists(root / "stale-state-receipt")
                                            ? "stale-full-input.json"
                                            : "full-input.json"));
                    std::cout << "Result: Bundle[{json="
                              << voyemu::Json({{"schema", 1},
                                               {"status", "applied"},
                                               {"map_sha256", voyemu::map_sha256},
                                               {"sequence", state.at("sequence")},
                                               {"field_count", 579},
                                               {"raw_oem", state.at("raw_oem")},
                                               {"typed_projection",
                                                {{"TJA_HWA_STS",
                                                  state.at("raw_oem").at("mpc_icaSts")}}}})
                                         .dump()
                              << "}]\n";

                    return 0;
                }

                auto session = value("session:s:");
                auto auth = value("token:s:");

                if (auth.size() != 32 || auth == session) {
                    throw voyemu::VoyemuError("FIXTURE",
                                              "fixture rejected public id as private credential");
                }

                auto calls_path = root / "content-requests.json";
                auto calls = std::filesystem::exists(calls_path)
                                     ? voyemu::read_private_json(calls_path)
                                     : voyemu::Json::array();
                calls.push_back({{"method", method}, {"session_id", session}, {"auth", auth}});
                voyemu::write_private_json(calls_path, calls);
                voyemu::Json reply;

                if (method == "bootstrap") {
                    if (std::filesystem::exists(root / "readiness-probes.json")) {
                        voyemu::write_private_json(
                                root / "bootstrap-probes.json",
                                voyemu::read_private_json(root / "readiness-probes.json"));
                    }

                    voyemu::write_private_json(root / "held-input.json", voyemu::Json::object());
                    voyemu::write_private_json(root / "provider-session.json",
                                               {{"session_id", session}, {"auth", auth}});
                    reply = {{"schema", 1}, {"session_id", session}, {"status", "session-ready"}};
                } else {
                    auto provisioned = voyemu::read_private_json(root / "provider-session.json",
                                                                 4096);

                    if (provisioned.at("session_id").get<std::string>() != session ||
                        provisioned.at("auth").get<std::string>() != auth) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture provider auth mismatch");
                    }

                    if (method == "input") {
                        auto held = voyemu::read_private_json(root / "held-input.json");

                        if (value("phase:s:") == "down") {
                            held[value("button:s:")] = session;
                        } else {
                            held.erase(value("button:s:"));
                        }

                        voyemu::write_private_json(root / "held-input.json", held);
                        auto sequence = std::stoul(value("sequence:l:"));
                        reply = {{"schema", 1},
                                 {"session_id", session},
                                 {"sequence", sequence},
                                 {"button", value("button:s:")},
                                 {"phase", value("phase:s:")},
                                 {"status", "callbacks"},
                                 {"callbacks", 1},
                                 {"consumed", 0},
                                 {"key_code", 3074},
                                 {"actions", voyemu::Json::array({4096})}};
                        voyemu::write_private_json(root / "provider-input-receipt.json", reply);
                        // Force status recovery after a consumed input: no credential may enter
                        // either reply.
                        std::cout << "Result: Bundle[{error=storage-or-delivery}]\n";

                        return 0;
                    }

                    if (method == "status") {
                        if (command.find("'--arg' 'bootstrap'") != std::string::npos) {
                            reply = {{"schema", 1},
                                     {"session_id", session},
                                     {"status", "session-ready"}};
                        } else {
                            reply = voyemu::read_private_json(root / "provider-input-receipt.json",
                                                              4096);
                        }
                    } else {
                        throw voyemu::VoyemuError("FIXTURE", "fixture unknown content method");
                    }
                }

                std::cout << "Result: Bundle[{json=" << reply.dump() << "}]\n";

                return 0;
            }

            if (argc > 4 && std::string(argv[3]) == "shell" &&
                std::string(argv[4]).starts_with("'am' 'startservice'")) {
                std::string command = argv[4], marker = "'state_json' '";
                auto start = command.find(marker);

                if (start == std::string::npos) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture source JSON missing");
                }

                start += marker.size();
                auto end = command.find('\'', start);

                if (end == std::string::npos) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture source JSON malformed");
                }

                auto state = voyemu::Json::parse(command.substr(start, end - start));
                voyemu::validate_state(state);
                gate("state");

                // Match the guest adapter: an older or conflicting equal revision is rejected.
                if (std::filesystem::exists(root / "full-input.json")) {
                    auto current = voyemu::read_private_json(root / "full-input.json");

                    if (state["sequence"] <= current["sequence"] &&
                        !(state["sequence"] == current["sequence"] &&
                          state["raw_oem"] == current["raw_oem"])) {
                        std::cout << "Starting service: rejected revision fixture\n";

                        return 0;
                    }
                }

                voyemu::write_private_json(root / "full-input.json", state);

                if (std::filesystem::exists(root / "readiness-probes.json")) {
                    voyemu::write_private_json(
                            root / "state-probes.json",
                            voyemu::read_private_json(root / "readiness-probes.json"));
                }
            }

            std::cout << "Starting service: fixture\n";

            return 0;
        }

        std::string avd, token;
        unsigned port = 0, grpc = 0;
        bool engine = argc > 1 && std::string(argv[1]) == "--owned-engine";

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "-avd" && i + 1 < argc) {
                avd = argv[++i];
            } else if (arg == "-port" && i + 1 < argc) {
                port = static_cast<unsigned>(std::stoul(argv[++i]));
            } else if (arg == "-grpc" && i + 1 < argc) {
                grpc = static_cast<unsigned>(std::stoul(argv[++i]));
            } else if (arg == "-prop" && i + 1 < argc) {
                std::string property = argv[++i];

                if (property.starts_with("qemu.voyemu.token=")) {
                    token = property.substr(18);
                }
            }
        }

        if (avd.empty() || token.size() != 32) {
            throw voyemu::VoyemuError("FIXTURE", "fixture emulator arguments missing");
        }

        const bool direct = std::filesystem::exists(root / "direct-engine");
        std::filesystem::path machine;

        if (!engine) {
            const auto *sdk_trace = std::getenv("ADB_TRACE");
            const auto *server_address = std::getenv("ANDROID_ADB_SERVER_ADDRESS"),
                       *server_port = std::getenv("ANDROID_ADB_SERVER_PORT"),
                       *server_socket = std::getenv("ADB_SERVER_SOCKET");
            const std::string destination = server_port ? server_port : "";
            const bool isolated =
                    server_address && std::string_view(server_address) == "127.0.0.1" &&
                    !destination.empty() &&
                    destination.find_first_not_of("0123456789") == std::string::npos &&
                    destination != "5037" && server_socket &&
                    server_socket == "tcp:127.0.0.1:" + destination;
            voyemu::write_private_json(
                    root / "sdk-adb-trace.json",
                    {{"empty", !sdk_trace || !*sdk_trace}, {"isolated", isolated}});
            voyemu::Json args = voyemu::Json::array();

            for (int i = 1; i < argc; ++i) {
                args.push_back(argv[i]);
            }

            voyemu::write_private_json(root / "emulator-arguments.json", args);
            const auto *trace = std::getenv("VOYEMU_ANDROID_BOOT_TRACE");
            voyemu::write_private_json(
                    root / "emulator-boot-trace.json",
                    {{"present", trace != nullptr}, {"value", trace ? trace : ""}});
            auto avd_home = std::getenv("ANDROID_AVD_HOME");

            if (!avd_home) {
                throw voyemu::VoyemuError("FIXTURE", "fixture AVD home missing");
            }

            std::ifstream locator(std::filesystem::path(avd_home) / (avd + ".ini"));
            std::string line;
            std::filesystem::path content;

            while (std::getline(locator, line)) {
                if (line.starts_with("path=")) {
                    content = line.substr(5);
                }
            }

            if (content.empty()) {
                throw voyemu::VoyemuError("FIXTURE", "fixture AVD locator missing");
            }

            machine = content;
            auto argument = [&](const char *key) {
                for (int i = 1; i + 1 < argc; ++i) {
                    if (std::string(argv[i]) == key) {
                        return std::string(argv[i + 1]);
                    }
                }

                return std::string{};
            };
            voyemu::Json environment = voyemu::Json::object();

            for (const char *name :
                 {"HOME", "ANDROID_EMULATOR_HOME", "ANDROID_USER_HOME", "USERPROFILE",
                  "LOCALAPPDATA", "ANDROID_TMP", "TMPDIR", "TMP", "TEMP", "XDG_RUNTIME_DIR"}) {
                const char *value = std::getenv(name);
                environment[name] = value ? value : "";
            }

            voyemu::write_private_json(root / "emulator-environment.json", environment);
            voyemu::write_private_json(root / "emulator-paths.json",
                                       {{"avd_content", content.string()},
                                        {"datadir", argument("-datadir")},
                                        {"data", argument("-data")},
                                        {"cache", argument("-cache")},
                                        {"encryption_key", argument("-encryption-key")}});
            const auto cache = argument("-cache");

            if (!cache.empty()) {
                for (const auto &file :
                     {std::filesystem::path(cache), std::filesystem::path(cache + ".qcow2")}) {
                    if (!std::filesystem::exists(file)) {
                        std::ofstream output(file);
                        output << "SDK-created disposable cache fixture";
                    }
                }
            }

            // Model the pinned SDK console's ordinary config.ini persistence.
            std::ofstream config(content / "config.ini", std::ios::app);
            config << "hw.display1.flag = 1737\n";

            if (!config) {
                throw voyemu::VoyemuError("FIXTURE", "fixture runtime config persistence failed");
            }
        }

#ifndef _WIN32
        if (!engine && !direct) {
            voyemu::ProcessSpec leaf;
            leaf.executable = std::filesystem::canonical(argv[0]);
            leaf.arguments = {"--owned-engine",
                              "-avd",
                              avd,
                              "-port",
                              std::to_string(port),
                              "-prop",
                              "qemu.voyemu.token=" + token};

            for (int i = 1; i + 1 < argc; ++i) {
                if (std::string(argv[i]) == "-snapshot") {
                    leaf.arguments.insert(leaf.arguments.end(), {"-snapshot", argv[i + 1]});
                }
            }

            leaf.output = root / "engine.log";
            auto child = voyemu::ChildProcess::launch(leaf);
            child.detach();

            return 0;
        }
#endif
#ifdef _WIN32
        WSADATA data{};

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw voyemu::VoyemuError("FIXTURE", "fixture Winsock unavailable");
        }

        using Socket = SOCKET;
        constexpr Socket invalid_socket = INVALID_SOCKET;
#else
        using Socket = int;
        constexpr Socket invalid_socket = -1;
#endif
        auto listen = [&](unsigned number) {
            Socket socket = ::socket(AF_INET, SOCK_STREAM, 0);

            if (socket == invalid_socket) {
                throw voyemu::VoyemuError("FIXTURE", "fixture listener unavailable");
            }

#ifndef _WIN32
            int reusable = 1;

            if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reusable, sizeof reusable)) {
                throw voyemu::VoyemuError("FIXTURE", "fixture reusable listener failed");
            }
#endif
            const auto failed = [&](const char *stage) {
#ifdef _WIN32
                const auto error = WSAGetLastError();
#else
                const auto error = errno;
#endif
                throw voyemu::VoyemuError("FIXTURE",
                                          "fixture listener " + std::string(stage) +
                                                  " failed: port=" + std::to_string(number) +
                                                  " error=" + std::to_string(error));
            };
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(static_cast<std::uint16_t>(number));

            if (::bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
                failed("bind");
            }

            if (::listen(socket, 32) != 0) {
                failed("listen");
            }

            fixture_wire::nonblocking(socket);

            return socket;
        };
        fixture_wire::Socket console(listen(port)), adb_listener(listen(port + 1));
        const char *home = std::getenv("ANDROID_EMULATOR_HOME");

        if (!home) {
            throw voyemu::VoyemuError("FIXTURE", "fixture console home missing");
        }

        const auto console_token = voyemu::random_token();
        const auto console_auth = std::filesystem::path(home) / ".emulator_console_auth_token";
        {
            std::ofstream output(console_auth);
            output << console_token << '\n';
        }
        std::filesystem::permissions(console_auth, std::filesystem::perms::owner_read |
                                                           std::filesystem::perms::owner_write);

        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "-snapshot") {
                auto saved = voyemu::read_private_json(root / "snapshot-boot.json", 4096);

                if (saved.at("name").get<std::string>() != argv[i + 1]) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture snapshot name changed");
                }

                token = saved.at("token").get<std::string>();
            }
        }

#ifdef _WIN32
        auto pid = std::int64_t(GetCurrentProcessId());
#else
        auto pid = std::int64_t(::getpid());
#endif
        voyemu::write_private_json(root / "identity.json", {{"avd", avd},
                                                            {"token", token},
                                                            {"pid", pid},
                                                            {"port", port},
                                                            {"alive", true},
                                                            {"machine", machine.string()}});
#ifdef _WIN32
        std::unique_ptr<FixtureSdkMedia> sdk_media;

        if (std::filesystem::exists(root / "slow-startup-guest.json")) {
            if (grpc < 1024 || grpc > 65535) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK control port missing");
            }

            sdk_media = std::make_unique<FixtureSdkMedia>(root, grpc, port, pid);
        }
#endif

        if (const char *media = std::getenv("VOYEMU_HOST_WINDOW_RECORD");
            media && std::filesystem::exists(root / "canvas-fixture")) {
            const int width = std::filesystem::exists(root / "canvas-hold") ? 960 : 1920;
            voyemu::write_private_json(
                    media,
                    {{"schema", 1},
                     {"source", "own-window-instrumentation"},
                     {"bundle_identifier", "com.voyemu.emulator"},
                     {"backing_scale", 2},
                     {"content_points", voyemu::Json::array({width, 360})},
                     {"content_subviews",
                      voyemu::Json::array(
                              {{{"class", "EmuGLViewWithMetal"},
                                {"frame_points", voyemu::Json::array({0, 0, width, 360})},
                                {"bounds_points", voyemu::Json::array({0, 0, width, 360})}}})}});
        }

        std::mutex command_mutex;
        auto invoke = [&](std::vector<std::string> action) {
            std::vector<std::string> args{argv[0], "-s", "emulator-" + std::to_string(port)};
            args.insert(args.end(), action.begin(), action.end());
            std::vector<char *> pointers;

            for (auto &arg : args) {
                pointers.push_back(arg.data());
            }

            std::ostringstream captured;
            auto *previous = std::cout.rdbuf(captured.rdbuf());
            struct Restore {
                std::streambuf *value;

                ~Restore() {
                    --framed_depth;
                    std::cout.rdbuf(value);
                }
            } restore{previous};
            ++framed_depth;
            const auto result = fixture_main(static_cast<int>(pointers.size()), pointers.data());

            return fixture_wire::Reply{result, captured.str()};
        };
        auto guest = [&](std::string_view command, bool shell) {
            std::lock_guard lock(command_mutex);
            ++fixture_operation;

            if (command == "root:" && !shell) {
                auto reply = invoke({"root"});
                reply.restart = reply.text.starts_with("restarting adbd as root");

                return reply;
            }

            if (command == "reboot:" && !shell) {
                auto identity = voyemu::read_private_json(root / "identity.json");
                identity["boot_token"] = voyemu::random_token();
                voyemu::write_private_json(root / "identity.json", identity);

                return fixture_wire::Reply{0, {}, true};
            }

            if (command.starts_with("IFS= read -r b < ") ||
                command.starts_with("voyemu_clock_caller()")) {
                return invoke({"shell", std::string(command)});
            }

            std::string output;
            const auto code = fixture_guest_command(argv[0], "emulator-" + std::to_string(port),
                                                    std::string(command), output);

            return fixture_wire::Reply{code, std::move(output)};
        };
        auto console_client = [&](const fixture_wire::Peer &peer) {
            peer.write("Android Console: Authentication required\r\nOK\r\n");

            if (peer.line() != "auth " + console_token) {
                peer.write("KO: authentication required\r\n");

                return;
            }

            peer.write("OK\r\n");
            const auto command = peer.line();
            std::vector<std::string> action{"emu"};

            if (command == "avd name") {
                action.insert(action.end(), {"avd", "name"});
            } else if (command == "avd snapshot save voyemu-last") {
                action.insert(action.end(), {"avd", "snapshot", "save", "voyemu-last"});
            } else if (command == "multidisplay add 1 1920 720 160 0" ||
                       command == "multidisplay add 1 1920 721 160 0") {
                action.push_back("multidisplay");
            } else if (command == "kill") {
                action.push_back("kill");
            } else {
                peer.write("KO: unsupported command\r\n");

                return;
            }

            std::lock_guard lock(command_mutex);
            const auto reply = invoke(std::move(action));

            if (reply.code) {
                peer.write("KO: fixture command failed\r\n");

                return;
            }

            std::string output;

            for (const auto c : reply.text) {
                if (c == '\n') {
                    output += '\r';
                }

                output += c;
            }

            peer.write(output);
        };
        const auto adbd_stop = root / ("adbd-stop-" + std::to_string(pid));
        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);
        {
            auto adb_client = [&](const fixture_wire::Peer &peer) {
                bool recorded = false;
                fixture_wire::adb(peer, [&](std::string_view command, bool shell) {
                    if (!recorded) {
                        voyemu::write_private_json(root / "adbd-routing.json",
                                                   {{"pid", pid}, {"port", port + 1}});
                        recorded = true;
                    }

                    return guest(command, shell);
                });
            };
            std::optional<fixture_wire::Clients> adb_clients(std::in_place);
            fixture_wire::Clients console_clients;

            while (!stopped && !std::filesystem::exists(root / "window-exit") &&
                   !std::filesystem::exists(root / ("emu-kill-" + std::to_string(pid)))) {
                console_clients.accept(console.fd, console_client);

                if (!adb_clients) {
                    for (unsigned i = 0; i < 64; ++i) {
                        fixture_wire::Socket probe(::accept(adb_listener.fd, nullptr, nullptr));

                        if (probe.fd == fixture_wire::invalid) {
                            break;
                        }
                    }
                } else {
                    if (std::filesystem::exists(adbd_stop)) {
                        adb_clients.reset();
                    } else {
                        adb_clients->accept(adb_listener.fd, adb_client);
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            // Keep the owned SDK listeners briefly during teardown to reproduce the actual restart
            // race.
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        // The session terminates the SDK process once its listeners close, so the exit receipts
        // are published while they are still bound; a termination during a later publication
        // would leave a pending state file that refuses the next fixture.
        voyemu::write_private_json(
                root / "identity.json",
                {{"avd", avd}, {"token", token}, {"pid", pid}, {"port", port}, {"alive", false}});
        voyemu::write_private_json(root / "emulator-exit.json",
                                   {{"token", token},
                                    {"pid", pid},
                                    {"cause", stopped ? "owned-stop" : "fixture-window-exit"}});
        console.close();
        adb_listener.close();
#ifdef _WIN32
        sdk_media.reset();
        WSACleanup();
#endif

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FIXTURE_ERROR: " << error.what() << '\n';

        return 1;
    }
}

int main(int argc, char **argv) {
    return fixture_main(argc, argv);
}
