#include "android_owned_package.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

static void check(bool yes, const char *reason) {
    if (!yes) {
        throw std::runtime_error(reason);
    }
}

int main() {
    try {
        const auto diagnostic = android_package_detail::install_failure(
                "com.qinggan.audiopolicy.service", 1,
                "Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: signatures do not match]\nservice_unavailable\033\nVOYEMU_STARTUP_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:body bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 12345678-1234-1234-1234-123456789abc");
        check(diagnostic.find("package=com.qinggan.audiopolicy.service exit=1") !=
                              std::string::npos &&
                      diagnostic.find("INSTALL_FAILED_UPDATE_INCOMPATIBLE") != std::string::npos &&
                      diagnostic.find("signatures do not match") != std::string::npos &&
                      diagnostic.find("service_unavailable") != std::string::npos,
              "PM causal diagnostics lost");
        check(diagnostic.find(std::string(32, 'b')) == std::string::npos &&
                      diagnostic.find("VOYEMU_STARTUP_") == std::string::npos &&
                      diagnostic.find("12345678-1234") == std::string::npos &&
                      diagnostic.find('\033') == std::string::npos &&
                      diagnostic.find('\n') == std::string::npos,
              "PM diagnostic leaks identity or control characters");
        check(android_package_detail::install_failure("owned.package", 7, std::string(8192, '!'))
                              .size() < 1000,
              "PM diagnostic is unbounded");
        check(android_package_detail::install_failure("owned.package", 9, "")
                              .find("exit=9 pm=<empty PM output>") != std::string::npos,
              "empty PM failure hidden");
        const auto stage = std::filesystem::current_path() / ".local/build/tmp" /
                           ("owned-package-test-" + random_token()),
                   machine = stage / "android";
        ensure_private_directory(machine / "packages");
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{stage};
        const std::string contents = "synthetic normally signed APK bytes", nonce(32, 'b'),
                          boot = "12345678-1234-1234-1234-123456789abc",
                          other = "12345678-1234-1234-1234-123456789abd",
                          sha = sha256_bytes(contents),
                          installed_path =
                                  "/data/app/~~owned/com.voyemu.input.wheel-test==/base.apk";
        const auto apk = machine / "packages" / (sha + ".apk");
        auto restore = [&] {
            std::ofstream(apk, std::ios::binary | std::ios::trunc) << contents;
        };
        restore();
        Json descriptor = {{"package", "com.voyemu.input.wheel"},
                           {"file", apk.string()},
                           {"sha256", sha},
                           {"bytes", static_cast<std::uint64_t>(contents.size())}},
             spec = {{"api", 30}, {"machine", machine.string()}, {"owned_package", descriptor}};

        const auto appdata = stage / "existing-app-private-data";
        std::ofstream(appdata) << "private preferences and installed apps remain";
        const auto appdata_identity = cache_file_identity(appdata);
        const auto appdata_sha = sha256_file(appdata);
        unsigned cases = 0;

        for (bool camera : {false, true}) {
            const auto package = camera ? std::string("com.qinggan.camera")
                                        : std::string("com.voyemu.input.wheel");
            auto selected = spec;

            if (camera) {
                selected["reference_profile"] = "free-11-chinese-20260731-7.3.1";
                selected.erase("owned_package");
                auto d = descriptor;
                d["package"] = package;
                selected["camera_package"] = d;
            }

            const auto selected_path =
                    camera ? std::string("/data/app/~~owned/com.qinggan.camera-test==/base.apk")
                           : installed_path;

            for (const auto *mode :
                 {"update", "api28-update", "package-service-delayed", "missing-installed",
                  "current", "pm-failed", "pm-partial", "pm-timeout", "path-failed",
                  "wrong-apk-package", "bad-readback", "split-path", "foreign-path", "wrong-nonce",
                  "new-boot", "foreign-staging"}) {
                if (camera && std::string_view(mode) == "api28-update") {
                    continue;
                }

                std::string installed = std::string_view(mode) == "current" ? sha
                                                                            : std::string(64, '0');
                bool missing = std::string_view(mode) == "missing-installed", created = false,
                     pushed = false, replaced = false;
                unsigned commands = 0, installs = 0, fences = 0, path_reads = 0;
                std::string guest_directory;
                auto run = [&](const std::vector<std::string> &args, int budget) -> ProcessResult {
                    check(budget > 0 && budget <= 60000, "update budget bound");
                    ++commands;

                    for (const auto &arg : args) {
                        check(arg != "uninstall" && arg != "clear", "destructive package command");
                    }

                    if (args.at(0) == "push") {
                        check(created && args.size() == 3 && args[1] == apk.string() &&
                                      args[2] == guest_directory + "/provider.apk",
                              "confined owned push");
                        pushed = true;

                        return {0, "pushed", false};
                    }

                    check(args.size() == 2 && args[0] == "shell", "framed shell required");
                    const auto &script = args[1];
                    const auto at = script.find("VOYEMU_STARTUP_");
                    check(at != std::string::npos, "frame missing");
                    const auto marker = script.substr(at, 47);
                    std::string body;
                    int status = 0;

                    if (script.find("'pm' 'path'") != std::string::npos) {
                        ++path_reads;

                        if (std::string_view(mode) == "package-service-delayed" &&
                            path_reads <= 2) {
                            status = 20;
                            body = "cmd: Can't find service: package";
                        } else if (std::string_view(mode) == "path-failed") {
                            status = 1;
                            body = "package service_unavailable";
                        } else if (std::string_view(mode) == "split-path") {
                            body = "package:" + selected_path +
                                   "\npackage:/data/app/other/split.apk\n";
                        } else if (std::string_view(mode) == "foreign-path") {
                            body = "package:/system/app/foreign.apk\n";
                        } else if (missing) {
                            status = 1;
                        } else {
                            body = "package:" + selected_path + "\n";
                        }
                    } else if (script.find("'sha256sum'") != std::string::npos) {
                        const bool staged = script.find("/provider.apk") != std::string::npos;
                        check(!staged || pushed, "staged digest before push");
                        const auto path = staged ? guest_directory + "/provider.apk"
                                                 : selected_path;
                        body = (staged ? sha : installed) + "  " + path + "\n";
                    } else if (script.find("'mkdir' '-m' '0700'") != std::string::npos) {
                        const auto start = script.find("/data/local/tmp/voyemu-owned-"),
                                   end = script.find('\'', start);
                        check(start != std::string::npos && end != std::string::npos,
                              "owned staging path");
                        guest_directory = script.substr(start, end - start);
                        check(guest_directory.starts_with("/data/local/tmp/voyemu-owned-" + nonce +
                                                          "-") &&
                                      guest_directory.size() ==
                                              std::string("/data/local/tmp/voyemu-owned-" + nonce +
                                                          "-")
                                                              .size() +
                                                      32,
                              "nonce random staging");

                        if (std::string_view(mode) == "foreign-staging") {
                            status = 1;
                        } else {
                            created = true;
                        }
                    } else if (script.find("'pm' 'install'") != std::string::npos) {
                        check(script.find("'pm' 'install' '-r' '--pkg' '" + package + "'") !=
                                      std::string::npos,
                              "expected package restriction missing");
                        check(created && pushed &&
                                      script.find("[ \"$b\" = '" + boot + "' ] || exit 79;") !=
                                              std::string::npos,
                              "mutation boot guard missing");
                        check(script.find("'-S' '" + std::to_string(contents.size()) + "'") !=
                                              std::string::npos &&
                                      script.find(" < '" + guest_directory + "/provider.apk'") !=
                                              std::string::npos,
                              "PM must stream pinned bytes via confined stdin, never system_server file open");
                        ++installs;

                        if (std::string_view(mode) == "pm-timeout") {
                            return {1, "", true};
                        }

                        if (script.find(" '-' < ") != std::string::npos) {
                            status = 255;
                            body = "java.lang.IllegalArgumentException: Unknown option -";
                        } else if (std::string_view(mode) == "pm-partial") {
                            status = 1;
                            body = "Failure [INSTALL_FAILED_INVALID_APK: truncated input]";
                        } else if (std::string_view(mode) == "pm-failed" ||
                                   std::string_view(mode) == "wrong-apk-package") {
                            status = 1;
                            body = "Failure [INSTALL_FAILED_INVALID_APK]";
                        } else {
                            installed = std::string_view(mode) == "bad-readback"
                                                ? std::string(64, 'c')
                                                : sha;
                            missing = false;
                            replaced = true;
                            body = "Success\n";
                        }
                    } else if (script.find("'rm' '-f'") != std::string::npos) {
                        check(created, "cleanup foreign staging");
                        pushed = false;
                    } else if (script.find("'rmdir'") != std::string::npos) {
                        check(created && !pushed, "cleanup ordering");
                        created = false;
                    } else {
                        throw std::runtime_error("unexpected package command");
                    }

                    const auto returned_nonce =
                            std::string_view(mode) == "wrong-nonce" ? std::string(32, 'a') : nonce;
                    const auto returned_boot = std::string_view(mode) == "new-boot" ? other : boot;

                    return {0,
                            marker + ":b1:" + returned_boot + "\n" + marker +
                                    ":n1:" + returned_nonce + "\n" + marker + ":body\n" + body +
                                    "\n" + marker + ":exit:" + std::to_string(status) + "\n" +
                                    marker + ":n2:" + returned_nonce + "\n" + marker +
                                    ":b2:" + returned_boot + "\n" + marker + ":utc:1700000000\n",
                            false};
                };
                bool failed = false;
                std::string failure;
                Json result;
                try {
                    if (!camera && std::string_view(mode) == "api28-update") {
                        auto api28spec = selected;
                        api28spec["api"] = 28;
                        result = android_package_detail::ensure(
                                api28spec, nonce, boot, run,
                                [&] {
                                    ++fences;
                                },
                                "owned_package", "com.voyemu.input.wheel", 16ULL << 20, 28);
                    } else if (camera) {
                        result = ensure_android_camera_package(selected, nonce, boot, run, [&] {
                            ++fences;
                        });
                    } else {
                        result = ensure_android_owned_package(selected, nonce, boot, run, [&] {
                            ++fences;
                        });
                    }
                } catch (const std::exception &error) {
                    failed = true;
                    failure = error.what();
                }

                const bool success = std::string_view(mode) == "package-service-delayed" ||
                                     std::string_view(mode) == "update" ||
                                     std::string_view(mode) == "api28-update" ||
                                     std::string_view(mode) == "missing-installed" ||
                                     std::string_view(mode) == "current";

                if (failed == success) {
                    throw std::runtime_error(std::string(mode) +
                                             ": package update outcome differs: " + failure);
                }

                if (success) {
                    check(result.at("installed") == (std::string_view(mode) != "current"),
                          "match must skip replacement");
                }

                if (std::string_view(mode) == "current") {
                    check(installs == 0 && !created && !pushed, "current APK installed again");
                }

                if (std::string_view(mode) == "pm-failed") {
                    check(failure.find("package=" + package + " exit=1") != std::string::npos &&
                                  failure.find("INSTALL_FAILED_INVALID_APK") != std::string::npos &&
                                  failure.find("data retained") == std::string::npos,
                          "actual PM failure swallowed or outcome invented");
                }

                if (std::string_view(mode) == "wrong-apk-package" ||
                    std::string_view(mode) == "pm-failed") {
                    check(!replaced && installed == std::string(64, '0'),
                          "failed install replaced app");
                }

                check(installs <= 1, "unknown or failed installation retried");

                if (std::string_view(mode) == "pm-partial") {
                    check(failure.find("truncated input") != std::string::npos,
                          "partial stdin cause hidden");
                }

                if (std::string_view(mode) == "path-failed") {
                    check(failure.find("service_unavailable") != std::string::npos &&
                                  failure.find("exit=1") != std::string::npos,
                          "read path failure swallowed");
                }

                check(!created && !pushed, "owned staging leaked");
                check(fences >= commands * 2 && cache_file_identity(appdata) == appdata_identity &&
                              sha256_file(appdata) == appdata_sha,
                      "app data changed");
                ++cases;
            }
        }

        {
            auto camera = descriptor;
            camera["package"] = "com.qinggan.camera";
            auto navi = descriptor;
            navi["package"] = "com.baidu.naviauto";
            auto prepared = spec;
            prepared["reference_profile"] = "free-11-chinese-20260731-7.3.1";
            prepared["prepared_packages"] = Json::array({camera, navi});

            for (const auto *mode : {"update", "current", "navi-fail"}) {
                std::string selected, guest;
                bool staged = false;
                unsigned installs = 0;
                std::map<std::string, std::string> installed = {
                        {"com.qinggan.camera",
                         std::string_view(mode) == "current" ? sha : std::string(64, '0')},
                        {"com.baidu.naviauto",
                         std::string_view(mode) == "current" ? sha : std::string(64, '0')}};
                auto run = [&](const std::vector<std::string> &args, int) -> ProcessResult {
                    if (args.at(0) == "push") {
                        staged = true;

                        return {0, "", false};
                    }

                    const auto &script = args.at(1);
                    const auto marker = script.substr(script.find("VOYEMU_STARTUP_"), 47);
                    std::string body;
                    int code = 0;

                    if (script.find("'pm' 'path'") != std::string::npos) {
                        selected = script.find("com.qinggan.camera") != std::string::npos
                                           ? "com.qinggan.camera"
                                           : "com.baidu.naviauto";
                        body = "package:/data/app/~~owned/" + selected + "-test==/base.apk\n";
                    } else if (script.find("'sha256sum'") != std::string::npos) {
                        bool input = script.find("/provider.apk") != std::string::npos;
                        body = (input ? sha : installed.at(selected)) + "  " +
                               (input ? guest + "/provider.apk"
                                      : "/data/app/~~owned/" + selected + "-test==/base.apk") +
                               "\n";
                    } else if (script.find("'mkdir'") != std::string::npos) {
                        auto start = script.find("/data/local/tmp/voyemu-owned-");
                        guest = script.substr(start, script.find('\'', start) - start);
                    } else if (script.find("'pm' 'install'") != std::string::npos) {
                        check(staged, "prepared push missing");
                        ++installs;

                        if (std::string_view(mode) == "navi-fail" &&
                            selected == "com.baidu.naviauto") {
                            code = 1;
                            body = "Failure [INSTALL_FAILED_INVALID_APK]";
                        } else {
                            installed[selected] = sha;
                            body = "Success\n";
                        }
                    } else if (script.find("'rm' '-f'") != std::string::npos) {
                        staged = false;
                    } else if (script.find("'rmdir'") == std::string::npos) {
                        throw std::runtime_error("unexpected prepared operation");
                    }

                    return {0,
                            marker + ":b1:" + boot + "\n" + marker + ":n1:" + nonce + "\n" +
                                    marker + ":body\n" + body + "\n" + marker + ":exit:" +
                                    std::to_string(code) + "\n" + marker + ":n2:" + nonce + "\n" +
                                    marker + ":b2:" + boot + "\n" + marker + ":utc:1700000000\n",
                            false};
                };
                bool failed = false;
                Json result;
                try {
                    result = ensure_android_prepared_packages(prepared, nonce, boot, run, [] {});
                } catch (...) {
                    failed = true;
                }

                check(failed == (std::string_view(mode) == "navi-fail"),
                      "prepared failure outcome differs");

                if (!failed) {
                    check(result.size() == 2 && result[0]["package"] == "com.qinggan.camera" &&
                                  result[1]["package"] == "com.baidu.naviauto",
                          "prepared receipts missing");
                    check(installs == (std::string_view(mode) == "current" ? 0 : 2),
                          "prepared idempotence/update differs");
                }

                check(cache_file_identity(appdata) == appdata_identity &&
                              sha256_file(appdata) == appdata_sha,
                      "prepared update changed app data");
            }
        }
        auto bad_camera = spec;
        bad_camera["camera_package"] = descriptor;
        bad_camera["camera_package"]["package"] = "com.qinggan.camera";
        bad_camera["reference_profile"] = "foreign";
        bool reached = false, denied = false;
        try {
            ensure_android_camera_package(
                    bad_camera, nonce, boot,
                    [&](const auto &, int) -> ProcessResult {
                        reached = true;
                        throw std::runtime_error("unexpected");
                    },
                    [] {});
        } catch (...) {
            denied = true;
        }

        check(denied && !reached, "foreign profile Camera reached transport");
        auto rejects = [&](const Json &invalid) {
            bool failed = false, reached = false;
            try {
                ensure_android_owned_package(
                        invalid, nonce, boot,
                        [&](const auto &, int) -> ProcessResult {
                            reached = true;
                            throw std::runtime_error("must not reach transport");
                        },
                        [] {});
            } catch (...) {
                failed = true;
            }

            check(failed && !reached, "unsafe wheel descriptor reached transport");
            ++cases;
        };
        auto invalid = spec;
        invalid["owned_package"]["package"] = "foreign.package";
        rejects(invalid);
        invalid = spec;
        invalid["owned_package"]["file"] = (machine / "../foreign.apk").string();
        rejects(invalid);
        invalid = spec;
        invalid["owned_package"]["sha256"] = std::string(64, 'x');
        rejects(invalid);
        invalid = spec;
        invalid["api"] = 28;
        rejects(invalid);
        std::filesystem::remove(apk);
        rejects(spec);
        restore();
        std::filesystem::create_hard_link(apk, stage / "alias.apk");
        rejects(spec);
        std::filesystem::remove(stage / "alias.apk");
        restore();
        std::ofstream(apk, std::ios::trunc) << std::string(contents.size(), 'X');
        bool tampered = false;
        try {
            ensure_android_owned_package(
                    spec, nonce, boot,
                    [](const auto &args, int) -> ProcessResult {
                        const auto &script = args.at(1);
                        const auto marker = script.substr(script.find("VOYEMU_STARTUP_"), 47);
                        const std::string b = "12345678-1234-1234-1234-123456789abc", n(32, 'b');

                        return {0,
                                marker + ":b1:" + b + "\n" + marker + ":n1:" + n + "\n" + marker +
                                        ":body\n\n" + marker + ":exit:1\n" + marker + ":n2:" + n +
                                        "\n" + marker + ":b2:" + b + "\n" + marker +
                                        ":utc:1700000000\n",
                                false};
                    },
                    [] {});
        } catch (...) {
            tampered = true;
        }

        check(tampered, "tampered package installed");
        ++cases;
        restore();
        std::ofstream(apk, std::ios::trunc) << std::string(contents.size(), 'X');
        auto corrupt_camera = spec;
        corrupt_camera["reference_profile"] = "free-11-chinese-20260731-7.3.1";
        corrupt_camera["camera_package"] = descriptor;
        corrupt_camera["camera_package"]["package"] = "com.qinggan.camera";
        bool camera_reached = false, camera_corrupt = false;
        try {
            ensure_android_camera_package(
                    corrupt_camera, nonce, boot,
                    [&](const auto &, int) -> ProcessResult {
                        camera_reached = true;
                        throw std::runtime_error("unexpected transport");
                    },
                    [] {});
        } catch (...) {
            camera_corrupt = true;
        }

        check(camera_corrupt && !camera_reached,
              "corrupt Camera must fail before current guest check");
        restore();
        std::cout
                << "owned package guarded update, idempotent match, wrong package, split path, missing/tampered file, cleanup and app-data preservation passed: "
                << cases << " cases\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
