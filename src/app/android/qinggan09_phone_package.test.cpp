#include "qinggan09_phone_package.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
// A synthetic qinggan09 profile that built the packages.
constexpr std::string_view test_profile = "test-qinggan09";

void check(bool yes, const char *message) {
    if (!yes) {
        throw std::runtime_error(message);
    }
}

struct Fixture {
    std::filesystem::path root, machine, data, file;
    std::string bytes = "source-built complete OEM Phone fixture", sha = sha256_bytes(bytes);
    Json descriptor, spec;

    Fixture() {
        root = std::filesystem::current_path() / ".local/build/tmp" /
               ("qinggan09-phone-package-" + random_token());
        machine = root / "android";
        ensure_private_directory(machine / "packages");
        file = machine / "packages" / (sha + ".apk");
        std::ofstream(file) << bytes;
        data = root / "userdata";
        std::ofstream(data) << "existing OEM contacts, applications and preferences";
        descriptor = {{"schema", 1},
                      {"api", 28},
                      {"profile", std::string(test_profile)},
                      {"certificate_sha256", std::string(qinggan09_phone_package_certificate)},
                      {"expected_previous_sha256", std::string(qinggan09_phone_package_previous)},
                      {"receipt_sha256", std::string(64, 'b')},
                      {"package",
                       {{"package", std::string(qinggan09_phone_package_name)},
                        {"file", file.string()},
                        {"sha256", sha},
                        {"bytes", bytes.size()}}}};
        spec = {{"api", 28},
                {"profile", std::string(test_profile)},
                {"machine", machine.string()},
                {"qinggan09_phone_package", descriptor}};
    }

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
} // namespace

