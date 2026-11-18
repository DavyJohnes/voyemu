#pragma once
#include "model.hpp"
#include <stdexcept>
#include <string_view>

namespace voyemu {
inline constexpr const char *nxp_profile_id = "free-09-chinese-20221230-nxp-rc45";
inline constexpr const char *nxp_home =
        "com.qinggan.app.launcher/com.qinggan.app.launcher.activity.MainActivity";

inline bool nxp_clock_service_ready(bool boot_complete, int exit_code, bool timed_out,
                                    std::string_view output) {
    return boot_complete && !timed_out && exit_code == 0 && output == "Service alarm: found\n";
}

inline bool nxp_key_manager_service_ready(int exit_code, bool timed_out, std::string_view output) {
    return !timed_out && exit_code == 0 && output == "Service qg.key_manager: found\n";
}

inline Json nxp_presentation_contract() {
    return {{"family", nxp_profile_id},
            {"vehicle_state", "unsupported"},
            {"wheel", "unsupported"},
            {"language", "unsupported"},
            {"appearance", "unsupported"}};
}

inline Json nxp_wheel_routes() {
    return {{"ica", nullptr},
            {"acc", nullptr},
            {"distance", nullptr},
            {"cruise_up", nullptr},
            {"cruise_down", nullptr},
            {"cancel", nullptr},
            {"night_vision", nullptr},
            {"custom", 3090},
            {"menu", 3074},
            {"voice", 130},
            {"volume_up", 2},
            {"ok", 6},
            {"volume_down", 1},
            {"previous", 4},
            {"next", 3},
            {"photo", 142},
            {"phone", 128}};
}

inline Json nxp_wheel_adapter() {
    return {{"authority", "com.voyemu.input.wheel"},
            {"component", "com.voyemu.oem/.RegistrationService"},
            {"action", "com.voyemu.input.action.WHEEL_INPUT_V1"},
            {"receipt_path", "/data/user/0/com.voyemu.oem/files/wheel-receipt.json"},
            {"routes", nxp_wheel_routes()}};
}

inline bool nxp_presentation(const Json &android) {
    if (!android.contains("presentation_adapter") ||
        android.at("presentation_adapter").value("family", std::string{}) != nxp_profile_id) {
        return false;
    }

    if (android.at("presentation_adapter") != nxp_presentation_contract() ||
        android.value("api", 0) != 28 ||
        android.value("home", Json::object()) !=
                Json{{"package", "com.qinggan.app.launcher"}, {"component", nxp_home}} ||
        android.contains("vehicle_adapter") || android.contains("systemui_adapter")) {
        throw VoyemuError("NXP_PRESENTATION", "exact API28 OEM presentation contract required");
    }

    if (android.contains("wheel_adapter") && android.at("wheel_adapter") != nxp_wheel_adapter()) {
        throw VoyemuError("NXP_WHEEL", "exact NXP key contract required");
    }

    return true;
}
} // namespace voyemu
