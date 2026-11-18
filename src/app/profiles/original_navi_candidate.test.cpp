#include "original_navi_candidate.hpp"
#include "api30_framework_boot.hpp"
#include "process.hpp"
#include "profile_catalog.hpp"
#include "qinggan11_startup.hpp"
#include <iostream>
using namespace voyemu;

static void check(bool b) {
    if (!b) {
        throw std::runtime_error("test failed");
    }
}

int main() {
    try {
        const std::string pin(original_navi_sha), cert(original_navi_cert),
                target(original_navi_target), profile(original_navi_profile), dummy(64, 'a');
        Json definition = {{"original",
                            {{"Navi",
                              {{"sha256", pin},
                               {"source", {{"package", "com.baidu.naviauto"}}},
                               {"acquisition", {{"image", "system"}, {"path", target}}}}}}},
                           {"install", Json::array({{{"artifact", "original:Navi"},
                                                     {"type", "apk"},
                                                     {"target", target},
                                                     {"package", "com.baidu.naviauto"}}})},
                           {"patches", Json::object()}};
        Json installed = Json::array({{{"artifact", "original:Navi"},
                                       {"package", "com.baidu.naviauto"},
                                       {"target", target},
                                       {"sha256", pin},
                                       {"mode", "0644"},
                                       {"mutable", false}}});
        Json artifacts = {{"original:Navi", {{"sha256", pin}, {"bytes", 226402144}}}};
        const std::array<std::tuple<std::string, std::string, Json, std::string>, 4> graph = {
                {{"component:view-observation", "egl", Json::array({"original-navi-egl.json"}),
                  "/system/framework/framework.jar"},
                 {"patched:api30-secondary-navigation", "libs",
                  Json::array({"original-navi-libraries.json"}), "/system/framework/services.jar"},
                 {"original:QGAPI", "api",
                  Json::array({"api30-audio-consumer.json", "original-navi-vin.json"}),
                  "/system/framework/QGAPI.jar"},
                 {"original:QGVehicle", "vehicle", Json::array({"original-navi-vehicle.json"}),
                  "/system/framework/QGVehicle.jar"}}};

        for (const auto &[input, name, steps, path] : graph) {
            definition["patches"][name] = {{"input", input}, {"type", "jar"}, {"steps", steps}};
            definition["install"].push_back({{"artifact", "patched:" + name},
                                             {"type", "file"},
                                             {"target", path},
                                             {"mode", "0644"}});
            artifacts["patched:" + name] = {{"sha256", dummy}};
            installed.push_back({{"artifact", "patched:" + name},
                                 {"target", path},
                                 {"sha256", dummy},
                                 {"mode", "0644"},
                                 {"mutable", false}});
        }

        Json candidate = {{"camera_package", Json::object()},
                          {"original_navi_system",
                           {{"schema", 1},
                            {"artifact", "original:Navi"},
                            {"package", "com.baidu.naviauto"},
                            {"target", target},
                            {"sha256", pin},
                            {"certificate_sha256", cert},
                            {"historical_source_image_sha256",
                             "c602475a9890bbe725a75ede7c8f667635fc1be69f9db65cae22e752c28fa3cd"}}},
                          {"installed", installed},
                          {"checks", Json::array({{{"id", "privileged-apk:com.baidu.naviauto"},
                                                   {"passed", true},
                                                   {"privileged", true},
                                                   {"updated_system_app", false},
                                                   {"system_base", target},
                                                   {"effective_apk", target}},
                                                  {{"id", "artifact:original:Navi:" + target},
                                                   {"passed", true},
                                                   {"expected", pin},
                                                   {"actual", pin}}})}};
        const std::string
                mac = "/system/etc/selinux/plat_mac_permissions.xml",
                macpin = "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392";
        definition["patches"]["original-navi-mac"] = {
                {"type", "file"},
                {"input", "android:" + mac},
                {"steps", Json::array({"original-navi-mac.json"})}};
        definition["install"].push_back({{"artifact", "patched:original-navi-mac"},
                                         {"type", "file"},
                                         {"target", mac},
                                         {"mode", "0644"}});
        artifacts["patched:original-navi-mac"] = {{"sha256", macpin}};
        candidate["installed"].push_back({{"artifact", "patched:original-navi-mac"},
                                          {"target", mac},
                                          {"sha256", macpin},
                                          {"mode", "0644"},
                                          {"mutable", false}});
        candidate["checks"].push_back({{"id", "artifact:patched:original-navi-mac:" + mac},
                                       {"passed", true},
                                       {"expected", macpin},
                                       {"actual", macpin}});
        candidate["checks"].push_back({{"id", "navi-mac-label"},
                                       {"passed", true},
                                       {"expected", "u:object_r:mac_perms_file:s0"},
                                       {"actual", "u:object_r:mac_perms_file:s0"}});
        candidate["checks"].push_back({{"id", "original-navi-security"},
                                       {"passed", true},
                                       {"package", "com.baidu.naviauto"},
                                       {"policy_seinfo", "platform"},
                                       {"policy_seinfo_source", "pinned-mac-policy"},
                                       {"process_context", "u:r:platform_app:s0:c123,c456"},
                                       {"pid", "1234"}});
        definition["components"]["canbus"] = "components/canbus";
        definition["install"].push_back(
                {{"artifact", "component:canbus"},
                 {"type", "apk"},
                 {"target", "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk"},
                 {"package", "com.qinggan.canbus.service"}});
        artifacts["component:canbus"] = {{"sha256", dummy}};
        candidate["installed"].push_back(
                {{"artifact", "component:canbus"},
                 {"target", "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk"},
                 {"package", "com.qinggan.canbus.service"},
                 {"sha256", dummy},
                 {"mode", "0644"},
                 {"mutable", false}});
        candidate["checks"].push_back({{"id", "provider:com.voyemu.free731.identity"},
                                       {"passed", true},
                                       {"package", "com.qinggan.canbus.service"},
                                       {"class", "com.qinggan.canbus.service.VinIdentityProvider"},
                                       {"artifact_sha256", dummy}});
        candidate["checks"].push_back(
                {{"id", "privileged-apk:com.qinggan.canbus.service"},
                 {"passed", true},
                 {"privileged", true},
                 {"updated_system_app", false},
                 {"effective_apk", "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk"}});

        for (const auto &[id, value] : std::array<std::pair<std::string, std::string>, 7>{
                     {{"privileged-base:" + target, pin},
                      {"privileged-owner:" + target, "0:0"},
                      {"privileged-parent-mode:/system/priv-app/dueros", "755"},
                      {"privileged-parent-owner:/system/priv-app/dueros", "0:0"},
                      {"mode:" + target, "644"},
                      {"privileged-label:/system/priv-app/dueros", "u:object_r:system_file:s0"},
                      {"privileged-label:" + target, "u:object_r:system_file:s0"}}}) {
            candidate["checks"].push_back(
                    {{"id", id}, {"expected", value}, {"actual", value}, {"passed", true}});
        }

        definition["compatibility"]["privapp_whitelist"] = {
                {"file", "files/voyemu-privapp.xml"},
                {"packages",
                 {{"com.baidu.naviauto",
                   Json::array({"android.permission.READ_LOGS",
                                "android.permission.MOUNT_UNMOUNT_FILESYSTEMS",
                                "android.permission.INTERACT_ACROSS_USERS",
                                "android.permission.WRITE_MEDIA_STORAGE"})}}}};
        definition["files"]["launcher-privapp"] = {{"file", "voyemu-privapp.xml"}};
        definition["install"].push_back({{"artifact", "files:launcher-privapp"},
                                         {"type", "file"},
                                         {"target", "/system/etc/permissions/voyemu-privapp.xml"},
                                         {"mode", "0644"}});

        for (int mode = 0; mode < 4; ++mode) {
            auto d = definition;
            auto &installs = d["install"];
            auto it = std::find_if(installs.begin(), installs.end(), [](const Json &row) {
                return row.value("artifact", std::string{}) == "component:canbus";
            });

            if (mode == 0) {
                installs.erase(it);
            }

            if (mode == 1) {
                (*it)["target"] = "/data/app/canbus.apk";
            }

            if (mode == 2) {
                (*it)["package"] = "foreign";
            }

            if (mode == 3) {
                installs.push_back(*it);
            }

            bool rejected = false;
            try {
                original_navi_preflight(d, profile, 30);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected);
        }

        for (int mode = 0; mode < 2; ++mode) {
            auto d = definition;

            if (mode == 0) {
                d["compatibility"]["privapp_whitelist"]["packages"].erase("com.baidu.naviauto");
            }

            if (mode == 1) {
                d["compatibility"]["privapp_whitelist"]["packages"]["com.baidu.naviauto"].push_back(
                        "android.permission.WRITE_SECURE_SETTINGS");
            }

            bool rejected = false;
            try {
                original_navi_preflight(d, profile, 30);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected);
        }

        auto boot = definition;
        boot["id"] = profile;
        boot["android"]["package"] = "system-images;android-30;default;arm64-v8a";
        boot["components"]["view-observation"] = "components/view-observation";
        api30_framework_boot::verify(boot);
        check(original_navi_storage_set(definition, profile, 30) ==
              std::vector<std::string>({"cmd", "appops", "set", "com.baidu.naviauto",
                                        "NO_ISOLATED_STORAGE", "allow"}));
        check(original_navi_storage_allowed("NO_ISOLATED_STORAGE: allow; time=+1s"));

        for (const auto &reply :
             {"NO_ISOLATED_STORAGE: deny", "NO_ISOLATED_STORAGE: default", "No operations.",
              "NO_ISOLATED_STORAGE: allowed", "NO_ISOLATED_STORAGE: allow\nother: deny"}) {
            check(!original_navi_storage_allowed(reply));
        }

        check(original_navi_storage_set(
                      Json{{"install", Json::array()}, {"patches", Json::object()}}, "other", 30)
                      .empty());

        for (int mode = 0; mode < 4; ++mode) {
            auto d = definition;
            auto name = profile;
            int api = 30;

            if (mode == 0) {
                name = "other";
            }

            if (mode == 1) {
                api = 29;
            }

            if (mode == 2) {
                d["original"]["Navi"]["sha256"] = dummy;
            }

            if (mode == 3) {
                d["install"].push_back(
                        {{"artifact", "patched:Navi"}, {"package", "com.baidu.naviauto"}});
            }

            bool rejected = false;
            try {
                original_navi_storage_set(d, name, api);
            } catch (...) {
                rejected = true;
            }

            check(rejected);
        }

        const Json storage_check = {{"id", "original-navi-storage-appop"},
                                    {"expected", "allow"},
                                    {"actual", "NO_ISOLATED_STORAGE: allow; time=+1s"},
                                    {"passed", true}};
        candidate["checks"].push_back(storage_check);

        for (int mode = 0; mode < 6; ++mode) {
            auto c = candidate;

            if (mode == 0) {
                c["checks"].erase(c["checks"].size() - 1);
            }

            if (mode == 1) {
                c["checks"].push_back(storage_check);
            }

            if (mode == 2) {
                c["checks"].back()["actual"] = "NO_ISOLATED_STORAGE: deny";
            }

            if (mode == 3) {
                c["checks"].back()["actual"] = "NO_ISOLATED_STORAGE: allowed";
            }

            if (mode == 4) {
                c["checks"].back()["expected"] = "deny";
            }

            if (mode == 5) {
                c["checks"].back()["passed"] = false;
            }

            bool rejected = false;
            try {
                original_navi_candidate(definition, profile, 30, c, artifacts, cert);
            } catch (...) {
                rejected = true;
            }

            check(rejected);
        }

        original_navi_candidate(definition, profile, 30, candidate, artifacts, cert);

        for (int mode = 0; mode < 29; ++mode) {
            auto d = definition, c = candidate, a = artifacts;
            auto id = profile, sign = cert;
            int api = 30;

            if (mode == 0) {
                id = "other";
            }

            if (mode == 1) {
                api = 29;
            }

            if (mode == 2) {
                c.erase("original_navi_system");
            }

            if (mode == 3) {
                c["prepared_packages"] = Json::array();
            }

            if (mode == 4) {
                c["original_navi_system"]["sha256"] = dummy;
            }

            if (mode == 5) {
                sign = dummy;
            }

            if (mode == 6) {
                c["checks"][0]["updated_system_app"] = true;
            }

            if (mode == 7) {
                c["checks"][0]["effective_apk"] = "/data/app/shadow/base.apk";
            }

            if (mode == 8) {
                c["checks"][1]["passed"] = false;
            }

            if (mode == 9) {
                c["installed"].push_back(
                        {{"artifact", "patched:Navi"}, {"package", "com.baidu.naviauto"}});
            }

            if (mode == 10) {
                d["original"]["Navi"]["acquisition"]["path"] = "/system/app/other.apk";
            }

            if (mode == 11) {
                a["original:Navi"]["sha256"] = dummy;
            }

            if (mode == 12) {
                d["patches"]["api"]["steps"] = Json::array({"original-navi-vin.json"});
            }

            if (mode == 13) {
                c["installed"][1]["sha256"] = std::string(64, 'b');
            }

            if (mode == 14) {
                c.erase("camera_package");
            }

            if (mode == 15) {
                c["checks"].push_back(c["checks"][0]);
            }

            if (mode == 16) {
                c["checks"][8]["actual"] = "1000:1000";
            }

            if (mode == 17) {
                c["checks"].erase(c["checks"].size() - 1);
            }

            if (mode == 18) {
                c["original_navi_system"]["historical_source_image_sha256"] = dummy;
            }

            if (mode == 19) {
                a["component:canbus"]["sha256"] = pin;
            }

            if (mode == 20) {
                c["checks"][5]["class"] = "wrong";
            }

            if (mode == 21) {
                d["components"]["canbus"] = "foreign";
            }

            if (mode == 22) {
                a["patched:original-navi-mac"]["sha256"] = dummy;
            }

            if (mode == 23) {
                d["patches"].erase("original-navi-mac");
            }

            if (mode == 24) {
                c["checks"][4]["process_context"] = "u:r:priv_app:s0";
            }

            if (mode == 25) {
                c["checks"][4]["policy_seinfo"] = "default";
            }

            if (mode == 26) {
                c["checks"][4]["policy_seinfo_source"] = "dumpsys-assumed";
            }

            if (mode == 27) {
                c["checks"][4].erase("process_context");
            }

            if (mode == 28) {
                c["checks"][4]["pid"] = "0";
            }

            bool rejected = false;
            try {
                original_navi_candidate(d, id, api, c, a, sign);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected);
        }

        const auto repository = std::filesystem::path(__FILE__)
                                        .parent_path()
                                        .parent_path()
                                        .parent_path()
                                        .parent_path();
        const auto actual = read_private_json(repository / "profiles" / profile / "profile.json",
                                              1 << 20);
        check(qinggan11_wheel_capability(actual) == qinggan11_partial_wheel_capability());
        check(declared_patch_plan(actual).at("patches").size() == actual.at("patches").size());

        for (unsigned mode = 0; mode < 8; ++mode) {
            auto bad = actual;

            if (mode == 0) {
                bad["install"].push_back({{"artifact", "component:view-observation"},
                                          {"type", "file"},
                                          {"target", "/system/framework/framework.jar"},
                                          {"mode", "0644"}});
            }

            if (mode == 1) {
                bad["patches"]["original-navi-framework"]["steps"] =
                        Json::array({"unreviewed.json"});
            }

            if (mode == 2) {
                bad["patches"]["extra-jar"] = {{"type", "jar"},
                                               {"input", "original:QGVehicle"},
                                               {"steps", Json::array({"unreviewed.json"})}};
            }

            if (mode == 3) {
                bad["patches"]["QGAPI"]["steps"] = Json::array({"api30-audio-consumer.json"});
            }

            if (mode == 4) {
                bad["components"]["view-observation"] = "foreign";
            }

            if (mode == 5) {
                bad["id"] = "foreign";
            }

            if (mode == 6) {
                bad["patches"]["original-navi-vehicle"]["input"] = "original:QGAPI";
            }

            if (mode == 7) {
                bad["install"].push_back({{"artifact", "component:view-observation"},
                                          {"type", "file"},
                                          {"target", "/system/framework/unreviewed.jar"},
                                          {"mode", "0644"}});
            }

            bool denied = false;
            try {
                (void)qinggan11_wheel_capability(bad);
                (void)declared_patch_plan(bad);
            } catch (...) {
                denied = true;
            }

            check(denied);
        }

        std::cout
                << "PASS original Navi exact candidate, 29 negative contracts, 4 provider and 3 permission preflight negatives\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
