#include "windows_startup.hpp"
#include "session.hpp"
#include "windows_wheel_pair.hpp"
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <thread>
using namespace voyemu;
namespace {
void need(bool value) {
    if (!value) {
        throw std::runtime_error("Windows startup frame contract differs");
    }
}

template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::runtime_error &) {
        return;
    }

    throw std::runtime_error("invalid startup frame accepted");
}

#ifndef _WIN32
std::filesystem::path test_ksh(const char *configured) {
    if (!configured) {
        return "/bin/ksh";
    }

    const std::filesystem::path path(configured);

    if (path.empty() || !std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("VOYEMU_TEST_KSH must name an existing KornShell executable");
    }

    return path;
}
#endif
} // namespace

int main() {
    try {
        {
            struct Event {
                std::uint32_t sequence;
                std::string id, phase, owner;
                std::chrono::steady_clock::time_point accepted_at;
            };
            const auto start = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
            std::deque<Event> events{{1, "volume_down", "down", "own", start}};
            need(windows_wheel_dispatch(events, start + std::chrono::milliseconds(150)).kind ==
                 WindowsWheelDispatch::wait);
            events.push_back(
                    {2, "volume_down", "up", "own", start + std::chrono::milliseconds(150)});
            // The5s transport fence must not become part of the150ms guest hold.
            const auto frozen = windows_wheel_dispatch(events, start + std::chrono::seconds(5));
            need(frozen.kind == WindowsWheelDispatch::pair && frozen.hold_ms == 150);

            for (unsigned field = 0; field < 5; ++field) {
                auto wrong = events;

                if (field == 0) {
                    wrong[1].owner = "foreign";
                }

                if (field == 1) {
                    wrong[1].id = "volume_up";
                }

                if (field == 2) {
                    wrong[1].sequence = 3;
                }

                if (field == 3) {
                    wrong[1].phase = "cancel";
                }

                if (field == 4) {
                    wrong[1].accepted_at = start - std::chrono::milliseconds(1);
                }

                need(windows_wheel_dispatch(wrong, start + std::chrono::seconds(5)).kind ==
                     WindowsWheelDispatch::single);
            }

            need(windows_wheel_dispatch(events, start).kind == WindowsWheelDispatch::single);
            {
                auto wrapped = events;
                wrapped.front().sequence = std::numeric_limits<std::uint32_t>::max();
                wrapped.back().sequence = 0;
                need(windows_wheel_dispatch(wrapped, start + std::chrono::seconds(5)).kind ==
                     WindowsWheelDispatch::single);
            }
            events[1].accepted_at = start + std::chrono::seconds(1);
            need(windows_wheel_dispatch(events, start + std::chrono::seconds(5)).kind ==
                 WindowsWheelDispatch::single);
            events.pop_back();
            need(windows_wheel_dispatch(events, start + std::chrono::milliseconds(200)).kind ==
                 WindowsWheelDispatch::single);
            events.front().id = "phone";
            need(windows_wheel_dispatch(events, start).kind == WindowsWheelDispatch::single);
            events.clear();
            need(windows_wheel_dispatch(events, start).kind == WindowsWheelDispatch::none);
        }
        windows_camera_delivery_guard(false);
        bool camera_blocked = false;
        try {
            windows_camera_delivery_guard(true);
        } catch (const std::exception &error) {
            camera_blocked = std::string(error.what()).find("ANDROID_OWNED_PACKAGE:") == 0;
        }

        need(camera_blocked);
        need(windows_startup_avd_fence_ms(false) == 1500);
        need(windows_startup_avd_fence_ms(true) == 5000);
        need(windows_startup_avd_timeout(true, 60000, true) == 5000);
        need(windows_startup_avd_timeout(true, 7000, true) == 1999);
        need(windows_startup_avd_timeout(false, 2000, true) == 499);
        need(windows_startup_avd_timeout(true, 250, false) == 250);
        rejects([&] {
            windows_startup_avd_timeout(true, 5001, true);
        });
        rejects([&] {
            windows_startup_avd_timeout(false, 0, false);
        });
        // A bound below both fences is raised to the floor, so no guest operation is refused before
        // dispatch.
        need(windows_guest_operation_timeout(1500, false) == 3002);
        need(windows_guest_operation_timeout(1500, true) == 10002);
        need(windows_guest_operation_timeout(60000, false) == 60000);
        need(windows_startup_avd_timeout(false, windows_guest_operation_timeout(1500, false),
                                         true) == 1500);
        {
            const auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
            unsigned captures = 0;
            int captured_timeout = 0;
            auto capture = [&](auto at, auto request_deadline, auto caller_deadline) {
                const auto timeout = windows_android_identity_timeout(at, request_deadline,
                                                                      caller_deadline);
                ++captures;
                captured_timeout = timeout;

                return ProcessResult{0, {}, false};
            };
            capture(now, now + std::chrono::seconds(90), now + std::chrono::seconds(10));
            need(captures == 1 && captured_timeout == 10000);
            capture(now + std::chrono::milliseconds(9750), now + std::chrono::seconds(90),
                    now + std::chrono::seconds(10));
            need(captures == 2 && captured_timeout == 250);
            capture(now, now + std::chrono::seconds(2), now + std::chrono::seconds(10));
            need(captures == 3 && captured_timeout == 2000);
            rejects([&] {
                capture(now + std::chrono::seconds(10), now + std::chrono::seconds(90),
                        now + std::chrono::seconds(10));
            });
            need(captures == 3);
            rejects([&] {
                capture(now + std::chrono::seconds(11), now + std::chrono::seconds(90),
                        now + std::chrono::seconds(10));
            });
            need(captures == 3);
            need(windows_android_identity_timeout(now) == 1500 &&
                 windows_android_identity_timeout(now, now + std::chrono::seconds(90)) == 60000);
        }
        const std::string serial = "emulator-5584";
        auto diagnostic = [&](std::string output, int status = 1, bool timeout = false) {
            return windows_adb_diagnostic({status, std::move(output), timeout}, serial);
        };
        need(diagnostic("restarting adbd as root\r\n", 0).code == "root_restart_requested");
        need(diagnostic("adbd is already running as root\n", 0).code == "root_already");
        need(diagnostic("adbd cannot run as root in production builds\n").code == "root_denied");
        need(diagnostic("error: device offline\n").code == "device_offline");
        need(diagnostic("error: device 'emulator-5584' not found\n").code == "device_missing");
        need(diagnostic("error: closed\n").code == "transport_closed");
        need(diagnostic("restarting adbd as root\nerror: device offline\n").code ==
             "root_restart_transport_unavailable");
        need(diagnostic("", 1, true).code == "timed_out");
        need(diagnostic("").code == "empty");
        need(diagnostic(std::string(4097, 'x')).code == "oversized");
        const std::string secret = "credential-" + std::string(32, 'b') +
                                   "-12345678-1234-1234-1234-123456789abc";

        for (const auto &payload : std::vector<std::string>{
                     secret, "error: device offline\n" + secret,
                     "restarting adbd as root\n" + secret, "prefix device offline suffix",
                     "error: device 'emulator-5586' not found\n",
                     std::string("error: closed\0secret", 20),
                     "error: device offline\nerror: device offline\n"}) {
            const auto value = diagnostic(payload);
            need(value.code == "unknown");
            need(value.code.find(secret) == std::string_view::npos &&
                 value.message.find(secret) == std::string_view::npos);
        }

        const std::string frame(32, 'a'), nonce(32, 'b'),
                boot = "12345678-1234-1234-1234-123456789abc",
                other = "12345678-1234-1234-1234-123456789abd", marker = "VOYEMU_STARTUP_" + frame;
        auto response = [&](std::string before, std::string first, std::string after,
                            std::string last, std::string body = "body", std::string status = "0") {
            return ProcessResult{0,
                                 marker + ":b1:" + before + "\n" + marker + ":n1:" + first + "\n" +
                                         marker + ":body\n" + body + "\n" + marker + ":exit:" +
                                         status + "\n" + marker + ":n2:" + last + "\n" + marker +
                                         ":b2:" + after + "\n" + marker + ":utc:1700000000\n",
                                 false};
        };
        bool unavailable = false;
        try {
            parse_windows_startup_frame({1, "device offline\n", false}, frame, nonce, boot);
        } catch (const std::runtime_error &error) {
            unavailable = std::string_view(error.what()).starts_with("ANDROID_IDENTITY_NOT_READY:");
        }

        need(unavailable);
        need(!parse_windows_reboot_frame({1, "device offline\n", false}, frame, nonce));
        need(!parse_windows_reboot_frame(
                {1, marker + ":b1:" + boot + "\n" + marker + ":n1:" + nonce + "\n", false}, frame,
                nonce));
        const auto rebooted = parse_windows_reboot_frame(response(other, nonce, other, nonce),
                                                         frame, nonce);
        need(rebooted && android_reboot_changed(boot, rebooted->boot_id));
        rejects([&] {
            parse_windows_reboot_frame(response(other, std::string(32, 'f'), other, nonce), frame,
                                       nonce);
        });
        rejects([&] {
            parse_windows_reboot_frame({0, "broken frame", false}, frame, nonce);
        });
        const auto valid = response(boot, nonce, boot, nonce, "Result: Bundle[{json={}}]", "255");
        const auto parsed = parse_windows_startup_frame(valid, frame, nonce, boot);
        need(parsed.boot_id == boot && parsed.utc_seconds == 1700000000 &&
             parsed.result.exit_code == 255 &&
             parsed.result.output == "Result: Bundle[{json={}}]" && !parsed.result.timed_out);
        need(parse_windows_startup_frame(response(other, nonce, other, nonce), frame, nonce, boot)
                     .boot_changed);
        const ProcessResult candidate{
                79, marker + ":b1:" + other + "\n" + marker + ":n1:" + nonce + "\n", false};
        need(parse_windows_startup_frame(candidate, frame, nonce, boot).boot_changed);
        auto clock_failure = [&](const std::string &body) {
            return windows_clock_set_diagnostic(
                    WindowsStartupPhase::clock_set,
                    parse_windows_startup_frame(response(boot, nonce, boot, nonce, body, "255"),
                                                frame, nonce, boot));
        };

        for (const auto &[body, kind] : std::vector<std::pair<std::string, WindowsClockSetFailure>>{
                     {"Permission denied\r\n", WindowsClockSetFailure::permission},
                     {"cmd: Can't find service: alarm\n", WindowsClockSetFailure::service},
                     {"android.os.DeadObjectException\n", WindowsClockSetFailure::binder},
                     {"Unknown command: set-time\n", WindowsClockSetFailure::unknown_command},
                     {"", WindowsClockSetFailure::unclassified}}) {
            const auto value = clock_failure(body);
            need(value && value->kind == kind);
        }

        need(clock_failure("").value().code == "no_diagnostic_text" &&
             clock_failure("").value().message ==
                     "Guest alarm command failed without diagnostic text");
        const std::string private_body =
                "nonce=" + nonce + " token=private-alarm-credential https://private.invalid/secret";

        for (const auto &body : std::vector<std::string>{
                     private_body, "Permission denied " + private_body,
                     "Permission denied\n" + private_body,
                     "cmd: Can't find service: alarm\n" + private_body,
                     "android.os.DeadObjectException: " + private_body,
                     "Unknown command: set-time " + private_body,
                     std::string("Permission denied\0", 18), std::string(4097, 'x')}) {
            const auto value = clock_failure(body);
            need(value && value->kind == WindowsClockSetFailure::unclassified &&
                 value->code == "unclassified");
            need(value.value().code.find(nonce) == std::string_view::npos &&
                 value.value().message.find(nonce) == std::string_view::npos &&
                 value.value().message.find("private-alarm") == std::string_view::npos &&
                 value.value().message.find("https:") == std::string_view::npos);
        }

        const auto failed_clock = parse_windows_startup_frame(
                response(boot, nonce, boot, nonce, "Permission denied", "255"), frame, nonce, boot);

        for (unsigned phase = 0; phase <= static_cast<unsigned>(WindowsStartupPhase::clock_service);
             ++phase) {
            if (phase != static_cast<unsigned>(WindowsStartupPhase::clock_set)) {
                need(!windows_clock_set_diagnostic(static_cast<WindowsStartupPhase>(phase),
                                                   failed_clock));
            }
        }

        auto successful_clock = failed_clock;
        successful_clock.result.exit_code = 0;
        need(!windows_clock_set_diagnostic(WindowsStartupPhase::clock_set, successful_clock));
        auto changed_clock = failed_clock;
        changed_clock.boot_changed = true;
        need(!windows_clock_set_diagnostic(WindowsStartupPhase::clock_set, changed_clock));
        auto timed_clock = failed_clock;
        timed_clock.result.timed_out = true;
        need(!windows_clock_set_diagnostic(WindowsStartupPhase::clock_set, timed_clock));
        auto diagnostic_from_frame = [&](const ProcessResult &raw) {
            return windows_clock_set_diagnostic(
                    WindowsStartupPhase::clock_set,
                    parse_windows_startup_frame(raw, frame, nonce, boot));
        };
        rejects([&] {
            diagnostic_from_frame(
                    response(boot, nonce, boot, std::string(32, 'c'), "Permission denied", "255"));
        });
        rejects([&] {
            diagnostic_from_frame({0, "Permission denied\n", false});
        });
        auto incomplete_clock = response(boot, nonce, boot, nonce, "Permission denied", "255");
        incomplete_clock.output.resize(incomplete_clock.output.find(marker + ":n2:"));
        rejects([&] {
            diagnostic_from_frame(incomplete_clock);
        });
        auto timeout_clock = response(boot, nonce, boot, nonce, "Permission denied", "255");
        timeout_clock.timed_out = true;
        rejects([&] {
            diagnostic_from_frame(timeout_clock);
        });
        // Candidate headers cannot complete recovery: its new pin requires a full frame.
        rejects([&] {
            parse_windows_startup_frame(candidate, frame, nonce, other);
        });
        const auto confirmed = parse_windows_startup_frame(response(other, nonce, other, nonce),
                                                           frame, nonce, other);
        need(!confirmed.boot_changed && confirmed.boot_id == other);

        rejects([&] {
            parse_windows_startup_frame(response(boot, std::string(32, 'c'), boot, nonce), frame,
                                        nonce, boot);
        });
        rejects([&] {
            parse_windows_startup_frame(response(boot, "", boot, ""), frame, nonce, boot);
        });
        need(parse_windows_startup_frame(response(boot, nonce, other, nonce), frame, nonce, boot)
                     .boot_changed);
        rejects([&] {
            parse_windows_startup_frame(response(boot, nonce, boot, std::string(32, 'c')), frame,
                                        nonce, boot);
        });
        rejects([&] {
            parse_windows_startup_frame(response("invalid", nonce, "invalid", nonce), frame, nonce,
                                        {});
        });
        auto invalid = valid;
        invalid.output += "extra";
        rejects([&] {
            parse_windows_startup_frame(invalid, frame, nonce, boot);
        });
        invalid = valid;
        invalid.output.pop_back();
        rejects([&] {
            parse_windows_startup_frame(invalid, frame, nonce, boot);
        });
        invalid = valid;
        invalid.timed_out = true;
        rejects([&] {
            parse_windows_startup_frame(invalid, frame, nonce, boot);
        });
        invalid = valid;
        invalid.output.resize(512 * 1024 + 1, 'x');
        rejects([&] {
            parse_windows_startup_frame(invalid, frame, nonce, boot);
        });
        rejects([&] {
            parse_windows_startup_frame(response(boot, nonce, boot, nonce, "body", "256"), frame,
                                        nonce, boot);
        });
        rejects([&] {
            parse_windows_startup_frame(response(boot, nonce, boot, nonce, "body", "0junk"), frame,
                                        nonce, boot);
        });
        rejects([&] {
            parse_windows_startup_frame(response(boot, nonce, boot, nonce, marker), frame, nonce,
                                        boot);
        });
        rejects([&] {
            windows_startup_command({"sync"}, frame, nonce, {}, true);
        });
        rejects([&] {
            windows_startup_command({"sync"}, "invalid", nonce, boot, true);
        });
        const auto script = windows_startup_command({"cmd", "alarm", "set-time", "1234"}, frame,
                                                    nonce, boot, true);
        need(script.find("[ \"$n\" = ") < script.find("'cmd' 'alarm' 'set-time' '1234'"));
        need(script.find("[ \"$b\" = ") < script.find("'cmd' 'alarm' 'set-time' '1234'"));
        // Captured unchanged from failed current Wine/native A9 startup: inner exit only.
        const std::string captured_nested_exit =
                "VOYEMU_STARTUP_8c9e2d2055e89f1446ab1cd36b6ace5b:b1:9f2bba6e-e4fd-480f-9dcc-244d113a4f53\r\nVOYEMU_STARTUP_8c9e2d2055e89f1446ab1cd36b6ace5b:n1:c3333aea6f8e77a28fe485072d0fe5d3\r\nVOYEMU_STARTUP_8c9e2d2055e89f1446ab1cd36b6ace5b:body\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:b1:9f2bba6e-e4fd-480f-9dcc-244d113a4f53\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:n1:c3333aea6f8e77a28fe485072d0fe5d3\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:body\r\npackage:/data/app/com.qinggan.canbus.service-Plx5CIIM7cKqksIFDFAOXw==/base.apk\r\n\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:exit:0\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:n2:c3333aea6f8e77a28fe485072d0fe5d3\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:b2:9f2bba6e-e4fd-480f-9dcc-244d113a4f53\r\nVOYEMU_STARTUP_b2b1b26054a5d0f2e8089fab03827097:utc:1793789639\r\n";
        rejects([&] {
            parse_windows_startup_frame(
                    {0, captured_nested_exit, false}, "8c9e2d2055e89f1446ab1cd36b6ace5b",
                    "c3333aea6f8e77a28fe485072d0fe5d3", "9f2bba6e-e4fd-480f-9dcc-244d113a4f53");
        });
#ifndef _WIN32
        // The fences require KornShell; an explicitly configured missing shell is an error.
        need(test_ksh(nullptr) == "/bin/ksh");
        rejects([] {
            test_ksh("");
        });
        rejects([] {
            test_ksh("/voyemu-missing-test-ksh");
        });
        const auto ksh = test_ksh(std::getenv("VOYEMU_TEST_KSH"));

        if (!std::filesystem::exists(ksh)) {
            std::cout
                    << "Windows startup portable guards passed; ksh fences skipped without /bin/ksh\n";

            return 77;
        }

        // Execute both fences with cat unavailable: neither boot read needs a guest utility.
        const auto slot =
                std::filesystem::temp_directory_path() /
                ("voyemu-startup-read-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        need(std::filesystem::create_directory(slot));
        struct RemoveSlot {
            std::filesystem::path path;

            ~RemoveSlot() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{slot};
        const auto selected_ksh = slot / "selected-ksh";
        std::filesystem::create_symlink(std::filesystem::absolute(ksh), selected_ksh);
        need(test_ksh(selected_ksh.c_str()) == selected_ksh);
        ProcessSpec selected_shell;
        selected_shell.executable = test_ksh(selected_ksh.c_str());
        selected_shell.arguments = {"-c", "print -r -- selected-ksh"};
        const auto selected_result = execute(selected_shell, 2000);
        need(selected_result.exit_code == 0 && !selected_result.timed_out &&
             selected_result.output == "selected-ksh\n");
        const auto boot_file = slot / "boot";
        {
            std::ofstream output(boot_file);
            output << boot << '\n';
        }
        auto generated = windows_startup_command({"id", "-u"}, frame, nonce, boot, false);
        const std::string original_path = "/proc/sys/kernel/random/boot_id";

        for (std::size_t position = 0;
             (position = generated.find(original_path, position)) != std::string::npos;
             position += boot_file.string().size()) {
            generated.replace(position, original_path.size(), boot_file.string());
        }

        auto run_fence = [&](std::string observed_nonce) {
            ProcessSpec spec;
            spec.executable = ksh;
            spec.arguments = {
                    "-c",
                    "cat(){ return 97; }; getprop(){ printf '%s\\n' '" + observed_nonce +
                            "'; }; id(){ printf '2000\\n'; }; EPOCHREALTIME=1700000000.123456; " +
                            generated};

            return execute(spec, 2000);
        };
        const auto executed = run_fence(nonce);
        need(executed.exit_code == 0 && !executed.timed_out);
        need(parse_windows_startup_frame(executed, frame, nonce, boot).result.output == "2000\n");
        // Real A9 Windows startup nested an already-framed pm-path command. Its exit
        // must terminate only the action, so both ownership fences still complete.
        auto run_action = [&](const std::string &action) {
            auto command = windows_startup_command({action}, frame, nonce, boot, false, {}, true);

            for (std::size_t position = 0;
                 (position = command.find(original_path, position)) != std::string::npos;
                 position += boot_file.string().size()) {
                command.replace(position, original_path.size(), boot_file.string());
            }

            ProcessSpec spec;
            spec.executable = ksh;
            spec.arguments = {"-c", "getprop(){ print -r -- " + android_shell_quote(nonce) +
                                            "; }; EPOCHREALTIME=1700000000.123456; " + command};

            return execute(spec, 2000);
        };

        for (const auto status : {0, 23}) {
            const auto captured =
                    run_action("print -r -- action-output; exit " + std::to_string(status));
            need(captured.exit_code == 0 && !captured.timed_out);
            const auto parsed = parse_windows_startup_frame(captured, frame, nonce, boot);
            need(parsed.result.exit_code == status && parsed.result.output == "action-output\n");
        }

        const auto inner_frame = std::string(32, 'd');
        const auto inner = windows_startup_command({"print -r -- nested-output; exit 7"},
                                                   inner_frame, nonce, boot, false, {}, true);
        const auto nested = parse_windows_startup_frame(run_action(inner), frame, nonce, boot);
        const auto inner_result = parse_windows_startup_frame(nested.result, inner_frame, nonce,
                                                              boot);
        need(nested.result.exit_code == 0 && inner_result.result.exit_code == 7 &&
             inner_result.result.output == "nested-output\n");
        {
            const WheelEvent down{1, "volume_down", "down", "own", 0, 0},
                    up{2, "volume_down", "up", "own", 0, 0};
            auto command = android_short_wheel_pair_command(
                    nonce, std::string(32, 'a'), boot, down, up, 150,
                    AndroidShortWheelContract::qinggan09, nonce);
            need(command.find_first_of("\r\n") == std::string::npos);
            (void)windows_startup_command({command}, frame, nonce, boot, true, {}, true);

            for (std::size_t position = 0;
                 (position = command.find(original_path, position)) != std::string::npos;
                 position += boot_file.string().size()) {
                command.replace(position, original_path.size(), boot_file.string());
            }

            const auto released = slot / "pair-released", slept = slot / "pair-sleep",
                       pressed = slot / "pair-pressed";
            auto pair_run = [&](bool malformed, bool foreign_after_down, bool boot_changed = false,
                                bool down_failure = false) {
                std::filesystem::remove(released);
                std::filesystem::remove(slept);
                std::filesystem::remove(pressed);
                {
                    std::ofstream output(boot_file);
                    output << boot << '\n';
                }
                ProcessSpec spec;
                spec.executable = ksh;
                const auto response = malformed ? std::string("MALFORMED-DOWN")
                                                : Json{{"schema", 1},     {"session_id", nonce},
                                                       {"sequence", 1},   {"button", "volume_down"},
                                                       {"phase", "down"}, {"status", "no-consumer"}}
                                                          .dump();
                spec.arguments = {
                        "-c",
                        "getprop(){ if " +
                                std::string(
                                        foreign_after_down
                                                ? "[ -e " + android_shell_quote(pressed.string()) +
                                                          " ]"
                                                : "false") +
                                "; then print -r -- foreign; else print -r -- " +
                                android_shell_quote(nonce) +
                                "; fi; }; sleep(){ print -r -- \"$1\" > " +
                                android_shell_quote(slept.string()) +
                                "; }; content(){ case \"$*\" in *phase:s:down*) : > " +
                                android_shell_quote(pressed.string()) + "; print -r -- " +
                                android_shell_quote(response) + "; " +
                                (boot_changed
                                         ? "print -r -- " + android_shell_quote(other) + " > " +
                                                   android_shell_quote(boot_file.string()) + "; "
                                         : "") +
                                (down_failure ? "return 23; " : "") + ";; *phase:s:up*) : > " +
                                android_shell_quote(released.string()) +
                                "; print -r -- UP;; *) return 9;; esac; }; " + command};

                return execute(spec, 2000);
            };

            for (bool malformed : {false, true}) {
                const auto result = pair_run(malformed, false);
                need(result.exit_code == 0 && !result.timed_out &&
                     std::filesystem::exists(released));
                std::ifstream input(slept);
                std::string interval;
                std::getline(input, interval);
                need(interval == "0.150000");
                need(result.output.find("VOYEMU_UP\nUP") != std::string::npos);
            }

            const auto foreign = pair_run(false, true);
            need(foreign.exit_code == 79 && !std::filesystem::exists(released));
            const auto changed_boot = pair_run(false, false, true);
            need(changed_boot.exit_code == 79 && !std::filesystem::exists(released));
            const auto failed_down = pair_run(false, false, false, true);
            need(failed_down.exit_code != 0 && std::filesystem::exists(released));
            {
                std::ofstream output(boot_file);
                output << boot << '\n';
            }
            rejects([&] {
                android_short_wheel_pair_command(nonce, std::string(32, 'a'), "invalid", down, up,
                                                 150, AndroidShortWheelContract::qinggan09, nonce);
            });
        }
        // Android R56 has no printf builtin. Count actual external utility calls;
        // marker emission must use print's builtin rather than hide cost behind host printf.
        const auto external_calls = slot / "external-printf";
        ProcessSpec marker_contract;
        marker_contract.executable = ksh;
        marker_contract.arguments = {
                "-c", "printf(){ print -r -- call >> " +
                              android_shell_quote(external_calls.string()) +
                              "; /usr/bin/printf \"$@\"; }; getprop(){ print -r -- " +
                              android_shell_quote(nonce) +
                              "; }; id(){ print -r -- 2000; }; EPOCHREALTIME=1700000000.123456; " +
                              generated};
        const auto marker_result = execute(marker_contract, 2000);
        need(marker_result.exit_code == 0 && !marker_result.timed_out &&
             marker_result.output == executed.output);

        if (std::filesystem::exists(external_calls)) {
            throw std::runtime_error("startup frame invoked external printf for marker emission");
        }

        // Host KSH93 injects the Android R56 special variable; this is a byte contract,
        // not execution of Android's gettimeofday-backed EPOCHREALTIME implementation.
        const auto date_calls = slot / "external-date";
        auto run_epoch = [&](const std::string &assignment) {
            ProcessSpec spec;
            spec.executable = ksh;
            spec.arguments = {"-c", assignment + "; date(){ print -r -- call >> " +
                                            android_shell_quote(date_calls.string()) +
                                            "; return 97; }; getprop(){ print -r -- " +
                                            android_shell_quote(nonce) +
                                            "; }; id(){ print -r -- 2000; }; " + generated};

            return execute(spec, 2000);
        };
        const auto builtin_epoch = run_epoch("EPOCHREALTIME=1700000000.123456");

        if (builtin_epoch.exit_code || builtin_epoch.timed_out ||
            std::filesystem::exists(date_calls)) {
            throw std::runtime_error("startup UTC tail invoked external date");
        }

        need(builtin_epoch.output == executed.output &&
             parse_windows_startup_frame(builtin_epoch, frame, nonce, boot).utc_seconds ==
                     1700000000);

        for (const auto *assignment :
             {"unset EPOCHREALTIME", "EPOCHREALTIME=garbage.123456", "EPOCHREALTIME=-1.123456"}) {
            const auto rejected = run_epoch(assignment);
            need(rejected.exit_code == 78 && !rejected.timed_out &&
                 rejected.output.find(marker + ":utc:") == std::string::npos);
            rejects([&] {
                parse_windows_startup_frame(rejected, frame, nonce, boot);
            });
            need(!std::filesystem::exists(date_calls));
        }

        // A blocked post-command nonce read must expose body completion without
        // granting authority to its still-incomplete frame. Completed bytes stay exact.
        const auto seen = slot / "getprop-seen", release = slot / "getprop-release";
        ProcessSpec delayed;
        delayed.executable = ksh;
        delayed.output = slot / "delayed.log";
        delayed.arguments = {
                "-c", "getprop(){ if [ -e " + android_shell_quote(seen.string()) +
                              " ]; then i=0; while [ ! -e " +
                              android_shell_quote(release.string()) +
                              " ] && [ $i -lt 250 ]; do sleep .01; i=$((i+1)); done; fi; : > " +
                              android_shell_quote(seen.string()) + "; printf '%s\n' '" + nonce +
                              "'; }; id(){ printf '2000\n'; }; EPOCHREALTIME=1700000000.123456; " +
                              generated};
        auto child = ChildProcess::launch(delayed);
        auto captured = [&] {
            std::ifstream input(delayed.output);

            return std::string{std::istreambuf_iterator<char>(input), {}};
        };
        std::string partial;
        const auto hold_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1600);

        while (std::chrono::steady_clock::now() < hold_until) {
            partial = captured();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const bool progress_visible = partial.find(marker + ":exit:0\n") != std::string::npos;
        const bool before_tail = partial.find(marker + ":n2:") == std::string::npos;
        { std::ofstream(release) << "release"; }
        const auto finish = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        while (child.alive() && std::chrono::steady_clock::now() < finish) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        need(!child.alive() && child.exit_code() == 0);
        const auto completed = captured();
        need(completed == executed.output);
        rejects([&] {
            parse_windows_startup_frame({0, partial, false}, frame, nonce, boot);
        });

        if (!progress_visible || !before_tail) {
            throw std::runtime_error(
                    "post-command progress marker missing while second getprop is blocked");
        }

        const auto first_seen = slot / "first-seen", first_release = slot / "first-release";
        ProcessSpec first;
        first.executable = ksh;
        first.output = slot / "first.log";
        first.arguments = {
                "-c", "getprop(){ if [ ! -e " + android_shell_quote(first_seen.string()) +
                              " ]; then i=0; while [ ! -e " +
                              android_shell_quote(first_release.string()) +
                              " ] && [ $i -lt 250 ]; do sleep .01; i=$((i+1)); done; fi; : > " +
                              android_shell_quote(first_seen.string()) + "; printf '%s\n' '" +
                              nonce +
                              "'; }; id(){ printf '2000\n'; }; EPOCHREALTIME=1700000000.123456; " +
                              generated};
        auto first_child = ChildProcess::launch(first);
        auto first_capture = [&] {
            std::ifstream input(first.output);

            return std::string{std::istreambuf_iterator<char>(input), {}};
        };
        std::string first_partial;
        const auto first_hold = std::chrono::steady_clock::now() + std::chrono::milliseconds(1600);

        while (std::chrono::steady_clock::now() < first_hold) {
            first_partial = first_capture();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const bool boot_progress = first_partial.find(marker + ":b1:" + boot + "\n") !=
                                   std::string::npos;
        const bool before_nonce = first_partial.find(marker + ":n1:") == std::string::npos;
        { std::ofstream(first_release) << "release"; }
        const auto first_end = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        while (first_child.alive() && std::chrono::steady_clock::now() < first_end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        need(!first_child.alive() && first_child.exit_code() == 0);
        need(first_capture() == executed.output);
        rejects([&] {
            parse_windows_startup_frame({0, first_partial, false}, frame, nonce, boot);
        });

        if (!boot_progress || !before_nonce) {
            throw std::runtime_error("pre-fence boot progress missing while first getprop blocked");
        }

        const auto foreign = run_fence(std::string(32, 'c'));
        need(foreign.exit_code == 79 && foreign.output.find(marker + ":body") == std::string::npos);
        std::filesystem::remove(boot_file);
        need(run_fence(nonce).exit_code == 78);
#endif
        const auto sections = windows_startup_sections("VOYEMU_SECTION_" + frame +
                                                               ":display\nfirst\nVOYEMU_SECTION_" +
                                                               frame + ":windows\nsecond\n",
                                                       frame, {"display", "windows"});
        need(sections == std::vector<std::string>{"first\n", "second\n"});
        rejects([&] {
            windows_startup_sections("VOYEMU_SECTION_" + frame + ":windows\nwrong", frame,
                                     {"display", "windows"});
        });
        rejects([&] {
            windows_startup_sections("VOYEMU_SECTION_" + frame + ":display\nVOYEMU_SECTION_" +
                                             frame + ":display\nduplicate",
                                     frame, {"display"});
        });
        need(windows_alarm_service_available({0, "Service alarm: found\n", false}));
        need(!windows_alarm_service_available({0, "Service alarm: not found\r\n", false}));

        for (const auto *text : {"Service alarm2: found\n", "Service alarm: found\nextra", "",
                                 "Service alarm: not found\nService alarm: found\n"}) {
            rejects([&] {
                windows_alarm_service_available({0, text, false});
            });
        }

        rejects([&] {
            windows_alarm_service_available({20, "Service alarm: found\n", false});
        });
        rejects([&] {
            windows_alarm_service_available({0, "Service alarm: found\n", true});
        });
        {
            static_assert(static_cast<int>(WindowsStartupPhase::identity) == 0);
            static_assert(static_cast<int>(WindowsStartupPhase::wheel_bootstrap) == 11);
            static_assert(static_cast<int>(WindowsStartupPhase::clock_service) == 35);
            static_assert(static_cast<int>(WindowsStartupPhase::framework_ready) == 36);
            need(WindowsAndroidTransaction::startup_budget == std::chrono::seconds(1800));
            const std::string ready =
                    "activity:Service activity: found\npackage:Service package: found\nboot:1\n";
            const auto command = windows_framework_probe_command();
            need(command.find("content") == std::string::npos &&
                 command.find("bootstrap") == std::string::npos &&
                 command.find("service check activity") != std::string::npos &&
                 command.find("service check package") != std::string::npos);
            WindowsAndroidTransaction t;
            t.phase = WindowsStartupPhase::framework_ready;
            unsigned protected_calls = 0, clock_calls = 0;

            for (int repeat = 0; repeat < 3; ++repeat) {
                for (auto p : std::vector<ProcessResult>{
                             {0,
                              "activity:Service activity: not found\npackage:Service package: found\nboot:1\n",
                              false},
                             {0,
                              "activity:Service activity: found\npackage:Service package: not found\nboot:1\n",
                              false},
                             {0,
                              "activity:Service activity: found\npackage:Service package: found\nboot:0\n",
                              false},
                             {0,
                              "activity:Service activity: found\npackage:Service package: found\nboot:\n",
                              false},
                             {1, {}, true}}) {
                    const auto observed = windows_framework_readiness(p);
                    need(observed.state == WindowsFrameworkState::pending);
                    t.phase = windows_framework_next(observed);

                    if (t.phase == WindowsStartupPhase::wheel_bootstrap) {
                        ++protected_calls;
                    }

                    if (t.phase == WindowsStartupPhase::clock_read) {
                        ++clock_calls;
                    }

                    need(t.phase == WindowsStartupPhase::framework_ready);
                }
            }

            need(protected_calls == 0 && clock_calls == 0);
            need(windows_framework_after_clock(false, true, false) ==
                 WindowsStartupPhase::framework_ready);
            t.phase = windows_framework_next(windows_framework_readiness({0, ready, false}));
            need(t.phase == WindowsStartupPhase::clock_read);
            ++clock_calls;
            need(clock_calls == 1);
            t.phase = windows_framework_after_clock(true, true, false);

            if (t.phase == WindowsStartupPhase::wheel_bootstrap) {
                ++protected_calls;
            }

            t.attempted_mutations.insert("wheel-bootstrap");
            t.phase = WindowsStartupPhase::wheel_verify;
            need(protected_calls == 1);
            need(windows_framework_after_clock(true, true, true) ==
                 WindowsStartupPhase::wheel_verify);

            for (auto text : std::vector<std::string>{
                         "", ready + "extra", std::string(4097, 'x'),
                         "activity:Service package: found\npackage:Service activity: found\nboot:1\n",
                         "activity:Service activity: found\npackage:Service package: found\nboot:true\n"}) {
                auto r = windows_framework_readiness({0, text, false});
                need(r.state == WindowsFrameworkState::invalid);
                bool rejected = false;
                try {
                    (void)windows_framework_next(r);
                } catch (...) {
                    rejected = true;
                }

                need(rejected);
            }

            auto crlf = ready;

            for (std::size_t i = 0; (i = crlf.find('\n', i)) != std::string::npos; i += 2) {
                crlf.insert(i, "\r");
            }

            need(windows_framework_readiness({0, crlf, false}).state ==
                 WindowsFrameworkState::ready);
            need(windows_framework_after_clock(true, false, false) ==
                 WindowsStartupPhase::state_prior);
        }
        {
            // Guard-stage reachability and ordering contract for the macOS-equivalent startup
            // services.
            need(!windows_startup_services_required(Json::object()));
            need(!windows_startup_services_required(
                    Json::parse(R"({"features":{"MultiDisplay":"on"}})")));
            need(windows_startup_services_required(
                    Json::parse(R"({"qinggan09_owned_packages":{"schema":1}})")));
            need(windows_startup_services_required(Json::parse(
                    R"({"audio_startup":{"authority":"com.voyemu.output.audio.startup"}})")));
            need(windows_startup_services_required(
                    Json::parse(R"({"interguest_network":{"transport":"tap-ram-v1"}})")));
            need(windows_after_framework_ready(true) == WindowsStartupPhase::startup_audio);
            need(windows_after_framework_ready(false) == WindowsStartupPhase::clock_read);
            need(windows_after_startup_audio(true) == WindowsStartupPhase::startup_interguest);
            need(windows_after_startup_audio(false) == WindowsStartupPhase::clock_read);
            static_assert(static_cast<int>(WindowsStartupPhase::framework_ready) == 36);
            static_assert(static_cast<int>(WindowsStartupPhase::startup_audio) == 37);
            static_assert(static_cast<int>(WindowsStartupPhase::startup_interguest) == 38);
        }
        WindowsAndroidTransaction transaction;
        const auto now = WindowsAndroidTransaction::Clock::now();
        transaction.initialized = true;
        transaction.started = now;
        need(!transaction.startup_expired(now + std::chrono::seconds(1799)));
        need(transaction.startup_expired(now + std::chrono::seconds(1800)));
        transaction.ever_ready = true;
        transaction.last_observation = now;
        need(transaction.observation_current(now + std::chrono::seconds(63)));
        need(!transaction.observation_current(now + std::chrono::seconds(64)));
        need(!transaction.startup_expired(now + std::chrono::hours(2)));
        need(android_clock_current(1700000000, 1700000000500));
        need(!android_clock_current(1699999990, 1700000000500));
        const WindowsAndroidOwner owner{std::string(32, 'a'), 7, 123, "route"};
        auto reboot_pin = [&] {
            WindowsAndroidTransaction value;
            value.owner = owner;
            value.nonce = nonce;
            value.boot = boot;
            value.reboot_confirmed = true;

            return value;
        };
        auto pinned = reboot_pin();
        pinned.initialize(owner, nonce, now);
        need(pinned.boot == boot && pinned.initialized && !pinned.reboot_confirmed);

        for (unsigned field = 0; field < 4; ++field) {
            auto changed = owner;

            if (field == 0) {
                changed.epoch = std::string(32, 'c');
            }

            if (field == 1) {
                ++changed.generation;
            }

            if (field == 2) {
                ++changed.sdk_pid;
            }

            if (field == 3) {
                changed.route = "other";
            }

            auto value = reboot_pin();
            value.initialize(changed, nonce, now);
            need(value.boot.empty());
        }

        auto wrong_nonce = reboot_pin();
        wrong_nonce.initialize(owner, std::string(32, 'c'), now);
        need(wrong_nonce.boot.empty());
        auto unconfirmed = reboot_pin();
        unconfirmed.reboot_confirmed = false;
        unconfirmed.initialize(owner, nonce, now);
        need(unconfirmed.boot.empty());
        std::cout << "Windows startup framed identity/status and pre-write guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
