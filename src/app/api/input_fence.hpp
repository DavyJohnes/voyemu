#pragma once
#include "model.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
namespace voyemu::api {
class InputFenceError final : public VoyemuError {
public:
    const Json details;

    InputFenceError(const char *code, const char *message, Json diagnostic)
        : VoyemuError(code, message), details(std::move(diagnostic)) {}
};

// Unsigned values are compared as unsigned, so values above the range never wrap into it.
inline bool fence_integer(const Json &value, std::int64_t minimum, std::int64_t maximum) {
    return value.is_number_integer() &&
           (value.is_number_unsigned()
                    ? value.get<std::uint64_t>() <= static_cast<std::uint64_t>(maximum) &&
                              static_cast<std::int64_t>(value.get<std::uint64_t>()) >= minimum
                    : value.get<std::int64_t>() >= minimum && value.get<std::int64_t>() <= maximum);
}

inline bool fence_int(const Json &value) {
    return fence_integer(value, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
}

// Only numeric public window geometry is projected; arbitrary observer/request strings never
// escape.
inline Json fence_window(const Json &window) {
    Json result = Json::object();

    if (!window.is_object()) {
        return result;
    }

    for (const char *key : {"id", "type", "layer"}) {
        if (window.contains(key) && fence_int(window.at(key))) {
            result[key] = window.at(key);
        }
    }

    if (window.contains("bounds") && window.at("bounds").is_object()) {
        const auto &bounds = window.at("bounds");
        bool valid = true;

        for (const char *key : {"x", "y", "width", "height"}) {
            if (!bounds.contains(key) || !fence_int(bounds.at(key))) {
                valid = false;
            }
        }

        if (valid) {
            result["bounds"] = Json::object();

            for (const char *key : {"x", "y", "width", "height"}) {
                result["bounds"][key] = bounds.at(key);
            }
        }
    }

    return result;
}

enum class InputIntent {
    Target,
    Gesture
};

inline Json fenced_point(const Json &request, const Json &observation,
                         InputIntent input_intent = InputIntent::Target) {
    auto argument = [] {
        throw VoyemuError(
                "TEST_ARGUMENT",
                "expected needs bounded boot/source/revision/window/target identity only");
    };
    Json details = Json::object();
    auto stale = [&](const char *reason) {
        details["reason"] = reason;
        throw InputFenceError("TEST_INPUT_STALE",
                              "target identity or geometry changed before dispatch", details);
    };
    auto blocked = [&](const char *reason = "coverage_unavailable") {
        details["reason"] = reason;
        throw InputFenceError("TEST_INPUT_BLOCKED",
                              "complete actionable input coverage unavailable", details);
    };

    if (!request.contains("expected") || !request.at("expected").is_object()) {
        argument();
    }

    const auto &expected = request.at("expected");
    const std::set<std::string> fields = {"boot_id", "source_instance", "revision", "window",
                                          "target"};

    if (expected.size() != fields.size()) {
        argument();
    }

    for (const auto &[key, value] : expected.items()) {
        if (!fields.contains(key)) {
            argument();
        }
    }

    for (const char *key : {"boot_id", "source_instance", "revision"}) {
        if (!expected.at(key).is_string() || expected.at(key).get<std::string>().empty() ||
            expected.at(key).get<std::string>().size() > 256) {
            argument();
        }

        if (std::string_view(key) != "revision" &&
            (!observation.contains(key) || observation.at(key) != expected.at(key))) {
            stale(std::string_view(key) == "boot_id" ? "boot_changed" : "source_changed");
        }
    }

    const auto &target = expected.at("target");
    const auto &window = expected.at("window");
    auto shape = [&](const Json &object, const std::set<std::string> &allowed,
                     const std::set<std::string> &required) {
        if (!object.is_object() || object.empty() || object.dump().size() > 8192) {
            argument();
        }

        for (const auto &[key, value] : object.items()) {
            if (!allowed.contains(key)) {
                argument();
            }
        }

        for (const auto &key : required) {
            if (!object.contains(key)) {
                argument();
            }
        }
    };
    const bool verified_target = window.is_object() && window.contains("verified_binding");
    auto window_fields = std::set<std::string>{"id", "type", "layer", "bounds", "title", "package"};
    auto target_fields = std::set<std::string>{"window_id", "resource_id", "package",       "class",
                                               "text",      "bounds",      "clipped_bounds"};

    if (verified_target) {
        window_fields.insert({"verified_binding", "read_complete", "root_id", "node_count"});
        target_fields.insert("id");
    }

    shape(window, window_fields, {"id", "type", "bounds", "title", "package"});
    shape(target, target_fields,
          {"window_id", "resource_id", "package", "class", "bounds", "clipped_bounds"});
    const auto projected = fence_window(window);

    if (!projected.empty()) {
        details["expected_window"] = projected;
    }

    if (!observation.contains("screens") || !observation.at("screens").is_array()) {
        blocked();
    }

    const Json *screen = nullptr;

    for (const auto &candidate : observation.at("screens")) {
        if (candidate.value("screen", std::string{}) == request.value("screen", std::string{})) {
            if (screen) {
                blocked();
            }

            screen = &candidate;
        }
    }

    if (!screen) {
        blocked();
    }

    if (!verified_target &&
        (screen->value("status", std::string{}) != "ok" || !screen->value("complete", false))) {
        blocked();
    }

    if (verified_target && (screen->value("status", std::string{}) != "ok" &&
                            screen->value("status", std::string{}) != "partial")) {
        blocked();
    }

    for (const char *key : {"width", "height"}) {
        if (!screen->contains(key) || !screen->at(key).is_number_integer() ||
            screen->at(key).get<std::int64_t>() <= 0 ||
            screen->at(key).get<std::int64_t>() >
                    (std::string_view(key) == "width" ? 3840 : 2160)) {
            throw VoyemuError("TEST_INPUT_UNAVAILABLE", "observed screen dimensions unavailable");
        }
    }

    const auto &windows = screen->at("windows");
    const auto &nodes = screen->at("nodes");

    if (!windows.is_array() || !nodes.is_array()) {
        blocked();
    }

    auto matches = [](const Json &actual, const Json &expected) {
        for (const auto &[key, value] : expected.items()) {
            if (!actual.contains(key) || actual.at(key) != value) {
                return false;
            }
        }

        return true;
    };
    const Json *matched_window = nullptr;

    for (const auto &candidate : windows) {
        if (matches(candidate, window)) {
            if (matched_window) {
                blocked();
            }

            matched_window = &candidate;
        }
    }

    if (!matched_window) {
        const Json *same_id = nullptr;
        std::size_t same_count = 0;

        for (const auto &candidate : windows) {
            if (candidate.is_object() && candidate.contains("id") &&
                candidate.at("id") == window.at("id")) {
                ++same_count;
                same_id = &candidate;
            }
        }

        if (same_count == 1) {
            const auto observed = fence_window(*same_id);

            if (!observed.empty()) {
                details["observed_window"] = observed;
            }
        }

        stale(same_count ? "window_changed" : "window_missing");
    }

    const auto observed = fence_window(*matched_window);

    if (!observed.empty()) {
        details["observed_window"] = observed;
    }

    // This finite path verifies the original Phone target identity and geometry only.
    // API28 does not prove global occlusion or input-region touchability.
    if (verified_target) {
        std::size_t window_count = 0;

        for (const auto &candidate : windows) {
            if (candidate.is_object() && candidate.contains("id") &&
                candidate.at("id") == matched_window->at("id")) {
                ++window_count;
            }
        }

        if (window_count != 1) {
            blocked();
        }

        if (input_intent != InputIntent::Target ||
            request.value("screen", std::string{}) != "primary" ||
            request.value("scope", std::string{}) != "all_attached" ||
            !request.contains("selector") || request.contains("x") || request.contains("y") ||
            request.contains("points") || request.value("intent", std::string("tap")) != "tap") {
            blocked();
        }

        const std::string package = "com.qinggan.bluetoothphone";
        const std::set<std::string> roles = {package + ":id/answer_iv", package + ":id/mini_answer",
                                             package + ":id/hangup_calling_single_iv",
                                             package + ":id/calling_callout_calloff_tv"};

        if (window.at("type") != 2010 || window.at("package") != package ||
            target.at("package") != package || !target.contains("id") ||
            !target.at("id").is_string() || target.at("id").get<std::string>().empty() ||
            !target.at("resource_id").is_string() ||
            !roles.contains(target.at("resource_id").get<std::string>())) {
            blocked();
        }

        const auto &binding = matched_window->at("verified_binding");
        const std::set<std::string> binding_fields = {"schema",
                                                      "basis",
                                                      "boot_id",
                                                      "observer_epoch",
                                                      "display_epoch",
                                                      "display_id",
                                                      "window_id",
                                                      "session_id",
                                                      "session_pid",
                                                      "session_uid",
                                                      "owner_uid",
                                                      "package",
                                                      "process_name",
                                                      "window_token",
                                                      "window_client",
                                                      "root_id",
                                                      "node_count",
                                                      "wm_order",
                                                      "wm_order_verified",
                                                      "read_complete",
                                                      "process_verified",
                                                      "current_focus",
                                                      "touchable",
                                                      "focusable",
                                                      "visible",
                                                      "has_surface",
                                                      "drawn",
                                                      "view_visible",
                                                      "obscured"};

        if (!binding.is_object() || binding.size() != binding_fields.size()) {
            blocked();
        }

        for (const auto &key : binding_fields) {
            if (!binding.contains(key)) {
                blocked();
            }
        }

        if (binding.at("schema") != 1 || binding.at("basis") != "api28_wms_process_viewdump_v1" ||
            binding.at("boot_id") != observation.at("boot_id") ||
            binding.at("observer_epoch") != observation.at("source_instance") ||
            !screen->contains("display_epoch") ||
            binding.at("display_epoch") != screen->at("display_epoch") ||
            !screen->contains("display_id") ||
            binding.at("display_id") != screen->at("display_id") || binding.at("display_id") != 0 ||
            binding.at("window_id") != matched_window->at("id") ||
            binding.at("package") != package || binding.at("process_name") != package ||
            binding.at("session_uid") != 1000 || binding.at("owner_uid") != 1000) {
            blocked();
        }

        for (const char *key : {"boot_id", "observer_epoch", "display_epoch", "session_id",
                                "window_token", "window_client", "root_id"}) {
            if (!binding.at(key).is_string() || binding.at(key).get<std::string>().empty() ||
                binding.at(key).get<std::string>().size() > 256) {
                blocked();
            }
        }

        if (!binding.at("session_pid").is_number_integer() ||
            binding.at("session_pid").get<std::int64_t>() <= 0 ||
            binding.at("session_pid").get<std::int64_t>() > 2147483647 ||
            !binding.at("node_count").is_number_integer() ||
            binding.at("node_count").get<std::int64_t>() <= 0 ||
            binding.at("node_count").get<std::int64_t>() > 12000) {
            blocked();
        }

        for (const char *key : {"read_complete", "process_verified", "current_focus", "touchable",
                                "focusable", "visible", "has_surface", "drawn", "view_visible"}) {
            if (binding.at(key) != true) {
                blocked();
            }
        }

        if (binding.at("obscured") != false ||
            matched_window->value("read_complete", Json()) != true ||
            !matched_window->contains("root_id") ||
            matched_window->at("root_id") != binding.at("root_id") ||
            !matched_window->contains("node_count") ||
            matched_window->at("node_count") != binding.at("node_count")) {
            blocked();
        }

        std::size_t count = 0, roots = 0;

        for (const auto &node : nodes) {
            if (node.value("window_id", Json()) == matched_window->at("id")) {
                ++count;

                if (node.value("id", Json()) == binding.at("root_id") && node.contains("parent") &&
                    node.at("parent").is_null() && node.value("package", Json()) == package) {
                    ++roots;
                }
            }
        }

        if (roots != 1 || count != binding.at("node_count").get<std::size_t>()) {
            blocked();
        }
    }

    const Json *match = nullptr;
    std::size_t count = 0;

    for (const auto &node : nodes) {
        bool selected = matches(node, target);

        if (request.contains("selector")) {
            const auto &selector = request.at("selector");
            selected = node.value("resource_id", Json()) == selector.at("resource_id") &&
                       (!selector.contains("package") ||
                        node.value("package", Json()) == selector.at("package"));
        }

        if (selected) {
            ++count;
            match = &node;
        }
    }

    if (count != 1 || !match) {
        details["target_count"] = std::min<std::size_t>(count, 2147483647);
        blocked(count ? "target_ambiguous" : "target_missing");
    }

    if (!matches(*match, target) || match->at("window_id") != matched_window->at("id")) {
        stale("target_changed");
    }

    if (!match->value("visible", false) || !match->value("enabled", false)) {
        blocked();
    }

    const auto intent = request.value("intent", std::string("tap"));

    if (intent != "tap" && intent != "scroll" && intent != "drag") {
        argument();
    }

    if (input_intent == InputIntent::Target && intent == "tap" &&
        !match->value("clickable", false)) {
        blocked();
    }

    if (intent == "scroll" && !match->value("scrollable", false)) {
        blocked();
    }

    if (intent == "drag" && !match->value("clickable", false) &&
        !match->value("long_clickable", false)) {
        blocked();
    }

    const auto &bounds = match->at("clipped_bounds");

    if (!bounds.is_object() || bounds.size() != 4) {
        blocked();
    }

    for (const char *key : {"x", "y", "width", "height"}) {
        if (!bounds.contains(key) || !fence_integer(bounds.at(key), 0, 3840)) {
            blocked();
        }
    }

    const auto x = bounds.at("x").get<int>(), y = bounds.at("y").get<int>(),
               width = bounds.at("width").get<int>(), height = bounds.at("height").get<int>();

    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > screen->at("width").get<int>() ||
        y + height > screen->at("height").get<int>()) {
        blocked();
    }

    if (verified_target) {
        const auto &rectangle = matched_window->at("bounds");

        if (!rectangle.is_object() || rectangle.size() != 4) {
            blocked();
        }

        for (const char *key : {"x", "y", "width", "height"}) {
            if (!rectangle.contains(key) || !fence_integer(rectangle.at(key), 0, 3840)) {
                blocked();
            }
        }

        const auto left = rectangle.at("x").get<int>(), top = rectangle.at("y").get<int>(),
                   w = rectangle.at("width").get<int>(), h = rectangle.at("height").get<int>();

        if (w <= 0 || h <= 0 || x < left || y < top || x + width > left + w ||
            y + height > top + h) {
            blocked();
        }
    }

    int px = x + width / 2, py = y + height / 2;

    if (request.contains("x") || request.contains("y")) {
        if (!request.contains("x") || !request.contains("y") || !fence_int(request.at("x")) ||
            !fence_int(request.at("y"))) {
            argument();
        }

        px = request.at("x").get<int>();
        py = request.at("y").get<int>();

        if (px < x || px >= x + width || py < y || py >= y + height) {
            stale("point_outside");
        }
    }

    if (!verified_target) {
        for (const auto &other : windows) {
            if (&other == matched_window) {
                continue;
            }

            if (!other.contains("bounds") || !other.at("bounds").is_object()) {
                blocked();
            }

            const auto &b = other.at("bounds");

            for (const char *key : {"x", "y", "width", "height"}) {
                if (!b.contains(key) || !fence_int(b.at(key))) {
                    blocked();
                }
            }

            const auto left = b.at("x").get<std::int64_t>(), top = b.at("y").get<std::int64_t>(),
                       w = b.at("width").get<std::int64_t>(),
                       h = b.at("height").get<std::int64_t>();

            if (px < left || py < top || px >= left + w || py >= top + h) {
                continue;
            }

            if (!other.contains("layer") || !matched_window->contains("layer") ||
                !other.at("layer").is_number_integer() ||
                !matched_window->at("layer").is_number_integer() ||
                other.at("layer").get<int>() >= matched_window->at("layer").get<int>()) {
                blocked();
            }
        }
    }

    return {{"x", px}, {"y", py}, {"at_ms", 0}};
}
} // namespace voyemu::api
