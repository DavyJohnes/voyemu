#pragma once
#include "android_clock.hpp"
#include "android_reboot.hpp"
#include "check.hpp"
#include "model.hpp"
#include "process.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
inline void windows_camera_delivery_guard(bool camera_package) {
    if (camera_package) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE",
                          "Camera delivery is not supported by Windows startup transaction");
    }
}

inline int windows_startup_avd_fence_ms(bool native_diagnostic) {
    return native_diagnostic ? 5000 : 1500;
}

// A framed guest operation reserves one AVD identity fence before dispatch and one after it, so a
// caller bound below that floor can never complete: the floor becomes the effective operation
// bound.
inline int windows_guest_operation_timeout(int timeout_ms, bool native_diagnostic) {
    const auto floor = std::int64_t(2) * windows_startup_avd_fence_ms(native_diagnostic) + 2;

    return static_cast<int>(std::max<std::int64_t>(timeout_ms, floor));
}

inline int windows_startup_avd_timeout(bool native_diagnostic, std::int64_t remaining,
                                       bool reserve_post_fence) {
    const auto fence = windows_startup_avd_fence_ms(native_diagnostic);
    const auto available = remaining - (reserve_post_fence ? fence + 1 : 0);

    if (available < 1) {
        throw VoyemuError("ANDROID_STARTUP_DEADLINE", "no budget for complete owned action");
    }

    return static_cast<int>(std::min<std::int64_t>(fence, available));
}

inline int
windows_android_identity_timeout(std::chrono::steady_clock::time_point now,
                                 std::chrono::steady_clock::time_point request_deadline = {},
                                 std::chrono::steady_clock::time_point caller_deadline = {}) {
    auto deadline = request_deadline == std::chrono::steady_clock::time_point{}
                            ? now + std::chrono::milliseconds(1500)
                            : request_deadline;

    if (caller_deadline != std::chrono::steady_clock::time_point{}) {
        deadline = std::min(deadline, caller_deadline);
    }

    const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

    if (remaining < 1) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY", "no remaining operation identity budget");
    }

    return static_cast<int>(std::min<std::int64_t>(remaining, 60000));
}

// Diagnostics are fixed strings only. Unrecognized/extra lines never escape as payload.
struct WindowsAdbDiagnostic {
    std::string_view code, message;
};

inline WindowsAdbDiagnostic windows_adb_diagnostic(const ProcessResult &result,
                                                   std::string_view serial) {
    if (result.timed_out) {
        return {"timed_out", "Retained ADB client timed out"};
    }

    if (result.output.size() > 4096) {
        return {"oversized", "ADB response exceeds diagnostic bound"};
    }

    if (result.output.empty()) {
        return {"empty", "ADB response is empty"};
    }

    bool restart = false, already = false, denied = false, offline = false, missing = false,
         closed = false;
    unsigned lines = 0;
    std::string_view remaining = result.output;

    while (!remaining.empty()) {
        const auto end = remaining.find('\n');
        auto line = remaining.substr(0, end);
        remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end + 1);

        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (++lines > 3 || line.empty()) {
            return {"unknown", "Unclassified ADB response; payload not persisted"};
        }

        if (line == "restarting adbd as root") {
            restart = true;
        } else if (line == "adbd is already running as root") {
            already = true;
        } else if (line == "adbd cannot run as root in production builds") {
            denied = true;
        } else if (line == "error: device offline" || line == "adb: error: device offline" ||
                   line == "adb: device offline") {
            offline = true;
        } else if (line == "error: no devices/emulators found" ||
                   line == "error: device '" + std::string(serial) + "' not found" ||
                   line == "adb: error: device '" + std::string(serial) + "' not found") {
            missing = true;
        } else if (line == "error: closed" || line == "adb: error: closed") {
            closed = true;
        } else {
            return {"unknown", "Unclassified ADB response; payload not persisted"};
        }
    }

    const unsigned kinds = unsigned(restart) + unsigned(already) + unsigned(denied) +
                           unsigned(offline) + unsigned(missing) + unsigned(closed);

    if (restart && kinds == 2 && (offline || missing || closed)) {
        return {"root_restart_transport_unavailable",
                "ADB root restart reported; transport unavailable"};
    }

    if (kinds != 1 || lines != 1) {
        return {"unknown", "Unclassified ADB response; payload not persisted"};
    }

    if (restart) {
        return {"root_restart_requested", "ADB reported root restart"};
    }

    if (already) {
        return {"root_already", "ADB reported already root"};
    }

    if (denied) {
        return {"root_denied", "ADB reported production root unavailable"};
    }

    if (offline) {
        return {"device_offline", "ADB reported device offline"};
    }

    if (missing) {
        return {"device_missing", "ADB reported target device unavailable"};
    }

    return {"transport_closed", "ADB reported closed transport"};
}

