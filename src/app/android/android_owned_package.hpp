#pragma once
#include "check.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "profile_catalog.hpp"
#include "quickboot.hpp"
#include "windows_startup.hpp"
#include <chrono>
#include <functional>
#include <regex>
namespace voyemu {
namespace android_package_detail {
// PM failures are actionable diagnostics, not proof that a timed-out install left data unchanged.
inline std::string install_failure(const std::string &package, int exit_code,
                                   const std::string &output) {
    std::string text = std::regex_replace(output, std::regex("VOYEMU_STARTUP_[A-Za-z0-9_:.-]+"),
                                          "<frame>");

    text = std::regex_replace(
            text,
            std::regex(
                    "[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}|[0-9A-Fa-f]{32,}"),
            "<redacted>");
    text = std::regex_replace(text, std::regex("/[A-Za-z0-9_~+./=<>:-]+"), "<path>");

    for (auto &c : text) {
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) {
            c = ' ';
        }
    }

    if (text.size() > 768) {
        text = text.substr(0, 768) + " [truncated]";
    }

    if (text.empty()) {
        text = "<empty PM output>";
    }

    return "ANDROID_OWNED_PACKAGE: replacement failed package=" + package +
           " exit=" + std::to_string(exit_code) + " pm=" + text;
}

inline void descriptor(const Json &value, bool portable, const std::string &package,
                       std::uint64_t limit) {
    auto need = [](bool yes) {
        if (!yes) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE",
                              "exact pinned prepared APK descriptor required");
        }
    };

    need(value.is_object() && value.size() == 4 && value.contains("package") &&
         value.contains("file") && value.contains("sha256") && value.contains("bytes"));
    need(value.at("package") == package && value.at("file").is_string() &&
         value.at("sha256").is_string() && value.at("bytes").is_number_integer());
    const auto sha = value.at("sha256").get<std::string>();
    need(sha256_text(sha) && value.at("bytes").get<std::uint64_t>() > 0 &&
         value.at("bytes").get<std::uint64_t>() <= limit);

    if (portable) {
        need(value.at("file") == "android/packages/" + sha + ".apk");
    }
}

inline std::filesystem::path file(const Json &specification, const std::string &key,
                                  const std::string &package, std::uint64_t limit,
                                  unsigned expected_api = 30) {
    const auto &value = specification.at(key);

    descriptor(value, false, package, limit);
    const std::filesystem::path machine = specification.at("machine").get<std::string>(),
                                file = value.at("file").get<std::string>();

    if (specification.value("api", 0) != static_cast<int>(expected_api) || !machine.is_absolute() ||
        machine.lexically_normal() != machine ||
        file != machine / "packages" / (value.at("sha256").get<std::string>() + ".apk")) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE", "confined exact-API machine package required");
    }

    for (const auto &at : os::ancestors(file)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "linked package path rejected");
        }
    }

    if (!std::filesystem::is_regular_file(file) || std::filesystem::hard_link_count(file) != 1 ||
        std::filesystem::file_size(file) != value.at("bytes").get<std::uint64_t>()) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE",
                          "machine APK missing or changed; restore its pinned package file");
    }

    return file;
}

