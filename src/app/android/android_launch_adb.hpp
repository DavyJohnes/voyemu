#pragma once
#include "model.hpp"

namespace voyemu {
// Call only for a trusted ready-machine launch. Builder recipes still use SDK adb.
inline void normalize_ready_android_adb(Json &launch) {
    if (launch.contains("adb") ||
        (launch.contains("adb_transport") && launch.at("adb_transport") != "direct")) {
        throw VoyemuError("LAUNCH_SCHEMA", "direct Android ADB transport required");
    }

    launch["adb_transport"] = "direct";
}
} // namespace voyemu
