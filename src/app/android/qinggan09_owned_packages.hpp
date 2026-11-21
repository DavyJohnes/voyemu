#pragma once
#include "android_owned_package.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "toolchain_manifest.hpp"
#include <array>
namespace voyemu {
inline constexpr std::string_view qinggan09_owned_packages_certificate_sha256 =
        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8";
inline constexpr std::array<std::string_view, 2> qinggan09_owned_package_names = {
        "com.qinggan.audiopolicy.service", "com.voyemu"};
// CanBus is a persistent system app like the OEM CanBusService, so qg.canbus precedes HOME.
inline constexpr std::string_view qinggan09_canbus_system_apk =
        "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk";

inline void qinggan09_owned_packages_need(bool yes) {
    if (!yes) {
        throw VoyemuError("QINGGAN09_OWNED_PACKAGES",
                          "exact verified two-component A9 descriptor required");
    }
}

inline bool qinggan09_owned_packages_hash(const Json &value) {
    if (!value.is_string()) {
        return false;
    }

    const auto text = value.get<std::string>();

    return sha256_text(text);
}

inline bool qinggan09_owned_packages_integer(const Json &object, const char *key, int expected) {
    return object.contains(key) && object.at(key).is_number_integer() && object.at(key) == expected;
}

// The descriptor names the profile whose candidate built the packages; the machine that carries
// it checks the platform and board of that profile.
inline void qinggan09_owned_packages_descriptor(const Json &value, bool portable) {
    qinggan09_owned_packages_need(
            value.is_object() && qinggan09_owned_packages_integer(value, "schema", 1) &&
            value.size() == 6 + (value.contains("canbus") ? 1U : 0U) +
                                    (value.contains("navigation_bus") ? 1U : 0U) &&
            (value.contains("canbus") || !value.contains("navigation_bus")) &&
            valid_profile_name(value.value("profile", std::string{})) &&
            qinggan09_owned_packages_integer(value, "api", 28));
    qinggan09_owned_packages_need(value.value("certificate_sha256", std::string{}) ==
                                          qinggan09_owned_packages_certificate_sha256 &&
                                  value.contains("receipt_sha256") &&
                                  qinggan09_owned_packages_hash(value.at("receipt_sha256")) &&
                                  value.contains("packages") && value.at("packages").is_array() &&
                                  value.at("packages").size() == 2);

    for (std::size_t i = 0; i < 2; ++i) {
        android_package_detail::descriptor(value.at("packages").at(i), portable,
                                           std::string(qinggan09_owned_package_names[i]),
                                           16ULL << 20);
    }

    qinggan09_owned_packages_need(value.at("packages").at(0).at("sha256") !=
                                  value.at("packages").at(1).at("sha256"));

    for (const auto *role : {"canbus", "navigation_bus"}) {
        if (!value.contains(role)) {
            continue;
        }

        const auto &canbus = value.at(role);
        qinggan09_owned_packages_need(
                canbus.is_object() && canbus.size() == 3 &&
                canbus.contains("expected_previous_sha256") &&
                qinggan09_owned_packages_hash(canbus.at("expected_previous_sha256")) &&
                canbus.contains("predecessor_receipt_sha256") &&
                qinggan09_owned_packages_hash(canbus.at("predecessor_receipt_sha256")));
        android_package_detail::descriptor(canbus.at("package"), portable,
                                           std::string_view(role) == "canbus"
                                                   ? "com.qinggan.canbus.service"
                                                   : "com.qinggan.QGBus",
                                           16ULL << 20);
    }
}

// Descriptor of FULL build-artifacts origin; signature acceptance comes from actual guest
// PackageManager installation.
template <std::size_t Count>
inline Json
qinggan09_owned_packages_artifacts_candidate(const std::string &bytes, const Json &installed,
                                             const std::array<std::string, Count> &certificates,
                                             std::string_view logical_profile) {
    static_assert(Count == 2 || Count == 3 || Count == 4);
    qinggan09_owned_packages_need(bytes.size() <= 16ULL << 20 && installed.is_array());
    const auto result = Json::parse(bytes);
    qinggan09_owned_packages_need(
            qinggan09_owned_packages_integer(result, "schema", 1) &&
            result.value("operation", std::string{}) == "build-artifacts" &&
            result.value("profile", std::string{}) == logical_profile &&
            result.at("artifacts_built") == true && result.at("guest_started") == false &&
            result.at("machine_ready") == false && result.at("artifacts").is_object());
    Json packages = Json::array();

    for (std::size_t i = 0; i < Count; ++i) {
        const std::string reference = i == 3   ? "component:navigation-bus"
                                      : i == 2 ? "component:canbus"
                                      : i      ? "component:bootstrap"
                                               : "component:audiopolicy";
        const std::string package_name = i == 3   ? "com.qinggan.QGBus"
                                         : i == 2 ? "com.qinggan.canbus.service"
                                                  : std::string(qinggan09_owned_package_names[i]);
        const auto &artifact = result.at("artifacts").at(reference);
        qinggan09_owned_packages_need(artifact.is_object() && artifact.size() == 3 &&
                                      certificates[i] ==
                                              qinggan09_owned_packages_certificate_sha256);
        Json package = {{"package", package_name},
                        {"file", artifact.at("file")},
                        {"sha256", artifact.at("sha256")},
                        {"bytes", artifact.at("bytes")}};
        android_package_detail::descriptor(package, false, package_name, 16ULL << 20);
        const std::filesystem::path path = package.at("file").get<std::string>();
        qinggan09_owned_packages_need(path.is_absolute() && path.lexically_normal() == path);
        unsigned matches = 0;

        for (const auto &entry : installed) {
            if (entry.value("artifact", std::string{}) == reference) {
                ++matches;
                const auto target = entry.value("target", std::string{});
                qinggan09_owned_packages_need(
                        entry.at("package") == package.at("package") &&
                        entry.at("sha256") == package.at("sha256") &&
                        entry.at("mutable") == false &&
                        (i == 2 ? target == qinggan09_canbus_system_apk
                                : target.starts_with("/data/app/" + package_name + "-") &&
                                          target.ends_with("/base.apk")));
            }
        }

        qinggan09_owned_packages_need(matches == 1);
        packages.push_back(package);
    }

    Json value = {{"schema", 1},
                  {"profile", std::string(logical_profile)},
                  {"api", 28},
                  {"certificate_sha256", std::string(qinggan09_owned_packages_certificate_sha256)},
                  {"receipt_sha256", sha256_bytes(bytes)},
                  {"packages", packages}};

    if constexpr (Count == 4) {
        const auto package = value["packages"].at(3);
        value["packages"].erase(3);
        value["navigation_bus"] = {{"package", package},
                                   {"expected_previous_sha256", package.at("sha256")},
                                   {"predecessor_receipt_sha256", sha256_bytes(bytes)}};
    }

    if constexpr (Count >= 3) {
        const auto package = value["packages"].at(2);
        value["packages"].erase(2);
        value["canbus"] = {{"package", package},
                           {"expected_previous_sha256", package.at("sha256")},
                           {"predecessor_receipt_sha256", sha256_bytes(bytes)}};
    }

    qinggan09_owned_packages_descriptor(value, false);

    return value;
}

// A navigation refresh carries the already accepted CanBus lineage across path relocation.
inline void qinggan09_owned_packages_preserve_canbus_role(Json &candidate, const Json &previous) {
    qinggan09_owned_packages_descriptor(candidate, false);
    qinggan09_owned_packages_descriptor(previous, false);
    qinggan09_owned_packages_need(candidate.contains("navigation_bus") &&
                                  previous.contains("canbus"));
    const auto &desired = candidate.at("canbus").at("package");
    const auto &accepted = previous.at("canbus").at("package");

    for (const auto *field : {"package", "sha256", "bytes"}) {
        qinggan09_owned_packages_need(desired.at(field) == accepted.at(field));
    }

    const std::filesystem::path file = desired.at("file").get<std::string>();
    qinggan09_owned_packages_need(file.is_absolute() && file.lexically_normal() == file);

    for (const auto *field : {"expected_previous_sha256", "predecessor_receipt_sha256"}) {
        candidate["canbus"][field] = previous.at("canbus").at(field);
    }
}

// Copies permit portable callers to rewrite confined paths without exposing aliases.
inline Json qinggan09_owned_packages_payloads(const Json &value) {
    qinggan09_owned_packages_descriptor(value, false);
    auto result = value.at("packages");

    if (value.contains("canbus")) {
        result.push_back(value.at("canbus").at("package"));
    }

    if (value.contains("navigation_bus")) {
        result.push_back(value.at("navigation_bus").at("package"));
    }

    return result;
}

inline void qinggan09_owned_packages_set_payload(Json &value, std::size_t index,
                                                 const Json &package) {
    qinggan09_owned_packages_need(index < 2 || (index == 2 && value.contains("canbus")) ||
                                  (index == 3 && value.contains("navigation_bus")));

    if (index < 2) {
        value["packages"][index] = package;
    } else {
        value[index == 2 ? "canbus" : "navigation_bus"]["package"] = package;
    }
}

inline std::vector<std::filesystem::path>
qinggan09_owned_packages_files(const Json &specification) {
    qinggan09_owned_packages_need(
            specification.contains("qinggan09_owned_packages") &&
            specification.value("reference_profile",
                                specification.value("profile", std::string{})) ==
                    specification.at("qinggan09_owned_packages").value("profile", std::string{}) &&
            qinggan09_owned_packages_integer(specification, "api", 28));
    const auto &value = specification.at("qinggan09_owned_packages");
    qinggan09_owned_packages_descriptor(value, false);
    std::vector<std::filesystem::path> files;
    const auto payloads = qinggan09_owned_packages_payloads(value);

    for (std::size_t i = 0; i < payloads.size(); ++i) {
        const std::string name = i == 3   ? "com.qinggan.QGBus"
                                 : i == 2 ? "com.qinggan.canbus.service"
                                          : std::string(qinggan09_owned_package_names[i]);
        auto single = specification;
        single["qinggan09_owned_package"] = payloads.at(i);
        const auto file = android_package_detail::file(single, "qinggan09_owned_package", name,
                                                       16ULL << 20, 28);
        const auto identity = cache_file_identity(file);
        qinggan09_owned_packages_need(sha256_file(file) ==
                                              payloads.at(i).at("sha256").get<std::string>() &&
                                      cache_file_identity(file) == identity);
        files.push_back(file);
    }

    return files;
}

// A navigation bus update first inspects both exact signed-byte predecessors without mutation.
// Certificate evidence is transitive from the producer's checked APK SHA256.
inline void qinggan09_owned_packages_preflight_roles(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    const auto &value = specification.at("qinggan09_owned_packages");

    if (!value.contains("navigation_bus")) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    auto shell = [&](const std::vector<std::string> &args) {
        const auto frame = random_token();
        fence();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
        qinggan09_owned_packages_need(left > 0);
        const auto result = run({"shell", windows_startup_command(args, frame, nonce, boot, false)},
                                static_cast<int>(left));
        fence();
        qinggan09_owned_packages_need(!result.timed_out && result.exit_code == 0 &&
                                      result.output.size() <= 8192);
        const auto parsed = parse_windows_startup_frame(result, frame, nonce, boot);
        qinggan09_owned_packages_need(!parsed.boot_changed && !parsed.result.timed_out &&
                                      parsed.result.exit_code == 0);

        return parsed.result.output;
    };

    for (const auto *role : {"canbus", "navigation_bus"}) {
        const auto &owned = value.at(role);
        const auto package = owned.at("package").at("package").get<std::string>();
        auto path = shell({"pm", "path", package});

        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
            path.pop_back();
        }

        qinggan09_owned_packages_need(path.starts_with("package:"));
        path.erase(0, 8);
        const std::filesystem::path selected = path;
        qinggan09_owned_packages_need(
                path.size() <= 1024 &&
                ((path.starts_with("/data/app/") && path.ends_with("/base.apk")) ||
                 (std::string_view(role) == "canbus" && path == qinggan09_canbus_system_apk)) &&
                selected.lexically_normal() == selected &&
                std::regex_match(path, std::regex("[A-Za-z0-9_~+./=\\-]+")));
        auto digest = shell({"sha256sum", path});

        while (!digest.empty() && (digest.back() == '\n' || digest.back() == '\r')) {
            digest.pop_back();
        }

        qinggan09_owned_packages_need(
                digest.size() == 66 + path.size() && digest.substr(64) == "  " + path &&
                qinggan09_owned_packages_hash(digest.substr(0, 64)) &&
                (digest.substr(0, 64) == owned.at("expected_previous_sha256").get<std::string>() ||
                 digest.substr(0, 64) == owned.at("package").at("sha256").get<std::string>()));
    }
}