// The caller supplies its retained private SDK transport and fresh host/guest ownership fence.
// A package at system_path is a persistent system app, which PackageManager refuses to update.
inline Json ensure(const Json &specification, const std::string &nonce, const std::string &boot,
                   const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
                   const std::function<void()> &fence, const std::string &key,
                   const std::string &package, std::uint64_t limit, unsigned expected_api = 30,
                   const std::string &expected_previous_sha256 = {},
                   const std::string &system_path = {}) {
    const auto file = android_package_detail::file(specification, key, package, limit,
                                                   expected_api);
    const auto &descriptor = specification.at(key);
    const auto sha = descriptor.at("sha256").get<std::string>();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    auto checked = [&](const std::vector<std::string> &args) {
        fence();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();

        if (left <= 0) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "update deadline exhausted");
        }

        auto result = run(args, static_cast<int>(left));
        fence();

        if (result.timed_out || result.exit_code || result.output.size() > 8192) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "owned package transport failed");
        }

        return result;
    };
    auto shell = [&](const std::vector<std::string> &args, bool mutation,
                     const std::string &input = std::string{}) {
        const auto frame = random_token();
        std::vector<std::string> command = args;

        if (!input.empty()) {
            std::string joined;

            for (const auto &arg : args) {
                if (!joined.empty()) {
                    joined += ' ';
                }

                joined += android_shell_quote(arg);
            }

            joined += " < " + android_shell_quote(input);
            command = {joined};
        }

        const auto result = parse_windows_startup_frame(
                checked({"shell", windows_startup_command(command, frame, nonce, boot, mutation, {},
                                                          !input.empty())}),
                frame, nonce, boot);

        if (result.boot_changed || result.result.timed_out) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "guest identity changed during update");
        }

        return result.result;
    };
    auto path = [&]() {
        auto result = shell({"pm", "path", package}, false);

        while (result.exit_code == 20 &&
               (result.output == "cmd: Can\'t find service: package" ||
                result.output == "cmd: Can\'t find service: package\n" ||
                result.output == "cmd: Can\'t find service: package\r\n")) {
            result = shell({"pm", "path", package}, false);
        }

        if (result.exit_code && result.output.empty()) {
            return std::string{};
        }

        if (result.exit_code) {
            throw std::runtime_error(install_failure(package, result.exit_code,
                                                     "package path unavailable: " + result.output));
        }

        auto text = result.output;

        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }

        if (text.empty()) {
            return text;
        }

        if (!text.starts_with("package:")) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "ambiguous installed package path");
        }

        text.erase(0, 8);
        const std::filesystem::path selected = text;

        if (text.size() > 1024 ||
            ((!text.starts_with("/data/app/") || !text.ends_with(".apk")) &&
             (system_path.empty() || text != system_path)) ||
            selected.lexically_normal() != selected ||
            text.find_first_of("\r\n") != std::string::npos ||
            !std::regex_match(text, std::regex("[A-Za-z0-9_~+./=\\-]+"))) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "installed APK path differs");
        }

        return text;
    };
    auto digest = [&](const std::string &selected) {
        const auto result = shell({"sha256sum", selected}, false);

        if (result.exit_code || result.output.size() < 66 || result.output.substr(64, 2) != "  " ||
            !sha256_text(result.output.substr(0, 64))) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "installed APK digest unavailable");
        }

        return result.output.substr(0, 64);
    };

    if (key == "camera_package" || key == "prepared_package") {
        const auto identity = cache_file_identity(file);

        if (sha256_file(file) != sha || cache_file_identity(file) != identity) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "pinned prepared APK hash differs");
        }
    }

    auto current = path();
    const auto current_sha = current.empty() ? std::string{} : digest(current);

    if (current_sha == sha) {
        return Json{
                {"package", package}, {"sha256", sha}, {"status", "current"}, {"installed", false}};
    }

    if (!system_path.empty() && current == system_path) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE",
                          "system package differs; rebuild the machine to replace it");
    }

    if (!expected_previous_sha256.empty() &&
        (current.empty() || current_sha != expected_previous_sha256)) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE",
                          "installed OEM package differs; preserve the existing application");
    }

    const auto identity = cache_file_identity(file);

    if (sha256_file(file) != sha || cache_file_identity(file) != identity) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE", "pinned machine APK hash differs");
    }

    const auto guest_directory = "/data/local/tmp/voyemu-owned-" + nonce + "-" + random_token(),
               guest = guest_directory + "/provider.apk";
    bool created = false, replacement_dispatched = false;
    auto cleanup = [&] {
        if (!created) {
            return;
        }

        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        const auto removed = shell({"rm", "-f", guest}, true);
        const auto directory = shell({"rmdir", guest_directory}, true);

        if (removed.exit_code || directory.exit_code) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "owned staging cleanup failed");
        }

        created = false;
    };
    try {
        const auto directory = shell({"mkdir", "-m", "0700", guest_directory}, true);

        if (directory.exit_code) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "exclusive owned staging unavailable");
        }

        created = true;
        checked({"push", file.string(), guest});

        if (digest(guest) != sha) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "staged APK digest differs");
        }

        if (!expected_previous_sha256.empty()) {
            current = path();
            const auto before_install_sha = current.empty() ? std::string{} : digest(current);

            if (before_install_sha == sha) {
                cleanup();

                return Json{{"package", package},
                            {"sha256", sha},
                            {"status", "current"},
                            {"installed", false}};
            }

            if (current.empty() || before_install_sha != expected_previous_sha256) {
                throw VoyemuError(
                        "ANDROID_OWNED_PACKAGE",
                        "installed OEM package changed before replacement; preserve the existing application");
            }
        }

        std::vector<std::string> install_args = {
                "pm",
                "install",
                "-r",
                "--pkg",
                package,
                "-S",
                std::to_string(descriptor.at("bytes").get<std::uint64_t>())};
        // OEM API30 and API28 treat a bare dash as an option; explicit size selects stdin.
        replacement_dispatched = true;
        const auto installed = shell(install_args, true, guest);

        if (installed.exit_code ||
            !std::regex_search(installed.output, std::regex("(?:^|\\n)Success(?:\\r?\\n|$)"))) {
            throw std::runtime_error(
                    install_failure(package, installed.exit_code, installed.output));
        }

        current = path();

        if (current.empty() || digest(current) != sha) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "installed APK readback differs");
        }

        if (cache_file_identity(file) != identity) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE", "update post-fence differs");
        }

        cleanup();
    } catch (const std::exception &error) {
        const auto message = std::string(error.what());
        try {
            cleanup();
        } catch (...) { /* Ignored: cleanup must not replace the original failure. */
        }

        if (replacement_dispatched && !expected_previous_sha256.empty()) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN", message);
        }

        throw;
    } catch (...) {
        try {
            cleanup();
        } catch (...) { /* Ignored: cleanup must not replace the original failure. */
        }

        if (replacement_dispatched && !expected_previous_sha256.empty()) {
            throw VoyemuError("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN",
                              "replacement did not return a verified result");
        }

        throw;
    }

    return Json{{"package", package}, {"sha256", sha}, {"status", "updated"}, {"installed", true}};
}
} // namespace android_package_detail

