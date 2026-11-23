#pragma once
#include "check.hpp"
#include "model.hpp"
#include <algorithm>
#include <array>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
namespace voyemu {
inline constexpr std::string_view original_navi_sha =
        "dceddff70db38bacb50ebffa46d36da76f70e5596c6184bca51c48c3ed562a05";
inline constexpr std::string_view original_navi_cert =
        "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3";
inline constexpr std::string_view original_navi_target =
        "/system/priv-app/dueros/Dueros-h97c-signed.apk";

inline void original_navi_need(bool ok) {
    if (!ok) {
        throw VoyemuError("QINGGAN11_ORIGINAL_NAVI", "candidate contract differs");
    }
}

// The stock navigation app of the firmware is installed when the profile declares the
// original_navi feature; its recipes and graph are details of that feature.
inline bool original_navi_requested(const Json &definition) {
    const auto features = definition.value("features", Json::object());

    return features.is_object() && features.value("original_navi", Json(false)) == true;
}

inline bool free731_usb_test_volume_visibility(const Json &definition) {
    const auto features = definition.value("features", Json::object());
    const bool enabled = features.is_object() &&
                         features.value("usb_test_volume_visibility", Json(false)) == true;

    original_navi_need(!enabled ||
                       (definition.value("id", std::string{}) ==
                                "free-11-chinese-20260731-7.3.1" &&
                        original_navi_requested(definition)));

    return enabled;
}

inline Json original_navi_services_steps(const Json &definition) {
    auto steps = Json::array({"original-navi-libraries.json"});

    if (free731_usb_test_volume_visibility(definition)) {
        steps.push_back("usb-mount-visibility.json");
    }

    return steps;
}

inline Json original_navi_qgapi_steps(const Json &definition) {
    auto steps = Json::array({"api30-audio-consumer.json", "original-navi-vin.json"});

    if (free731_usb_test_volume_visibility(definition)) {
        steps.push_back("usb-port-metadata.json");
    }

    return steps;
}

inline Json original_navi_marker() {
    return {{"schema", 1},
            {"artifact", "original:Navi"},
            {"package", "com.baidu.naviauto"},
            {"target", std::string(original_navi_target)},
            {"sha256", std::string(original_navi_sha)},
            {"certificate_sha256", std::string(original_navi_cert)},
            {"historical_source_image_sha256",
             "c602475a9890bbe725a75ede7c8f667635fc1be69f9db65cae22e752c28fa3cd"}};
}

inline void original_navi_preflight(const Json &definition, int api) {
    const auto need = original_navi_need;

    need(original_navi_requested(definition) && api == 30);
    const auto &original = definition.at("original").at("Navi");
    need(original.at("sha256") == original_navi_sha &&
         original.at("source").at("package") == "com.baidu.naviauto" &&
         original.at("acquisition") ==
                 Json{{"image", "system"}, {"path", std::string(original_navi_target)}});
    unsigned count = 0;

    for (const auto &i : definition.at("install")) {
        const auto artifact = i.value("artifact", std::string{}),
                   pkg = i.value("package", std::string{});

        if (artifact == "original:Navi") {
            need(i.at("type") == "apk" && i.at("target") == original_navi_target &&
                 pkg == "com.baidu.naviauto");
            ++count;
        } else {
            need(artifact != "patched:Navi" && pkg != "com.baidu.naviauto");
        }
    }

    need(count == 1);
    const auto &permissions = definition.at("compatibility").at("privapp_whitelist");
    need(permissions.at("file") == "files/voyemu-privapp.xml" &&
         permissions.at("packages").at("com.baidu.naviauto") ==
                 Json::array({"android.permission.READ_LOGS",
                              "android.permission.MOUNT_UNMOUNT_FILESYSTEMS",
                              "android.permission.INTERACT_ACROSS_USERS",
                              "android.permission.WRITE_MEDIA_STORAGE"}));
    need(definition.at("files").at("launcher-privapp") == Json({{"file", "voyemu-privapp.xml"}}));
    unsigned permission_installs = 0;

    for (const auto &i : definition.at("install")) {
        if (i.value("artifact", std::string{}) == "files:launcher-privapp") {
            need(i == Json({{"artifact", "files:launcher-privapp"},
                            {"type", "file"},
                            {"target", "/system/etc/permissions/voyemu-privapp.xml"},
                            {"mode", "0644"}}));
            ++permission_installs;
        }
    }

    need(permission_installs == 1);
    need(definition.at("components").at("canbus") == "components/canbus");
    unsigned provider_installs = 0;

    for (const auto &i : definition.at("install")) {
        if (i.value("artifact", std::string{}) == "component:canbus") {
            need(i.at("type") == "apk" &&
                 i.at("target") == "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk" &&
                 i.at("package") == "com.qinggan.canbus.service");
            ++provider_installs;
        }
    }

    need(provider_installs == 1);
    need(definition.at("patches").at("original-navi-selinux") ==
         Json({{"type", "file"},
               {"input", "android:/system/etc/selinux/plat_mac_permissions.xml"},
               {"steps", Json::array({"original-navi-selinux.json"})}}));
    unsigned mac_installs = 0;

    for (const auto &i : definition.at("install")) {
        if (i.value("artifact", std::string{}) == "patched:original-navi-selinux") {
            need(i == Json({{"artifact", "patched:original-navi-selinux"},
                            {"type", "file"},
                            {"target", "/system/etc/selinux/plat_mac_permissions.xml"},
                            {"mode", "0644"}}));
            ++mac_installs;
        }
    }

    need(mac_installs == 1);
    const std::array<std::tuple<std::string, Json, std::string>, 4> graph = {
            {{"component:view-observation", Json::array({"original-navi-egl.json"}),
              "/system/framework/framework.jar"},
             {"patched:api30-secondary-navigation", original_navi_services_steps(definition),
              "/system/framework/services.jar"},
             {"original:QGAPI",
              original_navi_qgapi_steps(definition),
              "/system/framework/QGAPI.jar"},
             {"original:QGVehicle", Json::array({"original-navi-vehicle.json"}),
              "/system/framework/QGVehicle.jar"}}};

    for (const auto &[input, steps, target] : graph) {
        std::string key;

        for (auto p = definition.at("patches").begin(); p != definition.at("patches").end(); ++p) {
            if (p.value().value("input", std::string{}) == input &&
                p.value().value("type", std::string{}) == "jar" &&
                p.value().value("steps", Json::array()) == steps) {
                need(key.empty());
                key = p.key();
            }
        }

        need(!key.empty());
        unsigned n = 0;

        for (const auto &i : definition.at("install")) {
            if (i.value("artifact", std::string{}) == "patched:" + key) {
                need(i.at("type") == "file" && i.at("target") == target &&
                     i.value("mode", std::string("0644")) == "0644");
                ++n;
            }
        }

        need(n == 1);
    }
}

inline std::vector<std::string> original_navi_storage_set(const Json &definition, int api) {
    if (!original_navi_requested(definition)) {
        return {};
    }

    original_navi_preflight(definition, api);

    return {"cmd", "appops", "set", "com.baidu.naviauto", "NO_ISOLATED_STORAGE", "allow"};
}

inline bool original_navi_storage_allowed(std::string_view reply) {
    return std::regex_match(std::string(reply),
                            std::regex(R"(NO_ISOLATED_STORAGE: allow(?:;[^\r\n]*)?)"));
}

inline std::string original_navi_process_context(const std::string &output,
                                                 const std::string &pid) {
    if (!std::regex_match(pid, std::regex("[1-9][0-9]{0,9}"))) {
        return {};
    }

    const std::regex row(
            "(?:[ \t]*LABEL[ \t]+PID[ \t]+NAME[ \t]*\r?\n)?[ \t]*(u:r:platform_app:s0(?::[a-zA-Z0-9_,]+)?)[ \t]+" +
            pid + "[ \t]+com\\.baidu\\.naviauto[ \t]*\r?\n?");
    std::smatch match;

    return std::regex_match(output, match, row) ? match[1].str() : std::string{};
}

inline void original_navi_candidate(const Json &definition, int api, const Json &candidate,
                                    const Json &artifacts, const std::string &verified_cert) {
    const auto need = original_navi_need;

    need(original_navi_requested(definition));
    original_navi_preflight(definition, api);
    need(!candidate.contains("prepared_packages") && candidate.contains("camera_package") &&
         candidate.contains("original_navi_system"));
    const Json expected = original_navi_marker();
    need(candidate.at("original_navi_system") == expected && verified_cert == original_navi_cert);
    const auto &original = definition.at("original").at("Navi");
    need(original.at("sha256") == original_navi_sha &&
         original.at("source").at("package") == "com.baidu.naviauto" &&
         original.at("acquisition") ==
                 Json{{"image", "system"}, {"path", std::string(original_navi_target)}});
    const auto &original_artifact = artifacts.at("original:Navi");
    need(original_artifact.at("sha256") == original_navi_sha &&
         original_artifact.at("bytes") == 226402144);
    unsigned original_installs = 0;

    for (const auto &i : definition.at("install")) {
        auto artifact = i.value("artifact", std::string{});
        auto pkg = i.value("package", std::string{});

        if (artifact == "original:Navi") {
            need(i.at("type") == "apk" && i.at("target") == original_navi_target &&
                 pkg == "com.baidu.naviauto");
            ++original_installs;
        } else {
            need(artifact != "patched:Navi" && pkg != "com.baidu.naviauto");
        }
    }

    need(original_installs == 1);
    const auto &patches = definition.at("patches");
    const std::array<std::tuple<std::string, std::string, Json, std::string>, 4> graph = {
            {{"component:view-observation", "jar", Json::array({"original-navi-egl.json"}),
              "/system/framework/framework.jar"},
             {"patched:api30-secondary-navigation", "jar",
              original_navi_services_steps(definition), "/system/framework/services.jar"},
             {"original:QGAPI", "jar",
              original_navi_qgapi_steps(definition),
              "/system/framework/QGAPI.jar"},
             {"original:QGVehicle", "jar", Json::array({"original-navi-vehicle.json"}),
              "/system/framework/QGVehicle.jar"}}};

    for (const auto &[input, type, steps, target] : graph) {
        std::string key;

        for (auto p = patches.begin(); p != patches.end(); ++p) {
            if (p.value().value("input", std::string{}) == input &&
                p.value().value("type", std::string{}) == type &&
                p.value().value("steps", Json::array()) == steps) {
                need(key.empty());
                key = p.key();
            }
        }

        need(!key.empty());
        unsigned installs = 0;

        for (const auto &i : definition.at("install")) {
            if (i.value("artifact", std::string{}) == "patched:" + key) {
                need(i.at("type") == "file" && i.at("target") == target &&
                     i.value("mode", std::string("0644")) == "0644");
                ++installs;
            }
        }

        need(installs == 1);
        const std::string artifact = "patched:" + key;
        const auto pin = artifacts.at(artifact).at("sha256").get<std::string>();
        need(sha256_text(pin));
        unsigned rows = 0;

        for (const auto &row : candidate.at("installed")) {
            if (row.value("artifact", std::string{}) == artifact) {
                need(row.at("target") == target && row.at("sha256") == pin &&
                     row.at("mode") == "0644" && row.at("mutable") == false);
                ++rows;
            }
        }

        need(rows == 1);
    }

    const std::string mac = "/system/etc/selinux/plat_mac_permissions.xml",
                      macpin = "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392";
    need(artifacts.at("patched:original-navi-selinux").at("sha256") == macpin);
    unsigned macrow = 0, maccheck = 0;

    for (const auto &i : candidate.at("installed")) {
        if (i.value("artifact", std::string{}) == "patched:original-navi-selinux") {
            need(i.at("target") == mac && i.at("sha256") == macpin && i.at("mode") == "0644" &&
                 i.at("mutable") == false);
            ++macrow;
        }
    }

    for (const auto &c : candidate.at("checks")) {
        if (c.value("id", std::string{}) == "artifact:patched:original-navi-selinux:" + mac) {
            need(c.at("passed") == true && c.at("expected") == macpin && c.at("actual") == macpin);
            ++maccheck;
        }
    }

    need(macrow == 1 && maccheck == 1);
    unsigned maclabel = 0;

    for (const auto &c : candidate.at("checks")) {
        if (c.value("id", std::string{}) == "navi-mac-label") {
            need(c.at("passed") == true && c.at("actual") == "u:object_r:mac_perms_file:s0" &&
                 c.at("expected") == "u:object_r:mac_perms_file:s0");
            ++maclabel;
        }
    }

    need(maclabel == 1);
    const auto &canbus = artifacts.at("component:canbus");
    const auto canbus_pin = canbus.at("sha256").get<std::string>();
    need(sha256_text(canbus_pin));
    need(definition.at("components").at("canbus") == "components/canbus");
    unsigned provider_install = 0;

    for (const auto &i : definition.at("install")) {
        if (i.value("artifact", std::string{}) == "component:canbus") {
            need(i.at("type") == "apk" &&
                 i.at("target") == "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk" &&
                 i.at("package") == "com.qinggan.canbus.service");
            ++provider_install;
        }
    }

    need(provider_install == 1);
    unsigned provider_row = 0;

    for (const auto &i : candidate.at("installed")) {
        if (i.value("artifact", std::string{}) == "component:canbus") {
            need(i.at("sha256") == canbus_pin &&
                 i.at("target") == "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk" &&
                 i.at("package") == "com.qinggan.canbus.service" && i.at("mode") == "0644" &&
                 i.at("mutable") == false);
            ++provider_row;
        }
    }

    need(provider_row == 1);
    unsigned authority = 0, provider_privilege = 0;

    for (const auto &c : candidate.at("checks")) {
        const auto id = c.value("id", std::string{});

        if (id == "provider:com.voyemu.free731.identity") {
            need(c.at("passed") == true && c.at("package") == "com.qinggan.canbus.service" &&
                 c.at("class") == "com.qinggan.canbus.service.VinIdentityProvider" &&
                 c.at("artifact_sha256") == canbus_pin);
            ++authority;
        }

        if (id == "privileged-apk:com.qinggan.canbus.service") {
            need(c.at("passed") == true && c.at("privileged") == true &&
                 c.at("updated_system_app") == false &&
                 c.at("effective_apk") == "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk");
            ++provider_privilege;
        }
    }

    need(authority == 1 && provider_privilege == 1);
    unsigned storage = 0;

    for (const auto &c : candidate.at("checks")) {
        if (c.value("id", std::string{}) == "original-navi-storage-appop") {
            need(c.at("passed") == true && c.at("expected") == "allow" &&
                 original_navi_storage_allowed(c.at("actual").get<std::string>()));
            ++storage;
        }
    }

    need(storage == 1);
    unsigned security = 0;

    for (const auto &c : candidate.at("checks")) {
        if (c.value("id", std::string{}) == "original-navi-security") {
            need(c.at("passed") == true && c.at("package") == "com.baidu.naviauto" &&
                 c.at("policy_seinfo") == "platform" &&
                 c.at("policy_seinfo_source") == "pinned-mac-policy" &&
                 std::regex_match(c.at("process_context").get<std::string>(),
                                  std::regex("u:r:platform_app:s0(?::[a-zA-Z0-9_,]+)?")) &&
                 std::regex_match(c.at("pid").get<std::string>(), std::regex("[1-9][0-9]{0,9}")));
            ++security;
        }
    }

    need(security == 1);
    unsigned installed = 0;

    for (const auto &item : candidate.at("installed")) {
        const auto artifact = item.value("artifact", std::string{}),
                   pkg = item.value("package", std::string{});

        if (artifact == "original:Navi") {
            need(pkg == "com.baidu.naviauto" && item.at("target") == original_navi_target &&
                 item.at("sha256") == original_navi_sha && item.at("mode") == "0644" &&
                 item.at("mutable") == false);
            ++installed;
        } else {
            need(artifact != "patched:Navi" && pkg != "com.baidu.naviauto");
        }
    }

    need(installed == 1);
    unsigned privilege = 0, hash = 0;

    for (const auto &c : candidate.at("checks")) {
        const auto id = c.value("id", std::string{});

        if (id == "privileged-apk:com.baidu.naviauto") {
            need(c.at("passed") == true && c.at("privileged") == true &&
                 c.at("updated_system_app") == false &&
                 c.at("system_base") == original_navi_target &&
                 c.at("effective_apk") == original_navi_target);
            ++privilege;
        }

        if (id == "artifact:original:Navi:" + std::string(original_navi_target)) {
            need(c.at("passed") == true && c.at("expected") == original_navi_sha &&
                 c.at("actual") == original_navi_sha);
            ++hash;
        }
    }

    need(privilege == 1 && hash == 1);
    const std::string target(original_navi_target), parent = "/system/priv-app/dueros";

    for (const auto &[id, value] : std::array<std::pair<std::string, std::string>, 7>{
                 {{"privileged-base:" + target, std::string(original_navi_sha)},
                  {"privileged-owner:" + target, "0:0"},
                  {"privileged-parent-mode:" + parent, "755"},
                  {"privileged-parent-owner:" + parent, "0:0"},
                  {"mode:" + target, "644"},
                  {"privileged-label:" + parent, "u:object_r:system_file:s0"},
                  {"privileged-label:" + target, "u:object_r:system_file:s0"}}}) {
        unsigned count = 0;

        for (const auto &c : candidate.at("checks")) {
            if (c.value("id", std::string{}) == id) {
                need(c.at("passed") == true && c.at("expected") == value &&
                     c.at("actual") == value);
                ++count;
            }
        }

        need(count == 1);
    }
}
} // namespace voyemu
