#pragma once
#include "model.hpp"
#include <regex>
#include <set>
namespace voyemu {
inline std::string observer_wire_timeout(std::string_view operation, std::string_view phase,
                                         int budget_ms, std::int64_t elapsed_ms,
                                         std::size_t response_bytes, std::int64_t observer_pid) {
    const std::set<std::string_view> operations = {"hello",
                                                   "ui",
                                                   "tree",
                                                   "close",
                                                   "events.subscribe",
                                                   "events.read",
                                                   "events.unsubscribe"};
    const std::set<std::string_view> phases = {"connect", "send", "recv"};
    Json value = {{"event", "observer_wire_timeout"},
                  {"op", operations.contains(operation) ? std::string(operation) : "other"},
                  {"phase", phases.contains(phase) ? std::string(phase) : "unknown"},
                  {"budget_ms", budget_ms},
                  {"elapsed_ms", elapsed_ms},
                  {"response_bytes", response_bytes},
                  {"observer_pid", observer_pid > 1 && observer_pid <= 4294967295LL
                                           ? Json(observer_pid)
                                           : Json(nullptr)}};

    return value.dump();
}

inline std::string observer_failure_summary(const Json &hello) {
    const auto word = [](const Json &object, const char *key, const char *pattern,
                         std::size_t bound) {
        if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
            return std::string("unknown");
        }

        const auto value = object.at(key).get<std::string>();

        return value.size() <= bound && std::regex_match(value, std::regex(pattern))
                       ? value
                       : std::string("unknown");
    };
    std::string result =
            "reason=" + word(hello, "reason", "[a-z0-9_]{1,80}", 80) +
            "; error_type=" + word(hello, "error_type", "[A-Za-z_$][A-Za-z0-9_$]{0,79}", 80) +
            "; cause_type=" + word(hello, "cause_type", "[A-Za-z_$][A-Za-z0-9_$]{0,79}", 80);

    if (!hello.is_object() || !hello.contains("failure") || !hello.at("failure").is_object()) {
        return result;
    }

    const auto &failure = hello.at("failure");

    if (!failure.contains("causes") || !failure.at("causes").is_array()) {
        return result;
    }

    const std::set<std::string> codes = {
            "unclassified", "automation_already_connected", "automation_not_connected",
            "automation_service_already_registered", "automation_connect_failed"};
    std::size_t index = 0;

    for (const auto &cause : failure.at("causes")) {
        if (index++ == 4) {
            break;
        }

        const auto code = word(cause, "code", "[a-z_]{1,80}", 80);
        result += "; cause_code=" + (codes.contains(code) ? code : std::string("unknown"));

        if (!cause.is_object() || !cause.contains("frames") || !cause.at("frames").is_array()) {
            continue;
        }

        std::size_t frames = 0;

        for (const auto &frame : cause.at("frames")) {
            if (frames++ == 4) {
                break;
            }

            const auto klass = word(
                    frame, "class",
                    "(android|com\\.android|java|com\\.voyemu\\.api)\\.[A-Za-z_$][A-Za-z0-9_.$]{0,159}",
                    160);
            const auto method = word(frame, "method", "[A-Za-z_$<>][A-Za-z0-9_$<>]{0,79}", 80);

            if (klass == "unknown" || method == "unknown") {
                continue;
            }

            result += "; frame=" + klass + "." + method;

            if (frame.contains("line") && frame.at("line").is_number_integer()) {
                const auto line = frame.at("line").get<std::int64_t>();

                if (line >= -1 && line <= 1000000) {
                    result += ":" + std::to_string(line);
                }
            }
        }
    }

    return result;
}
} // namespace voyemu