inline void android_owned_package_descriptor(const Json &value, bool portable) {
    android_package_detail::descriptor(value, portable, "com.voyemu.input.wheel", 16ULL << 20);
}

inline std::filesystem::path android_owned_package_file(const Json &specification) {
    return android_package_detail::file(specification, "owned_package", "com.voyemu.input.wheel",
                                        16ULL << 20);
}

inline void android_prepared_packages_descriptor(const Json &packages, bool portable) {
    if (!packages.is_array() || packages.size() != 2) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE", "exact Free A11 prepared packages required");
    }

    android_package_detail::descriptor(packages.at(0), portable, "com.qinggan.camera",
                                       128ULL << 20);
    android_package_detail::descriptor(packages.at(1), portable, "com.baidu.naviauto",
                                       512ULL << 20);
}

inline Json ensure_android_prepared_packages(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    if (!embedded_profile_declares(specification.value("reference_profile", std::string{}),
                                   ProfilePlatform::qinggan11, ProfileBoard::sa8155)) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE", "exact Free A11 profile required");
    }

    const auto &packages = specification.at("prepared_packages");
    android_prepared_packages_descriptor(packages, false);
    Json receipts = Json::array();

    for (const auto &package : packages) {
        auto spec = specification;
        spec["prepared_package"] = package;
        const auto name = package.at("package").get<std::string>();
        const auto limit = name == "com.baidu.naviauto" ? 512ULL << 20 : 128ULL << 20;
        receipts.push_back(android_package_detail::ensure(spec, nonce, boot, run, fence,
                                                          "prepared_package", name, limit));
    }

    return receipts;
}

inline void android_camera_package_descriptor(const Json &value, bool portable) {
    android_package_detail::descriptor(value, portable, "com.qinggan.camera", 128ULL << 20);
}

inline std::filesystem::path android_camera_package_file(const Json &specification) {
    if (!embedded_profile_declares(specification.value("reference_profile", std::string{}),
                                   ProfilePlatform::qinggan11, ProfileBoard::sa8155)) {
        throw VoyemuError("ANDROID_OWNED_PACKAGE", "exact Free A11 Camera profile required");
    }

    return android_package_detail::file(specification, "camera_package", "com.qinggan.camera",
                                        128ULL << 20);
}

inline Json ensure_android_camera_package(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    (void)android_camera_package_file(specification);

    return android_package_detail::ensure(specification, nonce, boot, run, fence, "camera_package",
                                          "com.qinggan.camera", 128ULL << 20);
}

inline Json ensure_android_owned_package(
        const Json &specification, const std::string &nonce, const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<void()> &fence) {
    return android_package_detail::ensure(specification, nonce, boot, run, fence, "owned_package",
                                          "com.voyemu.input.wheel", 16ULL << 20);
}
} // namespace voyemu