struct WindowsStartupFrame {
    std::string boot_id, nonce;
    std::int64_t utc_seconds = 0;
    ProcessResult result;
    bool boot_changed = false;
};

inline bool windows_startup_token(std::string_view value) {
    return hex_text(value, 32);
}

// A mutation is guarded inside the guest shell, before the first side effect.
inline std::string windows_startup_command(const std::vector<std::string> &arguments,
                                           std::string_view frame, std::string_view nonce,
                                           std::string_view boot, bool mutation,
                                           std::string_view alternate_nonce = {},
                                           bool joined_shell = false) {
    if (arguments.empty() || !windows_startup_token(frame) || !windows_startup_token(nonce) ||
        (!boot.empty() && !android_boot_id_valid(boot)) || (mutation && boot.empty()) ||
        (!alternate_nonce.empty() && (!windows_startup_token(alternate_nonce) || mutation))) {
        throw VoyemuError("ANDROID_STARTUP_ARGUMENT", "fixed frame identity required");
    }

    const std::string marker = "VOYEMU_STARTUP_" + std::string(frame);
    std::string command;

    if (joined_shell) {
        if (arguments.size() != 1 || arguments.front().find_first_of("\r\n") != std::string::npos ||
            arguments.front().find(char(0)) != std::string::npos) {
            throw VoyemuError("ANDROID_STARTUP_ARGUMENT", "bounded joined shell required");
        }

        command = arguments.front();
    } else {
        for (const auto &argument : arguments) {
            if (!command.empty()) {
                command += ' ';
            }

            command += android_shell_quote(argument);
        }
    }

    const auto quoted_marker = android_shell_quote(marker),
               quoted_nonce = android_shell_quote(std::string(nonce));
    std::string script = "IFS= read -r b < /proc/sys/kernel/random/boot_id || exit 78; ";
    script += "print -r -- " + quoted_marker +
              ":b1:\"$b\"; n=$(getprop qemu.voyemu.token) || exit 78; print -r -- " +
              quoted_marker + ":n1:\"$n\"; ";
    script += "[ \"$n\" = " + quoted_nonce + " ]";

    if (!alternate_nonce.empty()) {
        script += " || [ \"$n\" = " + android_shell_quote(std::string(alternate_nonce)) + " ]";
    }

    script += " || exit 79; ";

    if (!boot.empty()) {
        script += "[ \"$b\" = " + android_shell_quote(std::string(boot)) + " ] || exit 79; ";
    }

    script += "print -r -- " + quoted_marker + ":body; ( " + command + " ); r=$?; ";
    script += "print; print -r -- " + quoted_marker + ":exit:\"$r\"; ";
    script += "n=$(getprop qemu.voyemu.token) || exit 78; print -r -- " + quoted_marker +
              ":n2:\"$n\"; ";
    script += "IFS= read -r b < /proc/sys/kernel/random/boot_id || exit 78; print -r -- " +
              quoted_marker + ":b2:\"$b\"; ";
    script +=
            "u=$EPOCHREALTIME; u=${u%%.*}; case \"$u\" in \"\"|*[!0-9]*) exit 78;; esac; print -r -- " +
            quoted_marker + ":utc:\"$u\"; exit 0";

    return script;
}

