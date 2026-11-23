#pragma once
#include "api30_framework_boot.hpp"
#include "check.hpp"
#include "model.hpp"
#include "profile_catalog.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace voyemu {
// Whether a qinggan11 profile delivers its wheel through the com.voyemu.input.wheel provider, as
// wheel.provider of profile.json declares.
inline bool qinggan11_wheel_provider(const Json &definition) {
    const auto wheel = definition.value("wheel", Json::object());

    return definition.value("platform", Json()) == "qinggan11" && wheel.is_object() &&
           wheel.value("provider", Json()) == "com.voyemu.input.wheel";
}

// A qinggan11 profile with the system_upgrade feature delivers its Camera and Navi as packages of
// the machine and rebuilds them with its Android system.
inline bool qinggan11_package_delivery(const Json &definition) {
    return definition.value("platform", Json()) == "qinggan11" &&
           profile_feature(definition, "system_upgrade");
}

// The same for a bundled profile; false for a profile that is not bundled.
inline bool qinggan11_wheel_provider_profile(std::string_view profile) {
    return embedded_profile_declares(profile, ProfilePlatform::qinggan11, ProfileBoard::sa8155) &&
           qinggan11_wheel_provider(embedded_profile(profile).at("definition"));
}

// A pinned OEM original of a profile: its file, size, SHA-256 and system image path are the
// profile's own data.
inline bool qinggan11_pinned_original(const Json &original, const std::string &image = "system") {
    if (!original.is_object()) {
        return false;
    }

    const auto file = original.value("file", Json{}), bytes = original.value("bytes", Json{}),
               sha256 = original.value("sha256", Json{}),
               acquisition = original.value("acquisition", Json{});

    return file.is_string() && !file.get<std::string>().empty() && bytes.is_number_unsigned() &&
           sha256.is_string() && sha256_text(sha256.get<std::string>()) &&
           acquisition.is_object() && acquisition.value("image", Json{}) == image &&
           acquisition.value("path", Json{}).is_string() &&
           acquisition.at("path").get<std::string>().starts_with("/" + image + "/");
}

inline Json qinggan11_startup_values(const Json &definition) {
    const auto status = definition.value("status", std::string{});

    if (definition.value("schema", 0) != 1 || !qinggan11_wheel_provider(definition) ||
        (status != "incomplete" && status != "ready") ||
        definition.value("android", Json::object()).value("package", std::string{}) !=
                "system-images;android-30;default;arm64-v8a" ||
        definition.value("configure", Json::object()).value("home", std::string{}) !=
                "com.qinggan.app.launcher" ||
        definition.value("configure", Json::object()).value("homeActivity", std::string{}) !=
                "com.qinggan.app.launcher/com.qinggan.app.launcher.activity.MainActivity") {
        throw VoyemuError("QINGGAN11_STARTUP",
                          "exact supported API30 source and OEM HOME required");
    }

    const auto values = definition.value("initial_state", Json::object());

    if (!values.is_object()) {
        throw VoyemuError("QINGGAN11_STARTUP", "source initial_state must be an object");
    }

    return values;
}

// A qinggan11 Android of API 30 with the OEM HOME; the platform is the one the embedded reference
// profile declares.
inline bool qinggan11_presentation(const Json &android) {
    return embedded_profile_declares(android.value("reference_profile", std::string{}),
                                     ProfilePlatform::qinggan11, ProfileBoard::sa8155) &&
           android.value("api", 0) == 30 &&
           android.value("home", Json::object()) ==
                   Json{{"package", "com.qinggan.app.launcher"},
                        {"component",
                         "com.qinggan.app.launcher/com.qinggan.app.launcher.activity.MainActivity"}};
}

inline Json qinggan11_wheel_adapter() {
    return {{"authority", "com.voyemu.input.wheel"},
            {"routes",
             {{"volume_up", 2},
              {"volume_down", 1},
              {"next", 3},
              {"previous", 4},
              {"ok", 6},
              {"photo", 173},
              {"voice", 130},
              {"phone", 128}}}};
}

inline Json qinggan11_partial_wheel_capability(std::string_view profile) {
    return {{"schema", 1},
            {"profile", profile},
            {"status", "partial"},
            {"supported", true},
            {"authority", "com.voyemu.input.wheel"},
            {"routes", qinggan11_wheel_adapter().at("routes")}};
}