int main() {
    try {
        Fixture f;
        unsigned cases = 0;
        {
            auto copy = f.spec;
            copy["profile"] = "independent-local-copy";
            bool refused = false;
            try {
                (void)qinggan09_phone_package_file(copy);
            } catch (...) {
                refused = true;
            }

            check(refused, "logical copy needs phone adapter identity");
            copy["reference_profile"] = std::string(test_profile);
            check(qinggan09_phone_package_file(copy) == f.file,
                  "independent logical copy retains phone package");
            copy["reference_profile"] = "unsupported-family";
            refused = false;
            try {
                (void)qinggan09_phone_package_file(copy);
            } catch (...) {
                refused = true;
            }

            check(refused, "foreign reference cannot inherit phone package");
        }

        // A refreshed package names the exact predecessor package and its receipt.
        auto chained = f.descriptor;
        chained["expected_previous_sha256"] = std::string(64, '5');
        chained["predecessor_receipt_sha256"] = std::string(64, 'b');
        chained["receipt_sha256"] = std::string(64, 'c');
        qinggan09_phone_package_descriptor(chained, false);
        ++cases;
        Json bad;
        const std::string nonce(32, '7'), boot = "12345678-1234-1234-1234-123456789abc";
        const auto identity = cache_file_identity(f.data);
        const auto data_sha = sha256_file(f.data);

        for (const bool chain : {false, true}) {
            f.spec["qinggan09_phone_package"] = chain ? chained : f.descriptor;
            const auto expected_previous = f.spec.at("qinggan09_phone_package")
                                                   .at("expected_previous_sha256")
                                                   .get<std::string>();

            for (const std::string mode :
                 {"update", "current", "unknown", "other-known-generation", "missing",
                  "changed-during-push", "desired-during-push", "pm-failed", "bad-readback",
                  "timeout", "wrong-nonce", "new-boot"}) {
                std::string installed = mode == "current"   ? f.sha
                                        : mode == "unknown" ? std::string(64, '9')
                                                            : expected_previous,
                            staged, directory;

                if (mode == "other-known-generation") {
                    installed = std::string(64, '6');
                }

                unsigned mutations = 0, installs = 0, commands = 0, fences = 0;
                bool created = false, pushed = false;
                auto run = [&](const std::vector<std::string> &args, int budget) -> ProcessResult {
                    ++commands;
                    check(budget > 0 && budget <= 60000, "bounded update");

                    if (args.at(0) == "push") {
                        check(created && args.at(2) == directory + "/provider.apk",
                              "confined push");
                        staged = f.sha;
                        pushed = true;
                        ++mutations;

                        if (mode == "changed-during-push") {
                            installed = std::string(64, '9');
                        }

                        if (mode == "desired-during-push") {
                            installed = f.sha;
                        }

                        return {0, "pushed", false};
                    }

                    check(args.size() == 2 && args.at(0) == "shell", "framed transport");
                    const auto &script = args.at(1);
                    const auto at = script.find("VOYEMU_STARTUP_");
                    check(at != std::string::npos, "frame missing");
                    const auto frame = script.substr(at, 47);
                    std::string body;
                    int status = 0;
                    check(script.find("'uninstall'") == std::string::npos &&
                                  script.find("'clear'") == std::string::npos,
                          "destructive update");

                    if (script.find("'pm' 'path'") != std::string::npos) {
                        if (mode == "missing") {
                            status = 1;
                        } else {
                            body = "package:/data/app/~~owned/com.qinggan.bluetoothphone-test==/base.apk\n";
                        }
                    } else if (script.find("'sha256sum'") != std::string::npos) {
                        const bool staging = script.find("/provider.apk") != std::string::npos;
                        body = (staging ? staged : installed) + "  /data/app/~~owned/base.apk\n";
                    } else if (script.find("'mkdir' '-m' '0700'") != std::string::npos) {
                        ++mutations;
                        const auto begin = script.find("/data/local/tmp/voyemu-owned-");
                        directory = script.substr(begin, script.find('\'', begin) - begin);
                        check(directory.starts_with("/data/local/tmp/voyemu-owned-" + nonce + "-"),
                              "nonce staging");
                        created = true;
                    } else if (script.find("'pm' 'install'") != std::string::npos) {
                        ++mutations;
                        ++installs;
                        check(created && pushed &&
                                      script.find("'--pkg' 'com.qinggan.bluetoothphone'") !=
                                              std::string::npos,
                              "exact Phone package");
                        check(script.find("' - '") == std::string::npos &&
                                      script.find(" '-'") == std::string::npos &&
                                      script.find(" < '/data/local/tmp/") != std::string::npos,
                              "API28 must use stdin without bare dash");

                        if (mode == "timeout") {
                            installed = staged;

                            return {0, "", true};
                        }

                        if (mode == "pm-failed") {
                            status = 1;
                            body = "Failure [INSTALL_FAILED_INVALID_APK]";
                        } else {
                            installed = mode == "bad-readback" ? std::string(64, '8') : staged;
                            body = "Success\n";
                        }
                    } else if (script.find("'rm' '-f'") != std::string::npos) {
                        ++mutations;
                        check(created, "foreign cleanup");
                        pushed = false;
                    } else if (script.find("'rmdir'") != std::string::npos) {
                        ++mutations;
                        check(created && !pushed, "cleanup order");
                        created = false;
                    } else {
                        throw std::runtime_error("unexpected update command");
                    }

                    const auto n = mode == "wrong-nonce" ? std::string(32, 'a') : nonce;
                    const auto b = mode == "new-boot" ? "12345678-1234-1234-1234-123456789abd"
                                                      : boot;

                    return {0,
                            frame + ":b1:" + b + "\n" + frame + ":n1:" + n + "\n" + frame +
                                    ":body\n" + body + "\n" + frame +
                                    ":exit:" + std::to_string(status) + "\n" + frame + ":n2:" + n +
                                    "\n" + frame + ":b2:" + b + "\n" + frame + ":utc:1700000000\n",
                            false};
                };
                bool failed = false, unknown = false;
                try {
                    ensure_qinggan09_phone_package(f.spec, nonce, boot, run, [&] {
                        ++fences;
                    });
                } catch (const std::exception &error) {
                    failed = true;
                    unknown = std::string_view(error.what())
                                      .starts_with("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN:");
                }

                check(unknown ==
                              (mode == "timeout" || mode == "pm-failed" || mode == "bad-readback"),
                      "replacement uncertainty classification");

                if (mode == "timeout") {
                    // Exercise the same state guard and actual installer as Session: even a
                    // desired-SHA readback cannot erase the first uncertain configure.
                    installed = expected_previous;
                    installs = 0;
                    std::string retained_boot;
                    bool quarantined = false;
                    std::int64_t provider_pid = 123;

                    for (unsigned attempt = 0; attempt < 2; ++attempt) {
                        bool denied = false;
                        try {
                            qinggan09_phone_package_exchange(
                                    boot, retained_boot, quarantined, provider_pid, [&] {
                                        return ensure_qinggan09_phone_package(f.spec, nonce, boot,
                                                                              run, [&] {
                                                                                  ++fences;
                                                                              });
                                    });
                        } catch (const std::exception &error) {
                            denied = std::string_view(error.what())
                                             .starts_with("PHONE_DELIVERY_UNKNOWN:");
                        }

                        check(denied, "uncertain configure was retried or relabelled successful");
                    }

                    check(installs == 1 && retained_boot == boot && quarantined &&
                                  provider_pid == 0,
                          "two configure calls must dispatch one quarantined replacement");
                }

                check(failed != (mode == "update" || mode == "current" ||
                                 mode == "desired-during-push"),
                      "update outcome");

                if (mode == "unknown" || mode == "missing" || mode == "other-known-generation") {
                    check(mutations == 0 && installs == 0,
                          "user-modified, wrong generation or missing OEM Phone mutated");
                }

                if (mode == "changed-during-push" || mode == "desired-during-push") {
                    check(installs == 0 && !created && !pushed,
                          "changed application overwritten or staging leaked");
                }

                if (mode == "update") {
                    check(installs == 1 && !created && !pushed, "exact one clean update");
                }

                if (mode == "current") {
                    check(mutations == 0, "current APK rewritten");
                }

                check(fences >= 2 * commands, "owner fence missing");
                check(cache_file_identity(f.data) == identity && sha256_file(f.data) == data_sha,
                      "userdata changed");
                ++cases;
            }
        }

        f.spec["qinggan09_phone_package"] = f.descriptor;
        auto reject_spec = [&](const Json &spec) {
            bool rejected = false, reached = false;
            try {
                ensure_qinggan09_phone_package(
                        spec, nonce, boot,
                        [&](const auto &, int) -> ProcessResult {
                            reached = true;
                            throw std::runtime_error("transport reached");
                        },
                        [] {});
            } catch (...) {
                rejected = true;
            }

            check(rejected && !reached, "unsafe descriptor reached guest");
            ++cases;
        };
        bad = f.spec;
        bad["api"] = 30;
        reject_spec(bad);
        bad = f.spec;
        bad["api"] = 28.5;
        reject_spec(bad);
        bad = f.spec;
        bad["profile"] = "other";
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_phone_package"]["expected_previous_sha256"] = std::string(64, 'a');
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_phone_package"]["package"]["package"] = "other";
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_phone_package"] = chained;
        bad["qinggan09_phone_package"]["expected_previous_sha256"] = "INVALID";
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_phone_package"] = chained;
        bad["qinggan09_phone_package"]["predecessor_receipt_sha256"] = "INVALID";
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_phone_package"] = chained;
        bad["qinggan09_phone_package"].erase("predecessor_receipt_sha256");
        reject_spec(bad);

        for (const char *pin : {"expected_previous_sha256", "predecessor_receipt_sha256"}) {
            for (const std::string invalid :
                 {std::string(63, 'a'), std::string(65, 'a'), std::string(64, 'A')}) {
                bad = f.spec;
                bad["qinggan09_phone_package"] = chained;
                bad["qinggan09_phone_package"][pin] = invalid;
                reject_spec(bad);
            }
        }

        bad = f.spec;
        bad["qinggan09_phone_package"] = chained;
        bad["qinggan09_phone_package"]["schema"] = 2;
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_phone_package"] = chained;
        bad["qinggan09_phone_package"]["extra"] = true;
        reject_spec(bad);
        std::ofstream(f.file, std::ios::trunc) << "tampered APK";
        reject_spec(f.spec);
        std::cout << "PASS " << cases
                  << " finite Phone descriptor/guarded API28 update cases; userdata preserved\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
