#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace voyemu {
inline std::optional<std::int64_t> android_epoch_seconds(std::string_view reply) {
    if (reply.empty() || reply.size() > 64) {
        return std::nullopt;
    }

    while (!reply.empty() && (reply.back() == '\n' || reply.back() == '\r' || reply.back() == ' ' ||
                              reply.back() == '\t')) {
        reply.remove_suffix(1);
    }

    while (!reply.empty() && (reply.front() == ' ' || reply.front() == '\t')) {
        reply.remove_prefix(1);
    }

    if (reply.empty() || reply.front() < '0' || reply.front() > '9') {
        return std::nullopt;
    }

    std::int64_t value{};
    const auto [end, error] = std::from_chars(reply.data(), reply.data() + reply.size(), value);

    if (error != std::errc{} || end != reply.data() + reply.size() ||
        value > std::numeric_limits<std::int64_t>::max() / 1000) {
        return std::nullopt;
    }

    return value;
}

inline bool android_clock_current(std::int64_t guest_seconds, std::int64_t host_ms) {
    if (guest_seconds < 0 || guest_seconds > std::numeric_limits<std::int64_t>::max() / 1000 ||
        host_ms < 0) {
        return false;
    }

    const auto guest_ms = guest_seconds * 1000;
    const auto difference = host_ms >= guest_ms ? host_ms - guest_ms : guest_ms - host_ms;

    return difference <= 2000;
}

// Identity-bound observations age on the monotonic clock, independently of UTC quality.
inline bool android_clock_observation_current(std::int64_t steady_age_ms) {
    return steady_age_ms >= 0 && steady_age_ms <= 63000;
}

// Host UTC steps invalidate synchronization quality without changing boot identity.
inline bool android_clock_arrival_valid(std::int64_t arrival_host_ms, std::int64_t current_host_ms,
                                        std::int64_t steady_age_ms) {
    if (arrival_host_ms < 0 || current_host_ms < 0 ||
        !android_clock_observation_current(steady_age_ms)) {
        return false;
    }

    const auto wall_age = current_host_ms - arrival_host_ms;

    return wall_age >= steady_age_ms - 2000 && wall_age <= steady_age_ms + 2000;
}

inline bool android_clock_synced(std::int64_t guest_seconds, std::int64_t arrival_host_ms,
                                 std::int64_t current_host_ms, std::int64_t steady_age_ms) {
    return android_clock_current(guest_seconds, arrival_host_ms) &&
           android_clock_arrival_valid(arrival_host_ms, current_host_ms, steady_age_ms);
}

// Fixed caller only; no arbitrary shell arguments. Samples race with the short-lived child.
inline std::string android_clock_caller_command(std::int64_t target_ms) {
    if (target_ms < 0) {
        throw std::invalid_argument("ANDROID_CLOCK: nonnegative target required");
    }

    std::string command =
            "voyemu_clock_caller() { printf 'VOYEMU_CLOCK_V1 begin operation=alarm.set-time sample=post-spawn-not-atomic\\n'; ";
    command += "printf 'parent_pid=%s\\n' \"$$\"; ";
    command +=
            "for f in /proc/sys/kernel/random/boot_id /proc/uptime /proc/$$/stat /proc/$$/attr/current /proc/$$/comm; do printf 'before %s=' \"$f\"; head -c 512 \"$f\"; printf '\\n'; done; ";
    command += "/system/bin/cmd alarm set-time " + std::to_string(target_ms) +
               " & c=$!; printf 'child_pid=%s\\n' \"$c\"; ";
    command +=
            "for f in /proc/$c/stat /proc/$c/attr/current /proc/$c/comm; do printf 'child %s=' \"$f\"; head -c 512 \"$f\"; printf '\\n'; done; ";
    command +=
            "wait \"$c\"; r=$?; printf 'wait_status=%s signal_interpretation=ambiguous-if-over-128\\n' \"$r\"; ";
    command += "if [ \"$r\" -gt 128 ]; then printf 'possible_signal=%s\\n' \"$((r-128))\"; fi; ";
    command +=
            "for f in /proc/sys/kernel/random/boot_id /proc/uptime; do printf 'after %s=' \"$f\"; head -c 512 \"$f\"; printf '\\n'; done; printf 'VOYEMU_CLOCK_V1 end\\n'; return \"$r\"; }; voyemu_clock_caller";

    return command;
}
} // namespace voyemu
