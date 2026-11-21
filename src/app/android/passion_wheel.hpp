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

// The partial capability names the profile whose wheel the provider serves.
inline Json passion_wheel_partial_capability(std::string_view profile) {
    return {{"schema", 1},
            {"profile", profile},
            {"status", "partial"},
            {"supported", true},
            {"transport", "android-media-keys"},
            {"authority", "com.voyemu.input.wheel"},
            {"routes", passion_wheel_routes()}};
}

// Whether capability is the partial capability of the valid profile name it carries.
inline bool passion_wheel_partial(const Json &capability) {
    if (!capability.is_object() ||
        !valid_profile_name(capability.value("profile", std::string{}))) {
        return false;
    }

    return capability ==
           passion_wheel_partial_capability(capability.value("profile", std::string{}));
}

inline void passion_wheel_validate(const Json &android) {
    if (!android.contains("wheel_capability")) {
        if (android.contains("wheel_adapter")) {
            throw VoyemuError("PASSION_WHEEL", "unbound provider");
        }

        return;
    }

    const auto id = android.at("wheel_capability").is_object()
                            ? android.at("wheel_capability").value("profile", std::string{})
                            : std::string{};
    const auto partial = passion_wheel_partial_capability(id);
    const Json unsupported = {{"schema", 1},
                              {"profile", id},
                              {"status", "unsupported"},
                              {"supported", false},
                              {"reason", "provider_not_installed"}};

    if (!valid_profile_name(id)) {
        throw VoyemuError("PASSION_WHEEL", "invalid capability binding");
    }

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
    const auto id = profile.value("id", std::string{});

    if (profile.value("platform", Json()) != "passion" || !valid_profile_name(id)) {
        throw VoyemuError("PASSION_WHEEL", "Passion profile required");
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

    return passion_wheel_partial_capability(id);
}
} // namespace voyemu
