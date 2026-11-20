#include "profile_catalog.hpp"
#include "error.hpp"
#include "file_patch.hpp"
#include "qinggan11_startup.hpp"
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
namespace voyemu {
#include "profile_catalog.inc"

const Json &embedded_profile_catalog() {
    static const Json catalog = [] {
        auto parsed = Json::parse(std::begin(embedded_catalog_json),
                                  std::end(embedded_catalog_json));

        for (const auto &record : parsed) {
            const auto &definition = record.at("definition");
            (void)profile_platform(definition);
            (void)profile_board(definition);
            (void)profile_title(definition);
        }

        return parsed;
    }();

    return catalog;
}

const Json &embedded_profile(std::string_view id) {
    for (const auto &record : embedded_profile_catalog()) {
        if (record.at("id") == id) {
            return record;
        }
    }

    throw VoyemuError("PROFILE_NOT_FOUND", "profile is not bundled");
}

std::vector<std::string> embedded_profile_names() {
    std::vector<std::string> result;

    for (const auto &record : embedded_profile_catalog()) {
        result.push_back(record.at("id").get<std::string>());
    }

    return result;
}

std::vector<std::string> embedded_profile_names(ProfilePlatform platform, ProfileBoard board) {
    std::vector<std::string> result;

    for (const auto &record : embedded_profile_catalog()) {
        const auto &definition = record.at("definition");

        if (profile_platform(definition) == platform && profile_board(definition) == board) {
            result.push_back(record.at("id").get<std::string>());
        }
    }

    return result;
}

bool embedded_profile_declares(std::string_view id, ProfilePlatform platform, ProfileBoard board) {
    for (const auto &record : embedded_profile_catalog()) {
        if (record.at("id") == id) {
            const auto &definition = record.at("definition");

            return profile_platform(definition) == platform && profile_board(definition) == board;
        }
    }

    return false;
}

bool record_declares(const Json &record, std::string_view reference, ProfilePlatform platform,
                     ProfileBoard board) {
    if (record.is_object() && record.contains("platform")) {
        return profile_platform(record) == platform && profile_board(record) == board;
    }

    return embedded_profile_declares(reference, platform, board);
}

Json declared_patch_plan(const Json &definition) {
    const auto patches = definition.value("patches", Json::object());

    if (!patches.is_object()) {
        throw VoyemuError("PREPARATION_SCHEMA", "patches must be an object");
    }

    const auto android = definition.value("android", Json::object());
    const bool native_api28 =
            android.is_object() &&
            android.value("package", std::string{}).starts_with("system-images;android-28;");
    const bool original_navi = original_navi_requested(definition);

    if (original_navi) {
        original_navi_preflight(definition, definition.value("id", std::string{}), 30);
    }

    Json ordered = Json::array();
    std::map<std::string, unsigned> state;
    std::function<void(const std::string &)> visit;
    visit = [&](const std::string &id) {
        if (!patches.contains(id)) {
            throw VoyemuError("PREPARATION_REFERENCE", "unknown patch " + id);
        }

        if (state[id] == 2) {
            return;
        }

        if (state[id] == 1) {
            throw VoyemuError("PREPARATION_CYCLE", "patch dependency cycle at " + id);
        }

        state[id] = 1;
        const auto &patch = patches.at(id);
        const auto type = patch.at("type").get<std::string>();
        const auto profile_id = definition.value("id", std::string{});
        const bool secondary_binding = (profile_id == "free-11-chinese-20260731-7.3.1" &&
                                        id == "api30-secondary-navigation") ||
                                       (profile_id == "passion-11-russian-20260403-2.1.4.80" &&
                                        id == "passion-api30-secondary-navigation");
        const bool secondary_jar =
                secondary_binding &&
                android.value("package", std::string{}) ==
                        "system-images;android-30;default;arm64-v8a" &&
                (patch == Json{{"type", "jar"},
                               {"input", "android:/system/framework/services.jar"},
                               {"steps", Json::array({id + ".json"})}} ||
                 (profile_id == "passion-11-russian-20260403-2.1.4.80" &&
                  patch == Json{{"type", "jar"},
                                {"input", "android:/system/framework/services.jar"},
                                {"steps", Json::array({"passion-api30-secondary-navigation.json",
                                                       "passion-api30-display-power.json"})}}) ||
                 (profile_id == "passion-11-russian-20260403-2.1.4.80" &&
                  patch == Json{{"type", "jar"},
                                {"input", "android:/system/framework/services.jar"},
                                {"steps", Json::array({"passion-api30-secondary-navigation.json",
                                                       "passion-api30-display-power.json",
                                                       "passion-api30-startup-volume.json"})}}));
        const bool audio_consumer_jar =
                profile_id == "free-11-chinese-20260731-7.3.1" && id == "QGAPI" &&
                android.value("package", std::string{}) ==
                        "system-images;android-30;default;arm64-v8a" &&
                qinggan11_731_qgapi_original(definition.value("original", Json::object())
                                                     .value("QGAPI", Json::object())) &&
                patch == Json{{"type", "jar"},
                              {"input", "original:QGAPI"},
                              {"steps", Json::array({"api30-audio-consumer.json"})}};
        const bool original_jar =
                original_navi &&
                android.value("package", std::string{}) ==
                        "system-images;android-30;default;arm64-v8a" &&
                (patch == Json{{"type", "jar"},
                               {"input", "component:view-observation"},
                               {"steps", Json::array({"original-navi-egl.json"})}} ||
                 patch == Json{{"type", "jar"},
                               {"input", "patched:api30-secondary-navigation"},
                               {"steps", Json::array({"original-navi-libraries.json"})}} ||
                 (qinggan11_731_qgapi_original(definition.value("original", Json::object())
                                                       .value("QGAPI", Json::object())) &&
                  patch == Json{{"type", "jar"},
                                {"input", "original:QGAPI"},
                                {"steps", Json::array({"api30-audio-consumer.json",
                                                       "original-navi-vin.json"})}}) ||
                 patch == Json{{"type", "jar"},
                               {"input", "original:QGVehicle"},
                               {"steps", Json::array({"original-navi-vehicle.json"})}});

        if (type != "apk" && type != "dex" && type != "file" && !secondary_jar &&
            !audio_consumer_jar && !original_jar) {
            throw VoyemuError("PREPARATION_SCHEMA", "unsupported patch type");
        }

        Json dependencies = Json::array({patch.at("input")});

        if (patch.contains("uses")) {
            if (!patch.at("uses").is_array()) {
                throw VoyemuError("PREPARATION_SCHEMA", "uses must be an array");
            }

            for (const auto &reference : patch.at("uses")) {
                dependencies.push_back(reference);
            }
        }

        for (const auto &reference : dependencies) {
            const auto ref = reference.get<std::string>();
            const auto separator = ref.find(':');

            if (separator == std::string::npos || separator + 1 == ref.size()) {
                throw VoyemuError("PREPARATION_REFERENCE", "malformed reference");
            }

            const auto kind = ref.substr(0, separator), name = ref.substr(separator + 1);

            if (kind == "patched") {
                visit(name);
            } else if (kind == "original" || kind == "component") {
                const auto group = kind == "original" ? "original" : "components";

                if (!definition.contains(group) || !definition.at(group).contains(name)) {
                    throw VoyemuError("PREPARATION_REFERENCE", "unknown " + ref);
                }
            } else if (kind != "android") {
                throw VoyemuError("PREPARATION_REFERENCE", "unsupported " + kind);
            }
        }

        state[id] = 2;
        Json node = {{"id", id}, {"type", type}, {"dependencies", dependencies}};
        const auto native_steps = native_file_patch_steps(id);

        if (type == "file" && !native_steps.empty() && patch.contains("steps") &&
            patch.at("steps") == Json(native_steps)) {
            if (native_api28) {
                node["native_recipe"] = {
                        {"id", id}, {"api", 28}, {"scope", "pinned-file-draft-handler"}};
            } else {
                node["native_recipe_unavailable"] = "android_api_not_supported";
            }
        }

        ordered.push_back(std::move(node));
    };

    for (auto it = patches.begin(); it != patches.end(); ++it) {
        visit(it.key());
    }

    return {{"schema", 1},
            {"scope", "declared-patch-dependencies"},
            {"executable", false},
            {"patches", ordered}};
}
} // namespace voyemu
