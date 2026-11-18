#pragma once
#include "error.hpp"
#include "model.hpp"
#include "original_navi_candidate.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace voyemu::api30_framework_boot {
inline constexpr std::array<const char *, 6> core = {"core-oj", "core-libart",  "core-icu4j",
                                                     "okhttp",  "bouncycastle", "apache-xml"};
inline constexpr std::array<const char *, 6> extension = {
        "framework",   "ext",        "telephony-common",
        "voip-common", "ims-common", "framework-atb-backward-compatibility"};

inline void verify(const Json &profile) {
    Json install = {{"artifact", "component:view-observation"},
                    {"type", "file"},
                    {"target", "/system/framework/framework.jar"},
                    {"mode", "0644"}};

    if (original_navi_requested(profile)) {
        original_navi_preflight(profile, profile.value("id", std::string{}), 30);

        for (auto p = profile.at("patches").begin(); p != profile.at("patches").end(); ++p) {
            if (p.value().value("input", std::string{}) == "component:view-observation" &&
                p.value().value("type", std::string{}) == "jar" &&
                p.value().value("steps", Json::array()) ==
                        Json::array({"original-navi-egl.json"})) {
                install["artifact"] = "patched:" + p.key();
            }
        }
    }

    const auto entries = profile.value("install", Json::array());

    if ((profile.value("id", std::string{}) != "free-11-chinese-20260731-7.3.1" &&
         profile.value("id", std::string{}) != "passion-11-russian-20260403-2.1.4.80") ||
        profile.at("android").value("package", std::string{}) !=
                "system-images;android-30;default;arm64-v8a" ||
        profile.value("components", Json::object()).value("view-observation", std::string{}) !=
                "components/view-observation" ||
        std::count(entries.begin(), entries.end(), install) != 1) {
        throw VoyemuError("ANDROID_CANDIDATE",
                          "exact Free A11 7.3.1/Passion API30 framework boot extension required");
    }
}

inline std::string classpath() {
    std::string result;

    for (const auto *name : core) {
        if (!result.empty()) {
            result += ':';
        }

        result += "/apex/com.android.art/javalib/" + std::string(name) + ".jar";
    }

    for (const auto *name : extension) {
        result += ":/system/framework/" + std::string(name) + ".jar";
    }

    return result;
}

inline std::vector<std::string> paths(const Json &profile) {
    verify(profile);
    std::vector<std::string> result;

    for (const auto *name : extension) {
        for (const auto *suffix : {".art", ".oat", ".vdex"}) {
            result.push_back("/system/framework/arm64/boot-" + std::string(name) + suffix);
        }
    }

    return result;
}

inline std::vector<std::string> command(const Json &profile) {
    verify(profile);
    std::vector<std::string> result = {"/apex/com.android.art/bin/dex2oat64",
                                       "--image=/system/framework/arm64/boot.art",
                                       "--oat-file=/system/framework/arm64/boot.oat",
                                       "--oat-location=/system/framework/arm64/boot.oat",
                                       "--boot-image=/apex/com.android.art/javalib/boot.art",
                                       "--multi-image",
                                       "--instruction-set=arm64",
                                       "--instruction-set-variant=generic",
                                       "--instruction-set-features=default",
                                       "--compiler-filter=speed",
                                       "--image-format=lz4hc",
                                       "--android-root=/system",
                                       "--runtime-arg",
                                       "-Xms64m",
                                       "--runtime-arg",
                                       "-Xmx512m",
                                       "--runtime-arg",
                                       "-Xbootclasspath:" + classpath(),
                                       "--runtime-arg",
                                       "-Xbootclasspath-locations:" + classpath(),
                                       "--no-inline-from=core-oj.jar",
                                       "--force-determinism",
                                       "--abort-on-hard-verifier-error"};

    for (const auto *name : extension) {
        const auto jar = "/system/framework/" + std::string(name) + ".jar";
        result.push_back("--dex-file=" + jar);
        result.push_back("--dex-location=" + jar);
    }

    return result;
}
} // namespace voyemu::api30_framework_boot