inline Json ensure_qinggan09_navigation_bus_package(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    (void)qinggan09_owned_packages_files(specification);
    const auto &value = specification.at("qinggan09_owned_packages");

    if (!value.contains("navigation_bus")) {
        return Json();
    }

    qinggan09_owned_packages_preflight_roles(specification, nonce, boot, run, fence);
    auto single = specification;
    single["qinggan09_owned_package"] = value.at("navigation_bus").at("package");

    return android_package_detail::ensure(
            single, nonce, boot, run, fence, "qinggan09_owned_package", "com.qinggan.QGBus",
            16ULL << 20, 28,
            value.at("navigation_bus").at("expected_previous_sha256").get<std::string>());
}

// Runtime calls this before the legacy audio pair, so an unknown CanBus predecessor
// cannot cause any package mutation. The common installer retains API28 framing and guards.
inline Json ensure_qinggan09_canbus_package(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    (void)qinggan09_owned_packages_files(specification);
    const auto &value = specification.at("qinggan09_owned_packages");

    if (!value.contains("canbus")) {
        return Json();
    }

    qinggan09_owned_packages_preflight_roles(specification, nonce, boot, run, fence);
    auto single = specification;
    single["qinggan09_owned_package"] = value.at("canbus").at("package");

    return android_package_detail::ensure(
            single, nonce, boot, run, fence, "qinggan09_owned_package",
            "com.qinggan.canbus.service", 16ULL << 20, 28,
            value.at("canbus").at("expected_previous_sha256").get<std::string>(),
            std::string(qinggan09_canbus_system_apk));
}

inline Json ensure_qinggan09_owned_packages(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    (void)qinggan09_owned_packages_files(specification);
    qinggan09_owned_packages_preflight_roles(specification, nonce, boot, run, fence);
    Json results = Json::array();

    for (std::size_t i = 0; i < 2; ++i) {
        auto single = specification;
        single["qinggan09_owned_package"] =
                specification.at("qinggan09_owned_packages").at("packages").at(i);
        results.push_back(android_package_detail::ensure(
                single, nonce, boot, run, fence, "qinggan09_owned_package",
                std::string(qinggan09_owned_package_names[i]), 16ULL << 20, 28));
    }

    return results;
}
} // namespace voyemu
