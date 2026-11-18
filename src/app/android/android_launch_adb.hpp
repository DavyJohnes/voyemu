#pragma once
#include "model.hpp"

namespace voyemu {
inline bool legacy_android_sdk_adb(const Json &launch) {
    const std::filesystem::path sdk = launch.value("android_sdk_root", std::string{});

#ifdef _WIN32
    const auto expected = sdk / "platform-tools" / "adb.exe";
#else
    const auto expected = sdk / "platform-tools" / "adb";
#endif

    return sdk.is_absolute() &&
           std::filesystem::path(launch.value("adb", std::string{})) == expected;
}

// Call only for a trusted ready-machine launch. Builder recipes still use SDK adb.
inline void normalize_ready_android_adb(Json &launch) {
    if (launch.contains("adb_transport") && launch.at("adb_transport") != "direct") {
        throw VoyemuError("LAUNCH_SCHEMA", "direct Android ADB transport required");
    }

    if (launch.contains("adb")) {
        if (legacy_android_sdk_adb(launch) && launch.contains("pins") &&
            launch.at("pins").is_array()) {
            const auto &old = launch.at("adb");
            auto &pins = launch.at("pins");

            for (auto item = pins.begin(); item != pins.end();) {
                if (item->is_object() && item->value("path", Json{}) == old) {
                    item = pins.erase(item);
                } else {
                    ++item;
                }
            }
        }

        launch.erase("adb");
    }

    launch["adb_transport"] = "direct";
}
} // namespace voyemu