inline WindowsStartupFrame parse_windows_startup_frame(const ProcessResult &captured,
                                                       std::string_view frame,
                                                       std::string_view nonce,
                                                       std::string_view pinned_boot,
                                                       std::string_view alternate_nonce = {}) {
    if (!windows_startup_token(frame) || !windows_startup_token(nonce) ||
        (!pinned_boot.empty() && !android_boot_id_valid(pinned_boot)) ||
        (!alternate_nonce.empty() && !windows_startup_token(alternate_nonce))) {
        throw VoyemuError("ANDROID_STARTUP_ARGUMENT", "fixed frame identity required");
    }

    if (captured.timed_out) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY", "startup action deadline exceeded");
    }

    if (captured.output.size() > 512 * 1024) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "response exceeds bound");
    }

    std::string text;
    text.reserve(captured.output.size());

    for (std::size_t index = 0; index < captured.output.size(); ++index) {
        const auto value = captured.output[index];

        if (value == '\r' && index + 1 < captured.output.size() &&
            captured.output[index + 1] == '\n') {
            continue;
        }

        text += value;
    }

    const std::string marker = "VOYEMU_STARTUP_" + std::string(frame);
    std::size_t cursor = 0;

    if (captured.exit_code && !text.starts_with(marker + ":b1:")) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY",
                          "startup transport produced no guest frame");
    }

    auto field = [&](std::string_view name) {
        const std::string prefix = marker + ":" + std::string(name) + ":";

        if (text.compare(cursor, prefix.size(), prefix) != 0) {
            throw VoyemuError("ANDROID_STARTUP_FRAME", "missing or reordered field");
        }

        cursor += prefix.size();
        const auto end = text.find('\n', cursor);

        if (end == std::string::npos) {
            throw VoyemuError("ANDROID_STARTUP_FRAME", "incomplete field");
        }

        auto value = text.substr(cursor, end - cursor);
        cursor = end + 1;

        return value;
    };
    const auto before = field("b1"), first_nonce = field("n1");

    if (!android_boot_id_valid(before) ||
        (first_nonce != nonce && (alternate_nonce.empty() || first_nonce != alternate_nonce))) {
        throw VoyemuError("ANDROID_IDENTITY_MISMATCH", "startup pre-fence differs");
    }

    if (!pinned_boot.empty() && before != pinned_boot) {
        return {before, first_nonce, 0, {79, {}, false}, true};
    }

    if (captured.exit_code) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY", "startup framed action unavailable");
    }

    const auto body_marker = marker + ":body\n";

    if (text.compare(cursor, body_marker.size(), body_marker) != 0) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "missing body");
    }

    cursor += body_marker.size();
    const auto ending = "\n" + marker + ":exit:";
    const auto end = text.find(ending, cursor);

    if (end == std::string::npos || text.find(ending, end + ending.size()) != std::string::npos) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "missing or duplicated action status");
    }

    const auto body = text.substr(cursor, end - cursor);
    cursor = end + 1;
    const auto code = field("exit"), last_nonce = field("n2"), after = field("b2"),
               utc = field("utc");

    if (cursor != text.size() || body.find(marker) != std::string::npos) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "extra frame data");
    }

    int status = -1;
    const auto parsed = std::from_chars(code.data(), code.data() + code.size(), status);

    if (parsed.ec != std::errc{} || parsed.ptr != code.data() + code.size() || status < 0 ||
        status > 255) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "invalid action status");
    }

    if (last_nonce != first_nonce || !android_boot_id_valid(after)) {
        throw VoyemuError("ANDROID_IDENTITY_MISMATCH", "startup post-fence differs");
    }

    if (after != before) {
        return {after, last_nonce, 0, {79, {}, false}, true};
    }

    const auto epoch = android_epoch_seconds(utc);

    if (!epoch || *epoch < 0) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "invalid UTC observation");
    }

    return {before, first_nonce, *epoch, {status, body, false}};
}

