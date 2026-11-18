#pragma once
#include "android_reboot.hpp"
#include "interguest_link.hpp"
#include "model.hpp"
#include <charconv>
#include <functional>

namespace voyemu {
inline std::uint64_t restart_generation(const Json &value) {
    if (!value.is_string()) {
        throw VoyemuError("TEST_ARGUMENT",
                          "QNX generation must be a canonical positive decimal string");
    }

    const auto text = value.get<std::string>();
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);

    if (text.empty() || text.front() == '0' || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() || !result) {
        throw VoyemuError("TEST_ARGUMENT", "invalid QNX generation");
    }

    return result;
}

inline void validate_restart_expected(const Json &expected) {
    if (!expected.is_object() || expected.size() != 3 || !expected.contains("android") ||
        !expected.at("android").is_string() ||
        !android_boot_id_valid(expected.at("android").get<std::string>()) ||
        !expected.contains("qnx_generation") || !expected.contains("qnx_pid") ||
        !expected.at("qnx_pid").is_number_integer() ||
        expected.at("qnx_pid").get<std::int64_t>() < 2) {
        throw VoyemuError("TEST_ARGUMENT",
                          "full restart requires Android UUID and QNX generation/process identity");
    }

    (void)restart_generation(expected.at("qnx_generation"));
}

inline Json restart_boot_identity(const std::string &android, std::uint64_t generation,
                                  std::int64_t pid, bool cold) {
    return {{"android_boot_id", android},
            {"qnx",
             {{"kind", cold ? "owned_cold_incarnation" : "owned_qemu_incarnation"},
              {"generation", std::to_string(generation)},
              {"qemu_pid", pid}}}};
}

inline void validate_restart_before(const Json &expected, const Json &before) {
    validate_restart_expected(expected);

    if (before.at("android_boot_id") != expected.at("android") ||
        before.at("qnx").at("generation") != expected.at("qnx_generation") ||
        before.at("qnx").at("qemu_pid") != expected.at("qnx_pid")) {
        throw VoyemuError("SESSION_RESTART_STALE", "guest boot identity differs before dispatch");
    }
}

inline bool restart_qnx_ack(const InterguestLink::Status &link, std::uint64_t previous,
                            std::uint64_t current, std::uint64_t bound) {
    return link.ready && link.fault.empty() && link.generation > previous && bound == current &&
           link.utc_complete && link.utc_status == 0 && link.utc_expected >= 946684800ULL &&
           link.utc_expected < 4102444800ULL && link.utc_actual >= link.utc_expected &&
           link.utc_actual - link.utc_expected <= 2;
}

inline bool restart_boots_changed(const Json &before, const Json &after) {
    return after.is_object() && after.contains("android_boot_id") &&
           after.at("android_boot_id").is_string() &&
           android_boot_id_valid(after.at("android_boot_id").get<std::string>()) &&
           after.at("android_boot_id") != before.at("android_boot_id") && after.contains("qnx") &&
           after.at("qnx").value("kind", std::string{}) == "owned_cold_incarnation" &&
           restart_generation(after.at("qnx").at("generation")) >
                   restart_generation(before.at("qnx").at("generation")) &&
           after.at("qnx").at("qemu_pid").get<std::int64_t>() > 1;
}

// Specific full-machine orchestration, shared by runtime and host failure controls.
struct FullRestartActions {
    std::function<std::int64_t()> now_ms;
    std::function<void()> stop_qnx, stop_android, start_android, start_qnx, wait;
    std::function<Json()> observe;
    std::function<void(const Json &)> publish;
};

inline Json run_full_restart(const Json &expected, const Json &before, Json receipt, int timeout_ms,
                             const FullRestartActions &actions) {
    validate_restart_before(expected, before);

    if (timeout_ms < 1 || timeout_ms > 180000) {
        throw VoyemuError("TEST_ARGUMENT", "restart timeout must be 1..180000 ms");
    }

    const auto deadline = actions.now_ms() + timeout_ms;
    receipt.update({{"status", "pending"},
                    {"ok", false},
                    {"mode", "cold"},
                    {"scope", "android_qnx_cold_boot_changed"},
                    {"before", before},
                    {"after", nullptr},
                    {"authority_retained", false},
                    {"guest_acknowledged", false},
                    {"old_processes_stopped", false},
                    {"stopped", {{"android", false}, {"qnx", false}}},
                    {"phase", "admitted"},
                    {"accepted", false},
                    {"application_ready", false}});
    actions.publish(receipt);
    try {
        auto phase = [&](const char *name, const std::function<void()> &action) {
            if (actions.now_ms() >= deadline) {
                throw VoyemuError("SESSION_RESTART_DEADLINE", "operation deadline reached");
            }

            receipt["phase"] = name;
            actions.publish(receipt);
            action();
        };
        phase("stopping_qnx", actions.stop_qnx);
        receipt["stopped"]["qnx"] = true;
        actions.publish(receipt);
        phase("stopping_android", actions.stop_android);
        receipt["stopped"]["android"] = true;
        receipt["old_processes_stopped"] = true;
        actions.publish(receipt);
        phase("starting_android", actions.start_android);
        phase("starting_qnx", actions.start_qnx);
        receipt["phase"] = "verifying_both_boots";
        actions.publish(receipt);

        while (actions.now_ms() < deadline) {
            const auto observed = actions.observe();
            receipt["after"] = observed.value("boot", Json());

            if (actions.now_ms() < deadline && observed.value("guest_acknowledged", false) &&
                restart_boots_changed(before, receipt.at("after"))) {
                receipt["status"] = "completed";
                receipt["authority_retained"] = true;
                receipt["ok"] = true;
                receipt["phase"] = "completed";
                receipt["accepted"] = true;
                receipt["guest_acknowledged"] = true;
                actions.publish(receipt);

                return receipt;
            }

            actions.wait();
        }

        throw VoyemuError("SESSION_RESTART_DEADLINE",
                          "both fresh guest boots were not verified before deadline");
    } catch (const std::exception &failure) {
        receipt["status"] = "unknown";
        receipt["authority_retained"] = false;
        receipt["ok"] = false;
        receipt["accepted"] = false;
        receipt["error"] = std::string(failure.what()).substr(0, 1024);
        try {
            actions.publish(receipt);
        } catch (...) { /* Ignored: retain an earlier pending receipt; a failed witness cannot make
                           the action replayable. */
        }

        throw VoyemuError("SESSION_RESTART_UNKNOWN", failure.what());
    }
}

inline void validate_restart_complete(const Json &result, const Json &expected) {
    try {
        validate_restart_before(expected, result.at("before"));

        if (!result.value("ok", false) || result.value("status", std::string{}) != "completed" ||
            !result.value("accepted", false) || result.value("mode", std::string{}) != "cold" ||
            result.value("scope", std::string{}) != "android_qnx_cold_boot_changed" ||
            !result.value("old_processes_stopped", false) ||
            !result.value("authority_retained", false) ||
            !result.value("guest_acknowledged", false) ||
            !restart_boots_changed(result.at("before"), result.at("after"))) {
            throw VoyemuError("SESSION_RESTART_UNKNOWN", "incomplete full-machine restart receipt");
        }
    } catch (const std::exception &) {
        throw VoyemuError("SESSION_RESTART_UNKNOWN",
                          "invalid or incomplete full-machine restart receipt");
    }
}
} // namespace voyemu
