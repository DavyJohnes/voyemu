#pragma once
#include "model.hpp"
#include <stdexcept>
namespace voyemu::api {
inline Json capture_geometry(const Json &request, const Json &observation) {
    auto blocked = []() {
        throw VoyemuError("TEST_CAPTURE_UNAVAILABLE",
                          "complete unobscured ROI geometry unavailable");
    };
    const auto selector = request.at("selector");

    if (!selector.is_object() || !selector.contains("resource_id") ||
        !selector.at("resource_id").is_string() ||
        selector.at("resource_id").get<std::string>().empty() || selector.dump().size() > 1024) {
        throw VoyemuError("TEST_ARGUMENT",
                          "capture selector requires bounded resource_id and optional package");
    }

    for (const auto &[key, value] : selector.items()) {
        if ((key != "resource_id" && key != "package") || !value.is_string() ||
            value.get<std::string>().empty()) {
            throw VoyemuError("TEST_ARGUMENT", "unsupported capture selector field");
        }
    }

    const Json *screen = nullptr;

    for (const auto &candidate : observation.at("screens")) {
        if (candidate.at("screen") == request.at("screens").at(0)) {
            if (screen) {
                blocked();
            }

            screen = &candidate;
        }
    }

    if (!screen || screen->value("status", std::string{}) != "ok" ||
        !screen->value("complete", false)) {
        blocked();
    }

    const int slot = request.at("screens").at(0) == "primary" ? 0 : 1;

    if (screen->value("sdk_display_id", -1) != slot || !screen->contains("display_id") ||
        !screen->at("display_id").is_number_integer() || screen->value("width", 0) != 1920 ||
        screen->value("height", 0) != 720) {
        blocked();
    }

    const auto guest_display = screen->at("display_id").get<std::int64_t>();
    const bool interactive = screen->value("scope", std::string{}) == "api30_interactive";

    if (interactive) {
        if (screen->value("selection_coverage", std::string{}) != "interactive_windows_only" ||
            guest_display < 0 || guest_display > INT32_MAX ||
            (slot == 0 ? guest_display != 0 : guest_display == 0)) {
            blocked();
        }

        for (const auto &window : screen->at("windows")) {
            if (!window.contains("display_id") || window.at("display_id") != guest_display) {
                blocked();
            }
        }
    } else if (guest_display != slot) {
        blocked();
    }

    const Json *node = nullptr;

    for (const auto &candidate : screen->at("nodes")) {
        if (candidate.value("resource_id", Json()) == selector.at("resource_id") &&
            (!selector.contains("package") ||
             candidate.value("package", Json()) == selector.at("package"))) {
            if (node) {
                blocked();
            }

            node = &candidate;
        }
    }

    if (!node || !node->value("visible", false) || !node->contains("bounds") ||
        !node->contains("clipped_bounds") || node->at("bounds") != node->at("clipped_bounds")) {
        blocked();
    }

    auto rectangle = [&](const Json &b) {
        if (!b.is_object() || b.size() != 4) {
            blocked();
        }

        for (const char *k : {"x", "y", "width", "height"}) {
            if (!b.contains(k) || !b.at(k).is_number_integer() || b.at(k).get<std::int64_t>() < 0 ||
                b.at(k).get<std::int64_t>() > 3840) {
                blocked();
            }
        }
    };
    const auto bounds = node->at("bounds");
    rectangle(bounds);
    const auto x = bounds.at("x").get<int>(), y = bounds.at("y").get<int>(),
               w = bounds.at("width").get<int>(), h = bounds.at("height").get<int>();

    if (w < 1 || h < 1 || x + w > 1920 || y + h > 720) {
        blocked();
    }

    const Json *window = nullptr;

    for (const auto &candidate : screen->at("windows")) {
        if (candidate.at("id") == node->at("window_id")) {
            if (window) {
                blocked();
            }

            window = &candidate;
        }
    }

    if (!window) {
        blocked();
    }

    for (const auto &other : screen->at("windows")) {
        if (&other == window) {
            continue;
        }

        if (!other.contains("bounds")) {
            blocked();
        }

        const auto b = other.at("bounds");
        rectangle(b);

        if (x >= b.at("x").get<int>() + b.at("width").get<int>() ||
            y >= b.at("y").get<int>() + b.at("height").get<int>() ||
            x + w <= b.at("x").get<int>() || y + h <= b.at("y").get<int>()) {
            continue;
        }

        if (!window->contains("layer") || !other.contains("layer") ||
            !window->at("layer").is_number_integer() || !other.at("layer").is_number_integer() ||
            other.at("layer").get<int>() >= window->at("layer").get<int>()) {
            blocked();
        }
    }

    for (const char *key : {"boot_id", "source_instance", "revision"}) {
        if (!observation.contains(key) || !observation.at(key).is_string() ||
            observation.at(key).get<std::string>().empty() ||
            observation.at(key).get<std::string>().size() > 256) {
            blocked();
        }
    }

    for (const char *key : {"id", "type", "bounds", "title", "package"}) {
        if (!window->contains(key)) {
            blocked();
        }
    }

    for (const char *key :
         {"window_id", "resource_id", "package", "class", "bounds", "clipped_bounds"}) {
        if (!node->contains(key)) {
            blocked();
        }
    }

    Json target;

    for (const char *key :
         {"window_id", "resource_id", "package", "class", "text", "bounds", "clipped_bounds"}) {
        if (node->contains(key)) {
            target[key] = node->at(key);
        }
    }

    Json result{{"boot_id", observation.at("boot_id")},
                {"source_instance", observation.at("source_instance")},
                {"revision", observation.at("revision")},
                {"window", *window},
                {"target", target},
                {"rect", bounds}};

    if (interactive) {
        result["selection_coverage"] = "interactive_windows_only";
        result["display_mapping"] = {{"sdk_display_id", slot},
                                     {"guest_display_id", guest_display},
                                     {"basis", "verified_observer_logical_screen"}};
    }

    if (request.contains("expected")) {
        const auto &expected = request.at("expected");

        if (!expected.is_object() || expected.size() != 5) {
            throw VoyemuError("TEST_ARGUMENT",
                              "expected requires boot_id/source_instance/revision/window/target");
        }

        for (const char *key : {"boot_id", "source_instance", "revision", "window", "target"}) {
            if (!expected.contains(key)) {
                throw VoyemuError("TEST_ARGUMENT", "incomplete expected geometry");
            }
        }

        for (const char *key : {"boot_id", "source_instance", "revision"}) {
            if (!expected.at(key).is_string() || expected.at(key).get<std::string>().empty() ||
                expected.at(key).get<std::string>().size() > 256) {
                throw VoyemuError("TEST_ARGUMENT", "expected identities must be bounded strings");
            }
        }

        for (const char *key : {"boot_id", "source_instance"}) {
            if (expected.at(key) != result.at(key)) {
                throw VoyemuError("TEST_CAPTURE_STALE", "expected source changed");
            }
        }

        for (const char *key : {"window", "target"}) {
            if (!expected.at(key).is_object() || expected.at(key).empty() ||
                expected.at(key).dump().size() > 8192) {
                throw VoyemuError("TEST_ARGUMENT", "expected geometry must be an object");
            }

            for (const auto &[field, value] : expected.at(key).items()) {
                if (!result.at(key).contains(field) || result.at(key).at(field) != value) {
                    throw VoyemuError("TEST_CAPTURE_STALE", "expected geometry changed");
                }
            }
        }
    }

    return result;
}

inline void verify_capture_geometry(const Json &before, const Json &after) {
    for (const char *key : {"boot_id", "source_instance", "window", "target", "rect"}) {
        if (before.at(key) != after.at(key)) {
            throw VoyemuError("TEST_CAPTURE_STALE",
                              "ROI source or geometry changed during capture");
        }
    }

    for (const char *key : {"selection_coverage", "display_mapping"}) {
        if (before.value(key, Json()) != after.value(key, Json())) {
            throw VoyemuError("TEST_CAPTURE_STALE",
                              "ROI display mapping or coverage changed during capture");
        }
    }
}
} // namespace voyemu::api