// A partial capability of any valid profile name is accepted at runtime; preparation derives it
// from the wheel of that profile.
inline bool qinggan11_partial_wheel_capability_matches(const Json &capability) {
    if (!capability.is_object() || capability.value("status", std::string{}) != "partial" ||
        !capability.value("supported", false) ||
        !valid_profile_name(capability.value("profile", std::string{}))) {
        return false;
    }

    auto reference = qinggan11_partial_wheel_capability(capability.value("profile", std::string{}));

    return capability == reference;
}

inline Json qinggan11_wheel_capability(const Json &definition) {
    const std::string profile_id = definition.value("id", std::string{});

    (void)qinggan11_startup_values(definition);
    const bool original_navi = original_navi_requested(definition);
    const Json components = {{"canbus", "components/canbus"},
                             {"qg-runtime", "components/qg-runtime"},
                             {"bootstrap", "components/bootstrap"}};
    auto reviewed = components;
    reviewed["wheel-input"] = "components/wheel-input";
    reviewed["wheel-platform"] = "components/wheel-platform";
    auto declared = definition.value("components", Json{});
    const bool observation = declared.is_object() && declared.contains("view-observation");

    if (observation) {
        Json install = {{"artifact", "component:view-observation"},
                        {"type", "file"},
                        {"target", "/system/framework/framework.jar"},
                        {"mode", "0644"}};

        if (original_navi) {
            api30_framework_boot::verify(definition);

            for (auto p = definition.at("patches").begin(); p != definition.at("patches").end();
                 ++p) {
                if (p.value().value("input", std::string{}) == "component:view-observation" &&
                    p.value().value("type", std::string{}) == "jar" &&
                    p.value().value("steps", Json::array()) ==
                            Json::array({"original-navi-egl.json"})) {
                    install["artifact"] = "patched:" + p.key();
                }
            }
        }

        const auto nodes = definition.value("install", Json{});

        if (declared.at("view-observation") != "components/view-observation" || !nodes.is_array() ||
            std::count(nodes.begin(), nodes.end(), install) != 1 ||
            std::count_if(nodes.begin(), nodes.end(),
                          [](const auto &node) {
                              return node.value("artifact", std::string{}) ==
                                     "component:view-observation";
                          }) != (original_navi ? 0 : 1) ||
            std::count_if(nodes.begin(), nodes.end(), [](const auto &node) {
                return node.value("target", std::string{}) == "/system/framework/framework.jar";
            }) != 1) {
            throw VoyemuError("QINGGAN11_WHEEL", "exact optional view observation graph required");
        }

        declared.erase("view-observation");
    }

    const bool power = declared.is_object() && declared.contains("oem-power");

    if (power) {
        const Json install = {{"artifact", "component:oem-power"},
                              {"type", "apk"},
                              {"target", "/system/priv-app/VoyemuOemPower/VoyemuOemPower.apk"},
                              {"package", "com.voyemu.oem"}};
        const auto nodes = definition.value("install", Json{}),
                   start = definition.value("configure", Json::object()).value("start", Json{});

        if (declared.at("oem-power") != "components/oem-power" ||
            !qinggan11_pinned_original(
                    definition.value("original", Json::object()).value("QGAPI", Json{})) ||
            !nodes.is_array() || std::count(nodes.begin(), nodes.end(), install) != 1 ||
            std::count_if(nodes.begin(), nodes.end(),
                          [](const auto &n) {
                              return n.value("artifact", std::string{}) == "component:oem-power";
                          }) != 1 ||
            !start.is_array() || start.empty() ||
            start.at(0) != Json{{"component", "com.voyemu.oem/.RegistrationService"},
                                {"mode", "service"}}) {
            throw VoyemuError("QINGGAN11_WHEEL", "exact optional OEM power graph required");
        }

        declared.erase("oem-power");
    }

    const auto patches = definition.value("patches", Json::object()),
               nodes = definition.value("install", Json::array());
    const bool phone =
            (declared.is_object() && declared.contains("bluetooth")) ||
            (patches.is_object() && patches.contains("BluetoothPhone")) ||
            (nodes.is_array() && std::any_of(nodes.begin(), nodes.end(), [](const auto &node) {
                 const auto artifact = node.value("artifact", Json{}),
                            target = node.value("target", Json{});

                 return artifact == "component:bluetooth" || artifact == "patched:BluetoothPhone" ||
                        artifact == "original:BluetoothPhone" ||
                        target ==
                                "/system/priv-app/BluetoothPhone-release-signed/BluetoothPhone-release-signed.apk" ||
                        node.value("package", Json{}) == "com.qinggan.bluetoothphone";
             }));

    if (phone) {
        const Json patch = {{"type", "apk"},
                            {"input", "original:BluetoothPhone"},
                            {"steps", Json::array({"api30-phone-provider.json"})},
                            {"sign", "platform"}};
        const Json provider_install = {
                {"artifact", "patched:BluetoothPhone"},
                {"type", "apk"},
                {"target",
                 "/system/priv-app/BluetoothPhone-release-signed/BluetoothPhone-release-signed.apk"},
                {"package", "com.qinggan.bluetoothphone"},
                {"mode", "0644"}};
        const Json peer_install = {{"artifact", "component:bluetooth"},
                                   {"type", "file"},
                                   {"target", "/system/framework/Ivt8BluetoothAdapter.jar"},
                                   {"mode", "0644"}};

        if (!declared.is_object() ||
            declared.value("bluetooth", Json{}) != "components/bluetooth" || !patches.is_object() ||
            patches.value("BluetoothPhone", Json{}) != patch ||
            !definition.value("original", Json::object())
                     .value("BluetoothPhone", Json{})
                     .is_object() ||
            !nodes.is_array()) {
            throw VoyemuError("QINGGAN11_WHEEL", "exact optional Phone source graph required");
        }

        unsigned providers = 0, peers = 0;

        for (const auto &node : nodes) {
            const auto artifact = node.value("artifact", Json{}),
                       target = node.value("target", Json{});

            if (artifact == "original:BluetoothPhone") {
                throw VoyemuError("QINGGAN11_WHEEL", "original Phone cannot replace its provider");
            }

            if (artifact == "patched:BluetoothPhone" ||
                target ==
                        "/system/priv-app/BluetoothPhone-release-signed/BluetoothPhone-release-signed.apk" ||
                node.value("package", Json{}) == "com.qinggan.bluetoothphone") {
                if (node != provider_install || ++providers != 1) {
                    throw VoyemuError("QINGGAN11_WHEEL",
                                      "exact unique Phone provider install required");
                }
            }

            if (artifact == "component:bluetooth" ||
                target == "/system/framework/Ivt8BluetoothAdapter.jar") {
                if (node != peer_install || ++peers != 1) {
                    throw VoyemuError("QINGGAN11_WHEEL",
                                      "exact unique Phone peer install required");
                }
            }
        }

        if (providers != 1 || peers != 1) {
            throw VoyemuError("QINGGAN11_WHEEL", "complete optional Phone install graph required");
        }

        declared.erase("bluetooth");
    }

    if (declared.is_object() && declared.contains("dvr-arm64-libraries")) {
        if (declared.at("dvr-arm64-libraries") != "components/dvr-arm64-libraries" ||
            !qinggan11_pinned_original(
                    definition.value("original", Json::object()).value("Dvr", Json{}), "vendor") ||
            definition.value("patches", Json::object()).value("Dvr", Json{}) !=
                    Json{{"type", "apk"},
                         {"input", "original:Dvr"},
                         {"uses", Json::array({"component:dvr-arm64-libraries"})},
                         {"steps", profile_id == "free-11-chinese-20260731-7.3.1"
                                           ? Json::array({"api30-dvr-arm64.json",
                                                          "api30-framework-libraries.json",
                                                          "api30-dvr-camera-virtual.json"})
                                           : Json::array({"api30-dvr-arm64.json"})}} ||
            !definition.contains("install") || !definition.at("install").is_array() ||
            std::count(definition.at("install").begin(), definition.at("install").end(),
                       Json{{"artifact", "patched:Dvr"}, {"type", "apk"}}) != 1 ||
            std::count_if(definition.at("install").begin(), definition.at("install").end(),
                          [](const auto &n) {
                              const auto artifact = n.value("artifact", std::string{});

                              return artifact == "patched:Dvr" || artifact == "original:Dvr";
                          }) != 1) {
            throw VoyemuError("QINGGAN11_WHEEL", "exact optional DVR build dependency required");
        }

        declared.erase("dvr-arm64-libraries");
    }

    const bool can_early_gallery = declared.is_object() && declared.is_object() &&
                                   declared.contains("can-simulator");

    if (declared.is_object() && declared.contains("gallery-arm64-libraries")) {
        if (declared.size() != (can_early_gallery ? 9 : 6) ||
            declared.at("gallery-arm64-libraries") != "components/gallery-arm64-libraries" ||
            !qinggan11_pinned_original(
                    definition.value("original", Json::object()).value("Gallery", Json{})) ||
            definition.value("patches", Json::object()).value("Gallery", Json{}) !=
                    Json{{"type", "apk"},
                         {"input", "original:Gallery"},
                         {"uses", Json::array({"component:gallery-arm64-libraries"})},
                         {"steps", Json::array({"api30-gallery-arm64.json"})}} ||
            !definition.contains("install") || !definition.at("install").is_array() ||
            std::count(definition.at("install").begin(), definition.at("install").end(),
                       Json{{"artifact", "patched:Gallery"}, {"type", "apk"}}) != 1 ||
            std::count_if(definition.at("install").begin(), definition.at("install").end(),
                          [](const auto &n) {
                              return n.value("artifact", std::string{}) == "patched:Gallery";
                          }) != 1 ||
            std::any_of(definition.at("install").begin(), definition.at("install").end(),
                        [](const auto &n) {
                            return n.value("artifact", std::string{}) == "original:Gallery";
                        })) {
            throw VoyemuError("QINGGAN11_WHEEL",
                              "exact optional Gallery build dependency required");
        }

        declared.erase("gallery-arm64-libraries");

        if (declared != (can_early_gallery ? [&] {
                auto widened = reviewed;
                widened["can-simulator"] = "components/can-simulator";
                widened["early-inputs"] = "components/early-inputs";
                widened["can-model-observer"] = "components/can-model-observer";

                return widened;
            }()
                                           : reviewed)) {
            throw VoyemuError("QINGGAN11_WHEEL",
                              "Gallery does not replace the reviewed wheel graph");
        }
    }

    const bool can_early = declared.is_object() && declared.contains("can-simulator");

    if (can_early) {
        for (const auto *item : {"can-simulator", "early-inputs", "can-model-observer"}) {
            if (declared.value(item, Json{}) != Json(std::string("components/") + item)) {
                throw VoyemuError("QINGGAN11_WHEEL", "exact can-early native component required");
            }

            declared.erase(item);
        }
    }

    const bool installed = declared == reviewed;

    if (power && !installed) {
        throw VoyemuError("QINGGAN11_WHEEL", "OEM power requires reviewed wheel graph");
    }

    auto selected_components = installed ? reviewed : components;

    if (power) {
        selected_components["oem-power"] = "components/oem-power";
    }

    if (observation) {
        selected_components["view-observation"] = "components/view-observation";
    }

    if (phone) {
        selected_components["bluetooth"] = "components/bluetooth";
    }

    if (can_early) {
        selected_components["can-simulator"] = "components/can-simulator";
        selected_components["early-inputs"] = "components/early-inputs";
        selected_components["can-model-observer"] = "components/can-model-observer";
    }

    if ((!installed && declared != components) || !definition.contains("install") ||
        !definition.at("install").is_array()) {
        throw VoyemuError("QINGGAN11_WHEEL", "reviewed component graph required");
    }

    Json selected = Json::object();

    for (const auto &node : definition.at("install")) {
        const auto artifact = node.value("artifact", std::string{});

        if (!artifact.starts_with("component:")) {
            continue;
        }

        const auto name = artifact.substr(10);

        if (!selected_components.contains(name) || selected.contains(name)) {
            throw VoyemuError("QINGGAN11_WHEEL", "unknown or duplicate installed component");
        }

        selected[name] = node.at("type");
    }

    auto expected = Json{{"canbus", "apk"}, {"qg-runtime", "file"}, {"bootstrap", "apk"}};

    if (power) {
        expected["oem-power"] = "apk";
    }

    if (observation && !original_navi) {
        expected["view-observation"] = "file";
    }

    if (phone) {
        expected["bluetooth"] = "file";
    }

    if (can_early) {
        expected["can-simulator"] = "file";
        expected["early-inputs"] = "file";
        expected["can-model-observer"] = "file";
    }

    if (installed) {
        expected["wheel-input"] = "apk";
        expected["wheel-platform"] = "file";

        if (!qinggan11_pinned_original(definition.at("original").at("KeyManagerService")) ||
            (definition.at("patches").at("KeyManagerService") !=
                     Json{{"input", "original:KeyManagerService"},
                          {"type", "apk"},
                          {"steps", Json::array({"api30-keymanager.json"})}} &&
             definition.at("patches").at("KeyManagerService") !=
                     Json{{"input", "original:KeyManagerService"},
                          {"type", "apk"},
                          {"steps",
                           Json::array({"api30-keymanager.json", "api30-key-ingress.json"})}})) {
            throw VoyemuError("QINGGAN11_WHEEL", "exact OEM KeyManager original/adapter required");
        }

        bool jar = false, apk = false;

        for (const auto &node : definition.at("install")) {
            if (node.value("artifact", std::string{}) == "component:wheel-platform") {
                jar = node == Json{{"artifact", "component:wheel-platform"},
                                   {"type", "file"},
                                   {"target", "/system/framework/voyemu-free-wheel.jar"},
                                   {"mode", "0644"}};
            }

            if (node.value("artifact", std::string{}) == "patched:KeyManagerService") {
                apk = node ==
                      Json{{"artifact", "patched:KeyManagerService"},
                           {"type", "apk"},
                           {"target",
                            "/system/priv-app/KeyManagerService-release/KeyManagerService-release.apk"},
                           {"package", "com.qinggan.keymanager.service"}};
            }
        }

        if (!jar || !apk) {
            throw VoyemuError("QINGGAN11_WHEEL",
                              "genuine service and own library targets required");
        }
    }

    if (selected != expected) {
        throw VoyemuError("QINGGAN11_WHEEL",
                          "real canbus/bootstrap/runtime install graph required");
    }

    if (installed && definition.at("patches").at("KeyManagerService").at("steps").size() == 2) {
        return qinggan11_partial_wheel_capability(profile_id);
    }

    return {{"schema", 1},
            {"profile", profile_id},
            {"status", "unsupported"},
            {"supported", false},
            {"reason", installed ? "oem_delivery_unverified" : "provider_not_installed"},
            {"authority", "com.voyemu.input.wheel"}};
}

