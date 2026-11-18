#pragma once
#include "model.hpp"
#include <set>
#include <stdexcept>
namespace voyemu::runtime {
// Common full-tree normalization for public reads and supervisor snapshots.
inline Json normalize_ui_observation(Json result) {
    auto reject = [] {
        throw VoyemuError("TREE_PROTOCOL", "invalid bounded UI observation");
    };

    if (!result.is_object() || result.value("schema", 0) != 1 || !result.value("ok", false) ||
        !result.contains("surfaces") || !result.at("surfaces").is_array() ||
        result.at("surfaces").size() != 2) {
        reject();
    }

    for (const char *key : {"boot_id", "observer_epoch", "observation_id"}) {
        if (!result.contains(key) || !result.at(key).is_string() ||
            result.at(key).get<std::string>().empty()) {
            reject();
        }
    }

    std::set<std::string> seen;
    bool complete = true, observed = false;
    auto rectangle = [&](Json &object, const char *key) {
        if (!object.contains(key) || object.at(key).is_null()) {
            return;
        }

        const auto b = object.at(key);

        if (!b.is_array() || b.size() != 4) {
            reject();
        }

        for (const auto &coordinate : b) {
            if (!coordinate.is_number_integer()) {
                reject();
            }
        }

        const auto x = b[0].get<std::int64_t>(), y = b[1].get<std::int64_t>(),
                   right = b[2].get<std::int64_t>(), bottom = b[3].get<std::int64_t>();

        if (x < -1000000 || y < -1000000 || right > 1000000 || bottom > 1000000 || right < x ||
            bottom < y) {
            reject();
        }

        object[key] = {{"x", x}, {"y", y}, {"width", right - x}, {"height", bottom - y}};
    };

    for (auto &surface : result.at("surfaces")) {
        const auto name = surface.value("surface", std::string{});

        if ((name != "media_primary" && name != "media_secondary") || !seen.insert(name).second) {
            reject();
        }

        const int slot = name == "media_primary" ? 0 : 1;
        const std::string screen = slot == 0 ? "primary" : "secondary";

        if ((surface.contains("screen") && surface.at("screen") != screen) ||
            (surface.contains("sdk_display_id") && surface.at("sdk_display_id") != slot)) {
            reject();
        }

        if (!surface.contains("display_id") || !surface.at("display_id").is_number_integer() ||
            surface.at("display_id").get<int>() < 0 ||
            (slot == 0 && surface.at("display_id") != 0)) {
            reject();
        }

        surface["screen"] = screen;
        surface["sdk_display_id"] = slot;
        surface.erase("surface");
        const auto status = surface.value("status", std::string{});

        if (status == "unsupported") {
            complete = false;
            continue;
        }

        if ((status != "ok" && status != "partial") || !surface.contains("nodes") ||
            !surface.at("nodes").is_array() || surface.at("nodes").size() > 12000 ||
            !surface.contains("windows") || !surface.at("windows").is_array() ||
            surface.at("windows").size() > 64) {
            reject();
        }

        for (const auto &[key, maximum] : std::initializer_list<std::pair<const char *, int>>{
                     {"width", 3840}, {"height", 2160}}) {
            if (!surface.contains(key) || !surface.at(key).is_number_integer() ||
                (surface.at(key).is_number_unsigned()
                         ? surface.at(key).get<std::uint64_t>() >
                                   static_cast<std::uint64_t>(maximum)
                         : surface.at(key).get<std::int64_t>() > maximum) ||
                (!surface.at(key).is_number_unsigned() &&
                 surface.at(key).get<std::int64_t>() <= 0) ||
                surface.at(key) == 0) {
                reject();
            }
        }

        observed = true;
        complete &= surface.value("complete", false);

        for (auto &window : surface.at("windows")) {
            for (const char *key : {"bounds", "frame"}) {
                rectangle(window, key);
            }
        }

        for (auto &node : surface.at("nodes")) {
            if (node.value("text_redacted", false)) {
                for (const char *key : {"text", "description"}) {
                    if (node.contains(key) && !node.at(key).is_null() && node.at(key) != "") {
                        reject();
                    }
                }
            }

            rectangle(node, "bounds");
            rectangle(node, "clipped_bounds");
        }
    }

    if (!observed) {
        throw VoyemuError("TEST_UNSUPPORTED", "no verified UI backend");
    }

    result.erase("ok");
    result.erase("schema");
    result["format"] = "voyemu.ui.v1";
    result["source"] = "android.ui";
    result["screens"] = std::move(result["surfaces"]);
    result.erase("surfaces");
    result["complete"] = complete;
    result["atomic_ui_state"] = false;
    result["status"] = complete ? "ready" : "partial";
    result["source_clock_domain"] = "android.uptime";
    result["source_instance"] = result.at("observer_epoch");
    result["revision"] = result.at("observation_id");

    for (const char *key : {"started_uptime_ms", "completed_uptime_ms"}) {
        if (result.contains(key) && result.at(key).is_number_integer()) {
            if (result.at(key).get<std::int64_t>() < 0) {
                reject();
            }

            result[key] = std::to_string(result.at(key).get<std::uint64_t>());
        }
    }

    if (result.contains("completed_uptime_ms")) {
        result["source_timestamp_ms"] = result.at("completed_uptime_ms");
    }

    return result;
}
} // namespace voyemu::runtime