// A reboot disconnects adbd before its first framed read can complete.
// Only transport-unavailable frames are pending; identity and framing errors stay fatal.
inline std::optional<WindowsStartupFrame> parse_windows_reboot_frame(const ProcessResult &captured,
                                                                     std::string_view frame,
                                                                     std::string_view nonce) {
    try {
        return parse_windows_startup_frame(captured, frame, nonce, {});
    } catch (const std::runtime_error &error) {
        const std::string_view message = error.what();

        if (message == "ANDROID_IDENTITY_NOT_READY: startup transport produced no guest frame" ||
            message == "ANDROID_IDENTITY_NOT_READY: startup framed action unavailable") {
            return std::nullopt;
        }

        throw;
    }
}

inline std::vector<std::string> windows_startup_sections(std::string_view text,
                                                         std::string_view marker,
                                                         const std::vector<std::string> &names) {
    if (!windows_startup_token(marker) || names.empty() || text.size() > 512 * 1024) {
        throw VoyemuError("ANDROID_STARTUP_FRAME", "bounded section frame required");
    }

    std::vector<std::string> result;
    std::size_t cursor = 0;

    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto prefix = "VOYEMU_SECTION_" + std::string(marker) + ":" + names[index] + "\n";

        if (text.substr(cursor, prefix.size()) != prefix) {
            throw VoyemuError("ANDROID_STARTUP_FRAME", "missing compound section");
        }

        cursor += prefix.size();
        auto end = text.size();

        if (index + 1 < names.size()) {
            const auto next = "VOYEMU_SECTION_" + std::string(marker) + ":" + names[index + 1] +
                              "\n";
            end = text.find(next, cursor);

            if (end == std::string_view::npos) {
                throw VoyemuError("ANDROID_STARTUP_FRAME", "missing compound terminator");
            }
        }

        const auto section = text.substr(cursor, end - cursor);

        if (section.size() > 128 * 1024 ||
            section.find("VOYEMU_SECTION_" + std::string(marker) + ":") != std::string_view::npos) {
            throw VoyemuError("ANDROID_STARTUP_FRAME", "duplicate or oversized compound section");
        }

        result.emplace_back(section);
        cursor = end;
    }

    return result;
}

// Absence is a read-only pending condition, never a consumed clock write.
inline bool windows_alarm_service_available(const ProcessResult &result) {
    auto text = result.output;

    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }

    if (result.timed_out || result.exit_code) {
        throw VoyemuError("ANDROID_CLOCK_SERVICE", "service query unavailable");
    }

    if (text == "Service alarm: found") {
        return true;
    }

    if (text == "Service alarm: not found") {
        return false;
    }

    throw VoyemuError("ANDROID_CLOCK_SERVICE", "unexpected service identity");
}

// Closed Windows startup/maintenance phases; receipts are never mutation authority.
enum class WindowsStartupPhase {
    identity,
    boot_recovery,
    health,
    event_submit,
    event_verify,
    root_submit,
    root_verify,
    enforcing,
    clock_read,
    clock_set,
    clock_verify,
    wheel_bootstrap,
    wheel_verify,
    state_prior,
    state_submit,
    state_verify,
    boot_completed,
    home_qualified,
    home_declared,
    home_default,
    home_set,
    home_verify,
    home_windows,
    home_start,
    display_read,
    display_windows,
    secondary_wake,
    secondary_service,
    systemui_service,
    language,
    appearance,
    theme_first,
    theme_second,
    ready,
    failed,
    clock_service,
    framework_ready,
    startup_audio,
    startup_interguest
};

// macOS runs these three startup services inside its guard stage: the A9 canbus package bootstrap
// (also reached through the audio stage), the audio startup provider, and the interguest network.
inline bool windows_startup_services_required(const Json &specification) {
    return specification.contains("qinggan09_owned_packages") ||
           specification.contains("audio_startup") || specification.contains("interguest_network");
}

// Ordered guard-stage routing: framework -> audio (canbus + audio provider) -> interguest -> clock.
inline WindowsStartupPhase windows_after_framework_ready(bool services_required) {
    return services_required ? WindowsStartupPhase::startup_audio : WindowsStartupPhase::clock_read;
}

