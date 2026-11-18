#pragma once
#include "model.hpp"
#include <algorithm>
#include <stdexcept>
namespace voyemu {
inline int passion_qnx_wheel_index(std::string_view id) {
    if (id == "menu") {
        return 323;
    }

    if (id == "ok") {
        return 324;
    }

    if (id == "volume_up") {
        return 325;
    }

    if (id == "volume_down") {
        return 326;
    }

    if (id == "previous") {
        return 327;
    }

    if (id == "next") {
        return 328;
    }

    if (id == "phone") {
        return 330;
    }

    if (id == "voice") {
        return 331;
    }

    return 0;
}

inline Json passion_wheel_routes() {
    return {{"ok", 85}, {"next", 87}, {"previous", 88}, {"volume_up", 24}, {"volume_down", 25}};
}

inline Json passion_wheel_partial_capability() {
    return {{"schema", 1},
            {"profile", "passion-11-russian-20260403-2.1.4.80"},
            {"status", "partial"},
            {"supported", true},
            {"transport", "android-media-keys"},
            {"authority", "com.voyemu.input.wheel"},
            {"routes", passion_wheel_routes()}};
}

inline void passion_wheel_validate(const Json &android) {
    if (!android.contains("wheel_capability")) {
        if (android.contains("wheel_adapter")) {
            throw VoyemuError("PASSION_WHEEL", "unbound provider");
        }

        return;
    }

    constexpr auto id = "passion-11-russian-20260403-2.1.4.80";
    const auto partial = passion_wheel_partial_capability();
    const Json unsupported = {{"schema", 1},
                              {"profile", id},
                              {"status", "unsupported"},
                              {"supported", false},
                              {"reason", "provider_not_installed"}};

    if (android.at("wheel_capability") == partial &&
        android.value("wheel_adapter", Json{}) ==
                Json{{"authority", "com.voyemu.input.wheel"}, {"routes", passion_wheel_routes()}}) {
        return;
    }

    if (android.at("wheel_capability") == unsupported && !android.contains("wheel_adapter")) {
        return;
    }

    throw VoyemuError("PASSION_WHEEL", "invalid capability binding");
}

inline Json passion_wheel_capability(const Json &profile) {
    constexpr auto id = "passion-11-russian-20260403-2.1.4.80";

    if (profile.value("id", std::string{}) != id) {
        throw VoyemuError("PASSION_WHEEL", "exact profile required");
    }

    const auto components = profile.value("components", Json::object());

    if (!components.contains("wheel-input")) {
        return {{"schema", 1},
                {"profile", id},
                {"status", "unsupported"},
                {"supported", false},
                {"reason", "provider_not_installed"}};
    }

    const Json expected = {{"artifact", "component:wheel-input"},
                           {"type", "file"},
                           {"target", "/system/priv-app/VoyemuPassionWheel/VoyemuPassionWheel.apk"},
                           {"mode", "0644"}};
    const auto nodes = profile.value("install", Json::array());

    if (components.at("wheel-input") != "components/wheel-input" || !nodes.is_array() ||
        std::count(nodes.begin(), nodes.end(), expected) != 1 ||
        std::count_if(nodes.begin(), nodes.end(), [](const auto &n) {
            return n.value("artifact", std::string{}) == "component:wheel-input" ||
                   n.value("target", std::string{}) ==
                           "/system/priv-app/VoyemuPassionWheel/VoyemuPassionWheel.apk";
        }) != 1) {
        throw VoyemuError("PASSION_WHEEL", "exact provider install required");
    }

    return passion_wheel_partial_capability();
}
} // namespace voyemu
