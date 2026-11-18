#pragma once
#include "android_owned_package.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "phone_control.hpp"
#include "toolchain_manifest.hpp"

namespace voyemu {
inline constexpr std::string_view qinggan09_phone_package_profile =
        "free-09-chinese-20260106-6.0.3";
inline constexpr std::string_view qinggan09_phone_package_name = "com.qinggan.bluetoothphone";
inline constexpr std::string_view qinggan09_phone_package_certificate =
        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8";
inline constexpr std::string_view qinggan09_phone_package_previous =
        "031e29def6f4d388e47e9a3bbd68f7a315a900b8e89f5b7e75eea29ee324ee2b";

inline void qinggan09_phone_package_need(bool value) {
    if (!value) {
        throw VoyemuError("QINGGAN09_PHONE_PACKAGE",
                          "exact source-prepared OEM Phone update required");
    }
}

inline bool qinggan09_phone_package_hash(const Json &value) {
    if (!value.is_string()) {
        return false;
    }

    const auto text = value.get<std::string>();

    return sha256_text(text);
}

inline bool qinggan09_phone_package_integer(const Json &object, const char *key, int value) {
    return object.contains(key) && object.at(key).is_number_integer() && object.at(key) == value;
}

inline void qinggan09_phone_package_descriptor(const Json &value, bool portable) {
    const bool initial = !value.contains("predecessor_receipt_sha256");

    qinggan09_phone_package_need(
            value.is_object() && value.size() == (initial ? 7 : 8) &&
            qinggan09_phone_package_integer(value, "schema", 1) &&
            qinggan09_phone_package_integer(value, "api", 28) &&
            value.at("profile") == qinggan09_phone_package_profile &&
            value.at("certificate_sha256") == qinggan09_phone_package_certificate &&
            (initial ? value.at("expected_previous_sha256") == qinggan09_phone_package_previous
                     : qinggan09_phone_package_hash(value.at("expected_previous_sha256"))) &&
            (initial || qinggan09_phone_package_hash(value.at("predecessor_receipt_sha256"))) &&
            qinggan09_phone_package_hash(value.at("receipt_sha256")));
    android_package_detail::descriptor(value.at("package"), portable,
                                       std::string(qinggan09_phone_package_name), 128ULL << 20);
}

inline std::filesystem::path qinggan09_phone_package_file(const Json &spec) {
    qinggan09_phone_package_need(
            qinggan09_phone_package_integer(spec, "api", 28) &&
            spec.value("reference_profile", spec.value("profile", std::string{})) ==
                    qinggan09_phone_package_profile);
    const auto &descriptor = spec.at("qinggan09_phone_package");
    qinggan09_phone_package_descriptor(descriptor, false);
    Json local = spec;
    local["owned_package"] = descriptor.at("package");
    const auto path = android_package_detail::file(
            local, "owned_package", std::string(qinggan09_phone_package_name), 128ULL << 20, 28);
    const auto before = cache_file_identity(path);
    qinggan09_phone_package_need(sha256_file(path) ==
                                         descriptor.at("package").at("sha256").get<std::string>() &&
                                 before == cache_file_identity(path));

    return path;
}

inline Json ensure_qinggan09_phone_package(
        const Json &spec, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    (void)qinggan09_phone_package_file(spec);
    Json local = spec;
    local["owned_package"] = spec.at("qinggan09_phone_package").at("package");

    return android_package_detail::ensure(
            local, nonce, boot, run, fence, "owned_package",
            std::string(qinggan09_phone_package_name), 128ULL << 20, 28,
            spec.at("qinggan09_phone_package").at("expected_previous_sha256").get<std::string>());
}

template <class Install>
inline Json qinggan09_phone_package_exchange(const std::string &boot, std::string &retained_boot,
                                             bool &unknown, std::int64_t &provider_pid,
                                             Install install) {
    require_phone_outcome_known(boot, retained_boot, unknown, true);
    try {
        return install();
    } catch (const std::exception &error) {
        if (std::string_view(error.what()).starts_with("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN:")) {
            retained_boot = boot;
            provider_pid = 0;
            unknown = true;
            throw VoyemuError("PHONE_DELIVERY_UNKNOWN",
                              "OEM Phone replacement outcome requires a new owned session");
        }

        throw;
    }
}
} // namespace voyemu