inline bool qinggan11_wheel_unsupported(const Json &android) {
    if (!android.contains("wheel_capability")) {
        return false;
    }

    const std::string capability_profile =
            android.at("wheel_capability").value("profile", std::string{});

    if (android.at("wheel_capability").value("status", std::string{}) == "partial") {
        if (!valid_profile_name(capability_profile) || !qinggan11_presentation(android) ||
            android.at("wheel_capability") !=
                    qinggan11_partial_wheel_capability(capability_profile) ||
            android.value("wheel_adapter", Json{}) != qinggan11_wheel_adapter() ||
            android.value("vehicle_adapter", Json{}) !=
                    Json{{"authority", "com.voyemu.output.vehicle"}}) {
            throw VoyemuError("QINGGAN11_WHEEL",
                              "invalid partial-wheel contract or missing genuine vehicle adapter");
        }

        return false;
    }

    const auto reason = android.at("wheel_capability").value("reason", std::string{});

    if (reason != "provider_not_installed" && reason != "oem_delivery_unverified") {
        throw VoyemuError("QINGGAN11_WHEEL", "unknown unsupported reason");
    }

    const Json expected = {{"schema", 1},
                           {"profile", capability_profile},
                           {"status", "unsupported"},
                           {"supported", false},
                           {"reason", reason},
                           {"authority", "com.voyemu.input.wheel"}};

    if (!valid_profile_name(capability_profile) || !qinggan11_presentation(android) ||
        android.at("wheel_capability") != expected || android.contains("wheel_adapter") ||
        android.value("vehicle_adapter", Json{}) !=
                Json{{"authority", "com.voyemu.output.vehicle"}}) {
        throw VoyemuError("QINGGAN11_WHEEL",
                          "invalid unsupported-wheel contract or missing genuine vehicle adapter");
    }

    return true;
}
} // namespace voyemu