inline WindowsStartupPhase windows_after_startup_audio(bool interguest_required) {
    return interguest_required ? WindowsStartupPhase::startup_interguest
                               : WindowsStartupPhase::clock_read;
}

enum class WindowsFrameworkState {
    pending,
    ready,
    invalid
};
struct WindowsFrameworkReadiness {
    WindowsFrameworkState state = WindowsFrameworkState::pending;
    bool activity = false, package = false, boot_completed = false;
};

inline std::string windows_framework_probe_command() {
    return "printf 'activity:'; service check activity; printf 'package:'; service check package; printf 'boot:'; getprop sys.boot_completed";
}

inline WindowsFrameworkReadiness windows_framework_readiness(const ProcessResult &result) {
    if (result.timed_out || result.exit_code) {
        return {};
    }

    if (result.output.size() > 4096 || result.output.find(char(0)) != std::string::npos) {
        return {WindowsFrameworkState::invalid};
    }

    std::string text;

    for (std::size_t i = 0; i < result.output.size(); ++i) {
        if (result.output[i] == '\r' && i + 1 < result.output.size() &&
            result.output[i + 1] == '\n') {
            continue;
        }

        text += result.output[i];
    }

    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }

    std::vector<std::string> rows;
    std::size_t start = 0;

    for (;;) {
        auto end = text.find('\n', start);
        rows.push_back(text.substr(start, end == std::string::npos ? end : end - start));

        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    if (rows.size() != 3) {
        return {WindowsFrameworkState::invalid};
    }

    WindowsFrameworkReadiness value;

    if (rows[0] == "activity:Service activity: found") {
        value.activity = true;
    } else if (rows[0] != "activity:Service activity: not found") {
        return {WindowsFrameworkState::invalid};
    }

    if (rows[1] == "package:Service package: found") {
        value.package = true;
    } else if (rows[1] != "package:Service package: not found") {
        return {WindowsFrameworkState::invalid};
    }

    if (rows[2] == "boot:1") {
        value.boot_completed = true;
    } else if (rows[2] != "boot:0" && rows[2] != "boot:") {
        return {WindowsFrameworkState::invalid};
    }

    value.state = value.activity && value.package && value.boot_completed
                          ? WindowsFrameworkState::ready
                          : WindowsFrameworkState::pending;

    return value;
}

inline WindowsStartupPhase windows_framework_next(const WindowsFrameworkReadiness &ready) {
    if (ready.state == WindowsFrameworkState::invalid) {
        throw VoyemuError("ANDROID_FRAMEWORK_RESPONSE", "unexpected service readiness response");
    }

    return ready.state == WindowsFrameworkState::ready ? WindowsStartupPhase::clock_read
                                                       : WindowsStartupPhase::framework_ready;
}

inline WindowsStartupPhase windows_framework_after_clock(bool framework_verified, bool wheel,
                                                         bool verify) {
    if (!framework_verified) {
        return WindowsStartupPhase::framework_ready;
    }

    return wheel ? (verify ? WindowsStartupPhase::wheel_verify
                           : WindowsStartupPhase::wheel_bootstrap)
                 : WindowsStartupPhase::state_prior;
}

enum class WindowsClockSetFailure {
    permission,
    service,
    binder,
    unknown_command,
    unclassified
};
struct WindowsClockSetDiagnostic {
    WindowsClockSetFailure kind;
    std::string_view code, message;
};

// Call only after complete startup frame identity validation. Diagnostics grant no authority.
inline std::optional<WindowsClockSetDiagnostic>
windows_clock_set_diagnostic(WindowsStartupPhase phase, const WindowsStartupFrame &framed) {
    if (phase != WindowsStartupPhase::clock_set || framed.boot_changed || framed.result.timed_out ||
        framed.result.exit_code < 1 || framed.result.exit_code > 255) {
        return std::nullopt;
    }

    const auto unclassified = WindowsClockSetDiagnostic{
            WindowsClockSetFailure::unclassified, "unclassified",
            "Unclassified guest alarm command failure; payload not persisted"};
    std::string_view line = framed.result.output;

    if (line.size() > 4096) {
        return unclassified;
    }

    if (line.empty()) {
        return WindowsClockSetDiagnostic{WindowsClockSetFailure::unclassified, "no_diagnostic_text",
                                         "Guest alarm command failed without diagnostic text"};
    }

    if (!line.empty() && line.back() == '\n') {
        line.remove_suffix(1);
    }

    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }

    if (line == "Permission denied" || line == "Permission Denial" ||
        line == "java.lang.SecurityException" || line == "Security exception: Permission denied") {
        return WindowsClockSetDiagnostic{WindowsClockSetFailure::permission, "permission",
                                         "Guest alarm command reported permission failure"};
    }

    if (line == "cmd: Can't find service: alarm" || line == "Can't find service: alarm" ||
        line == "Service alarm: not found") {
        return WindowsClockSetDiagnostic{WindowsClockSetFailure::service, "service",
                                         "Guest alarm command reported unavailable service"};
    }

    if (line == "Dead object" || line == "android.os.DeadObjectException" ||
        line == "android.os.RemoteException" ||
        line == "Remote exception: android.os.DeadObjectException") {
        return WindowsClockSetDiagnostic{WindowsClockSetFailure::binder, "binder",
                                         "Guest alarm command reported Binder failure"};
    }

    if (line == "Unknown command: set-time" || line == "Unknown command: alarm") {
        return WindowsClockSetDiagnostic{WindowsClockSetFailure::unknown_command, "unknown_command",
                                         "Guest alarm command reported unknown command"};
    }

    return unclassified;
}

struct WindowsAndroidOwner {
    std::string epoch;
    std::uint64_t generation{};
    std::int64_t sdk_pid{};
    std::string route;
    bool operator==(const WindowsAndroidOwner &) const = default;
};
struct WindowsAndroidTransaction {
    using Clock = std::chrono::steady_clock;
    WindowsStartupPhase phase = WindowsStartupPhase::identity;
    WindowsAndroidOwner owner;
    std::string nonce, boot, candidate_boot, frame, component, display_text, window_text,
            health_marker;
    std::int64_t observed_utc = 0, observed_host_utc_ms = 0;
    Clock::time_point observed_arrival{};
    std::uint64_t root_proof_operation = 0, settings_revision = 0;
    // Replies to the OEM theme queries read so far.
    std::vector<std::string> theme_replies;
    int clock_exit_code = -1;
    std::uint32_t wheel_pair_up_sequence = 0;
    int wheel_pair_hold_ms = 0;
    bool wheel_pair_cleanup = false, wheel_pair_down_receipt_available = false;
    Json submitted_state = Json::object();
    std::set<std::string> attempted_mutations;
    Clock::time_point started{}, last_observation{}, next_health{}, action_started{},
            action_deadline{};
    bool initialized = false, reboot_confirmed = false, ever_ready = false,
         pending_mutation = false, pending_public = false, public_operation = false,
         host_action = false, launch_reserved = false, startup_services = false;
    std::vector<std::string> pending_arguments;
    std::string pending_attempt;
    std::size_t capture_limit = 130 * 1024;
    std::string failure;
    static constexpr auto startup_budget = std::chrono::seconds(1800);
    static constexpr auto observation_window = std::chrono::seconds(63);
    static constexpr auto child_budget = std::chrono::seconds(60);

    void initialize(const WindowsAndroidOwner &current, std::string current_nonce,
                    Clock::time_point start) {
        const auto confirmed = !initialized && reboot_confirmed && owner == current &&
                                               nonce == current_nonce && android_boot_id_valid(boot)
                                       ? boot
                                       : std::string{};
        *this = WindowsAndroidTransaction{};
        initialized = true;
        owner = current;
        nonce = std::move(current_nonce);
        boot = confirmed;
        started = start;
    }

    bool startup_expired(Clock::time_point now) const {
        return initialized && !ever_ready && now - started >= startup_budget;
    }

    bool observation_current(Clock::time_point now) const {
        return ever_ready && last_observation != Clock::time_point{} &&
               now - last_observation <= observation_window;
    }
};
} // namespace voyemu
