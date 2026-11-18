#include "qinggan09_owned_packages.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
void check(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

struct Fixture {
    std::filesystem::path root, machine, data;
    std::array<std::string, 2> bytes = {"audio APK fixture", "bootstrap APK fixture"};
    Json packages = Json::array(), descriptor, spec;

    Fixture() {
        root = std::filesystem::current_path() / ".local/build/tmp" /
               ("qinggan09-packages-" + random_token());
        machine = root / "android";
        ensure_private_directory(machine / "packages");

        for (unsigned i = 0; i < 2; ++i) {
            const auto sha = sha256_bytes(bytes[i]);
            const auto file = machine / "packages" / (sha + ".apk");
            std::ofstream(file) << bytes[i];
            packages.push_back({{"package", std::string(qinggan09_owned_package_names[i])},
                                {"file", file.string()},
                                {"sha256", sha},
                                {"bytes", bytes[i].size()}});
        }

        descriptor = {
                {"schema", 1},
                {"profile", std::string(qinggan09_owned_packages_profile)},
                {"api", 28},
                {"certificate_sha256", std::string(qinggan09_owned_packages_certificate_sha256)},
                {"receipt_sha256", std::string(64, 'b')},
                {"packages", packages}};
        spec = {{"profile", std::string(qinggan09_owned_packages_profile)},
                {"api", 28},
                {"machine", machine.string()},
                {"qinggan09_owned_packages", descriptor}};
        data = root / "userdata";
        std::ofstream(data) << "existing applications and preferences";
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
} // namespace

int main() {
    try {
        {
            Fixture copy;
            copy.spec["profile"] = "independent-local-copy";
            bool refused = false;
            try {
                (void)qinggan09_owned_packages_files(copy.spec);
            } catch (...) {
                refused = true;
            }

            check(refused, "logical copy needs embedded adapter identity");
            copy.spec["reference_profile"] = std::string(qinggan09_owned_packages_profile);
            check(qinggan09_owned_packages_files(copy.spec).size() == 2,
                  "independent logical copy retains A9 packages");
            copy.spec["reference_profile"] = "unsupported-family";
            refused = false;
            try {
                (void)qinggan09_owned_packages_files(copy.spec);
            } catch (...) {
                refused = true;
            }

            check(refused, "foreign reference cannot inherit A9 packages");
        }

        {
            Fixture full;
            Json result = {{"schema", 1},
                           {"operation", "build-artifacts"},
                           {"profile", std::string(qinggan09_owned_packages_profile)},
                           {"artifacts_built", true},
                           {"guest_started", false},
                           {"machine_ready", false},
                           {"artifacts", Json::object()}};
            Json installed = Json::array();
            std::array<std::string, 2> certs = {
                    std::string(qinggan09_owned_packages_certificate_sha256),
                    std::string(qinggan09_owned_packages_certificate_sha256)};

            for (unsigned i = 0; i < 2; ++i) {
                const std::string ref = i ? "component:bootstrap" : "component:audiopolicy";
                auto artifact = full.packages.at(i);
                artifact.erase("package");
                result["artifacts"][ref] = artifact;
                installed.push_back(
                        {{"artifact", ref},
                         {"package", std::string(qinggan09_owned_package_names[i])},
                         {"sha256", artifact.at("sha256")},
                         {"mutable", false},
                         {"target", "/data/app/" + std::string(qinggan09_owned_package_names[i]) +
                                            "-fixture/base.apk"}});
            }

            const auto value = qinggan09_owned_packages_artifacts_candidate(result.dump(),
                                                                            installed, certs);
            {
                auto copied = result;
                copied["profile"] = "independent-local-copy";
                const auto cloned = qinggan09_owned_packages_artifacts_candidate(
                        copied.dump(), installed, certs, "independent-local-copy");
                check(cloned.at("profile") == qinggan09_owned_packages_profile &&
                              cloned.at("receipt_sha256") == sha256_bytes(copied.dump()),
                      "logical artifact provenance and OEM descriptor identity remain distinct");
                bool refused = false;
                try {
                    (void)qinggan09_owned_packages_artifacts_candidate(copied.dump(), installed,
                                                                       certs);
                } catch (...) {
                    refused = true;
                }

                check(refused, "foreign logical artifact receipt cannot match canonical request");
            }

            check(value.at("packages") == full.packages &&
                          value.at("receipt_sha256") == sha256_bytes(result.dump()),
                  "full source origin lost actual result identity");
            {
                auto triple = result;
                auto triple_installed = installed;
                const std::string bytes = "full CanBus APK fixture";
                const auto sha = sha256_bytes(bytes);
                const auto file = full.machine / "packages" / (sha + ".apk");
                std::ofstream(file) << bytes;
                triple["artifacts"]["component:canbus"] = {
                        {"file", file.string()}, {"sha256", sha}, {"bytes", bytes.size()}};
                triple_installed.push_back({{"artifact", "component:canbus"},
                                            {"package", "com.qinggan.canbus.service"},
                                            {"sha256", sha},
                                            {"mutable", false},
                                            {"target", std::string(qinggan09_canbus_system_apk)}});
                const std::array<std::string, 3> triple_certs = {certs[0], certs[1], certs[0]};
                const auto refreshed = qinggan09_owned_packages_artifacts_candidate(
                        triple.dump(), triple_installed, triple_certs);
                check(refreshed.at("schema") == 1 &&
                              refreshed.at("canbus").at("package").at("sha256") == sha &&
                              refreshed.at("canbus").at("expected_previous_sha256") == sha &&
                              refreshed.at("canbus").at("predecessor_receipt_sha256") ==
                                      sha256_bytes(triple.dump()),
                      "full CanBus origin does not bind installed package and current receipt");
                auto obsolete = triple_installed;
                obsolete[2]["target"] = "/data/app/com.qinggan.canbus.service-fixture/base.apk";
                bool rejected_obsolete = false;
                try {
                    qinggan09_owned_packages_artifacts_candidate(triple.dump(), obsolete,
                                                                 triple_certs);
                } catch (...) {
                    rejected_obsolete = true;
                }

                check(rejected_obsolete, "full CanBus origin accepted obsolete data APK");
                auto cert_bad = triple_certs;
                cert_bad[2] = std::string(64, '0');
                bool denied = false;
                try {
                    qinggan09_owned_packages_artifacts_candidate(triple.dump(), triple_installed,
                                                                 cert_bad);
                } catch (...) {
                    denied = true;
                }

                check(denied, "full CanBus origin accepted unverified third signer");
                auto missing = triple_installed;
                missing.erase(2);
                denied = false;
                try {
                    qinggan09_owned_packages_artifacts_candidate(triple.dump(), missing,
                                                                 triple_certs);
                } catch (...) {
                    denied = true;
                }

                check(denied, "full CanBus origin accepted missing actual guest installation");
                auto foreign = triple_installed;
                foreign[2]["package"] = "foreign.package";
                denied = false;
                try {
                    qinggan09_owned_packages_artifacts_candidate(triple.dump(), foreign,
                                                                 triple_certs);
                } catch (...) {
                    denied = true;
                }

                check(denied, "full CanBus origin accepted foreign third installation");
            }
            unsigned rejected = 0;
            const auto reject = [&](const Json &r, const Json &pm, const auto &c) {
                bool denied = false;
                try {
                    (void)qinggan09_owned_packages_artifacts_candidate(r.dump(), pm, c);
                } catch (...) {
                    denied = true;
                }

                check(denied, "invalid full source origin accepted");
                ++rejected;
            };
            auto bad = result;
            bad["operation"] = "own-component-refresh";
            reject(bad, installed, certs);

            for (const auto *key : {"guest_started", "machine_ready"}) {
                bad = result;
                bad[key] = true;
                reject(bad, installed, certs);
            }

            bad = result;
            bad["schema"] = 2;
            reject(bad, installed, certs);
            bad = result;
            bad["artifacts"].erase("component:bootstrap");
            reject(bad, installed, certs);
            auto pm = installed;
            pm.erase(0);
            reject(result, pm, certs);
            pm = installed;
            pm.push_back(pm.at(0));
            reject(result, pm, certs);

            for (const auto *key : {"package", "sha256", "target"}) {
                pm = installed;
                pm[0][key] = "incorrect";
                reject(result, pm, certs);
            }

            pm = installed;
            pm[0]["mutable"] = true;
            reject(result, pm, certs);
            auto cert_bad = certs;
            cert_bad[1] = std::string(64, '0');
            reject(result, installed, cert_bad);
            check(rejected == 12, "full source negative count differs");
            std::cout << "A9 actual full-artifacts origin positive plus " << rejected
                      << " negatives passed\n";
        }

        {
            Fixture owned;
            const std::string canbus_bytes = "CanBus APK fixture";
            const auto canbus_sha = sha256_bytes(canbus_bytes);
            const auto canbus_file = owned.machine / "packages" / (canbus_sha + ".apk");
            std::ofstream(canbus_file) << canbus_bytes;
            const Json package = {{"package", "com.qinggan.canbus.service"},
                                  {"file", canbus_file.string()},
                                  {"sha256", canbus_sha},
                                  {"bytes", canbus_bytes.size()}};
            auto descriptor = owned.descriptor;
            descriptor["canbus"] = {{"package", package},
                                    {"expected_previous_sha256", std::string(64, 'c')},
                                    {"predecessor_receipt_sha256", std::string(64, 'd')}};
            qinggan09_owned_packages_descriptor(descriptor, false);
            check(qinggan09_owned_packages_payloads(owned.descriptor) == owned.packages,
                  "legacy payload array changed");
            auto payloads = qinggan09_owned_packages_payloads(descriptor);
            check(payloads.size() == 3 && payloads.at(0) == owned.packages.at(0) &&
                          payloads.at(1) == owned.packages.at(1) && payloads.at(2) == package,
                  "CanBus payload ordering");
            auto portable = descriptor;

            for (unsigned i = 0; i < 3; ++i) {
                auto item = payloads.at(i);
                item["file"] = "android/packages/" + item.at("sha256").get<std::string>() + ".apk";
                qinggan09_owned_packages_set_payload(portable, i, item);
            }

            qinggan09_owned_packages_descriptor(portable, true);
            check(portable.at("canbus").at("expected_previous_sha256") ==
                          descriptor.at("canbus").at("expected_previous_sha256"),
                  "payload relocation changed predecessor");
            const auto rejects = [&](const Json &value) {
                bool denied = false;
                try {
                    qinggan09_owned_packages_descriptor(value, false);
                } catch (...) {
                    denied = true;
                }

                check(denied, "invalid CanBus descriptor accepted");
            };
            auto bad = descriptor;
            bad["schema"] = 2;
            rejects(bad);
            bad = descriptor;
            bad["canbus"] = nullptr;
            rejects(bad);
            bad = descriptor;
            bad["extra"] = true;
            rejects(bad);
            bad = descriptor;
            bad["schema"] = 2.5;
            rejects(bad);

            for (const auto *field :
                 {"package", "expected_previous_sha256", "predecessor_receipt_sha256"}) {
                bad = descriptor;
                bad["canbus"].erase(field);
                rejects(bad);
            }

            bad = descriptor;
            bad["canbus"]["extra"] = true;
            rejects(bad);

            for (const auto *field : {"expected_previous_sha256", "predecessor_receipt_sha256"}) {
                for (const auto &invalid :
                     Json::array({"bad", std::string(64, 'A'), 17, nullptr})) {
                    bad = descriptor;
                    bad["canbus"][field] = invalid;
                    rejects(bad);
                }
            }

            bad = descriptor;
            bad["canbus"]["package"]["package"] = "foreign.package";
            rejects(bad);
            bad = descriptor;
            bad["canbus"]["package"]["extra"] = true;
            rejects(bad);
            bad = descriptor;
            bad["packages"].push_back(package);
            rejects(bad);
            auto spec = owned.spec;
            spec["qinggan09_owned_packages"] = descriptor;
            unsigned calls = 0, mutations = 0;
            const std::string nonce(32, 'b'), boot = "12345678-1234-1234-1234-123456789abc";
            auto run = [&](const std::vector<std::string> &args, int budget) -> ProcessResult {
                ++calls;
                check(budget > 0 && budget <= 60000, "bounded CanBus inspection");
                check(args.size() == 2 && args.at(0) == "shell",
                      "CanBus prior inspection transport");
                const auto &script = args.at(1);

                if (script.find("'pm' 'install'") != std::string::npos ||
                    script.find("'mkdir'") != std::string::npos) {
                    ++mutations;
                }

                const auto at = script.find("VOYEMU_STARTUP_");
                check(at != std::string::npos, "CanBus framed inspection");
                const auto frame = script.substr(at, 47);
                std::string body;

                if (script.find("'pm' 'path'") != std::string::npos) {
                    body = "package:/system/app/CanBus/CanBus.apk\n";
                } else if (script.find("'sha256sum'") != std::string::npos) {
                    body = std::string(64, 'e') + "  /system/app/CanBus/CanBus.apk\n";
                } else {
                    throw std::runtime_error("unexpected CanBus inspection");
                }

                return {0,
                        frame + ":b1:" + boot + "\n" + frame + ":n1:" + nonce + "\n" + frame +
                                ":body\n" + body + "\n" + frame + ":exit:0\n" + frame +
                                ":n2:" + nonce + "\n" + frame + ":b2:" + boot + "\n" + frame +
                                ":utc:1700000000\n",
                        false};
            };
            check(ensure_qinggan09_canbus_package(owned.spec, nonce, boot, run, [] {}).is_null() &&
                          calls == 0,
                  "legacy CanBus helper reached guest");
            bool denied = false;
            try {
                ensure_qinggan09_canbus_package(spec, nonce, boot, run, [] {});
            } catch (...) {
                denied = true;
            }

            check(denied && calls > 0 && mutations == 0,
                  "unknown CanBus predecessor reached guest mutation");
            std::cout
                    << "A9 optional CanBus descriptor, payload relocation and prior mutation barrier passed\n";
        }

        {
            Fixture f;
            auto value = f.descriptor;
            auto canbus = f.packages.at(0);
            canbus["package"] = "com.qinggan.canbus.service";
            auto nav = f.packages.at(1);
            nav["package"] = "com.qinggan.QGBus";
            value["canbus"] = {{"package", canbus},
                               {"expected_previous_sha256", std::string(64, 'c')},
                               {"predecessor_receipt_sha256", std::string(64, 'd')}};
            value["navigation_bus"] = {{"package", nav},
                                       {"expected_previous_sha256", std::string(64, 'e')},
                                       {"predecessor_receipt_sha256", std::string(64, 'f')}};
            qinggan09_owned_packages_descriptor(value, false);
            check(qinggan09_owned_packages_payloads(value).size() == 4,
                  "navigation bus payload missing");
            auto portable = value;
            const auto payloads = qinggan09_owned_packages_payloads(value);

            for (unsigned i = 0; i < 4; ++i) {
                auto item = payloads.at(i);
                item["file"] = "android/packages/" + item.at("sha256").get<std::string>() + ".apk";
                qinggan09_owned_packages_set_payload(portable, i, item);
            }

            qinggan09_owned_packages_descriptor(portable, true);

            for (const auto *role : {"canbus", "navigation_bus"}) {
                auto bad = value;
                bad.erase(role);
                bool denied = false;
                try {
                    qinggan09_owned_packages_descriptor(bad, false);
                } catch (...) {
                    denied = true;
                }

                // The navigation bus role extends the CanBus role and never replaces it.
                check(denied == (std::string_view(role) == "canbus"),
                      "navigation role without CanBus accepted");
                bad = value;
                bad[role]["package"]["package"] = "foreign.package";
                denied = false;
                try {
                    qinggan09_owned_packages_descriptor(bad, false);
                } catch (...) {
                    denied = true;
                }

                check(denied, "navigation foreign role accepted");
            }

            // A navigation refresh names the exact installed CanBus and navigation predecessors.
            auto candidate = value;
            candidate["receipt_sha256"] = std::string(64, '9');
            candidate["canbus"]["expected_previous_sha256"] = canbus.at("sha256");
            candidate["canbus"]["predecessor_receipt_sha256"] = std::string(64, 'c');
            candidate["navigation_bus"]["predecessor_receipt_sha256"] = std::string(64, 'd');
            qinggan09_owned_packages_descriptor(candidate, false);

            Json full = {{"schema", 1},
                         {"operation", "build-artifacts"},
                         {"profile", std::string(qinggan09_owned_packages_profile)},
                         {"revision", std::string(64, 'f')},
                         {"artifacts_built", true},
                         {"guest_started", false},
                         {"machine_ready", false},
                         {"artifacts", Json::object()}};
            Json installed = Json::array();
            const std::array<std::string, 4> refs = {"component:audiopolicy", "component:bootstrap",
                                                     "component:canbus",
                                                     "component:navigation-bus"};
            std::array<std::string, 4> certs;

            for (unsigned i = 0; i < 4; ++i) {
                auto item = payloads.at(i);
                const auto name = item.at("package").get<std::string>();
                item.erase("package");
                full["artifacts"][refs[i]] = item;
                installed.push_back(
                        {{"artifact", refs[i]},
                         {"package", name},
                         {"sha256", item.at("sha256")},
                         {"mutable", false},
                         {"target", i == 2 ? std::string(qinggan09_canbus_system_apk)
                                           : "/data/app/" + name + "-fixture/base.apk"}});
                certs[i] = std::string(qinggan09_owned_packages_certificate_sha256);
            }

            const auto origin = qinggan09_owned_packages_artifacts_candidate(full.dump(), installed,
                                                                             certs);
            check(origin.at("schema") == 1 &&
                          origin.at("navigation_bus").at("expected_previous_sha256") ==
                                  nav.at("sha256"),
                  "full navigation origin missing");
            auto accepted_canbus = value;
            accepted_canbus.erase("navigation_bus");
            qinggan09_owned_packages_descriptor(accepted_canbus, false);
            auto navigation_candidate = candidate;
            check(navigation_candidate.at("canbus").at("expected_previous_sha256") !=
                          accepted_canbus.at("canbus").at("expected_previous_sha256"),
                  "fixture does not distinguish desired CanBus from installed predecessor");
            auto spec = f.spec;
            spec["qinggan09_owned_packages"] = navigation_candidate;
            const std::string nonce(32, 'b'), boot = "12345678-1234-1234-1234-123456789abc";
            unsigned mutations = 0, reads = 0;
            std::string path, mode = "unknown-nav";
            auto run = [&](const std::vector<std::string> &args, int budget) -> ProcessResult {
                check(budget > 0 && budget <= 60000, "navigation bounded preflight");
                check(args.size() == 2 && args[0] == "shell", "navigation read-only transport");
                const auto &script = args[1];

                if (script.find("'mkdir'") != std::string::npos ||
                    script.find("'install'") != std::string::npos) {
                    ++mutations;
                }

                const auto at = script.find("VOYEMU_STARTUP_");
                check(at != std::string::npos, "navigation framed preflight");
                const auto frame = script.substr(at, 47);
                std::string body;

                if (script.find("'pm' 'path'") != std::string::npos) {
                    path = script.find("com.qinggan.QGBus") != std::string::npos
                                   ? "/data/app/com.qinggan.QGBus-fixture/base.apk"
                                   : "/data/app/com.qinggan.canbus.service-fixture/base.apk";
                    body = "package:" + path + "\n";
                } else if (script.find("'sha256sum'") != std::string::npos) {
                    const bool navigation = path.find("QGBus") != std::string::npos;
                    const auto sha =
                            mode == "current"
                                    ? (navigation ? nav : canbus).at("sha256").get<std::string>()
                            : (mode == "unknown-nav" && navigation) ||
                                            (mode == "unknown-canbus" && !navigation)
                                    ? std::string(64, '0')
                                    : std::string(64, navigation ? 'e' : 'c');
                    body = sha + "  " + path + "\n";
                } else {
                    throw std::runtime_error("navigation mutation before predecessor accepted");
                }

                ++reads;

                return {0,
                        frame + ":b1:" + boot + "\n" + frame + ":n1:" + nonce + "\n" + frame +
                                ":body\n" + body + "\n" + frame + ":exit:0\n" + frame +
                                ":n2:" + nonce + "\n" + frame + ":b2:" + boot + "\n" + frame +
                                ":utc:1700000000\n",
                        false};
            };
            mode = "baseline";
            reads = 0;
            bool rebased_denied = false;
            try {
                qinggan09_owned_packages_preflight_roles(spec, nonce, boot, run, [] {});
            } catch (...) {
                rebased_denied = true;
            }

            check(rebased_denied && reads == 2 && mutations == 0,
                  "rebased CanBus lineage did not reproduce baseline rejection");
            const auto candidate_canbus_file =
                    navigation_candidate.at("canbus").at("package").at("file");
            auto relocated_previous = accepted_canbus;
            relocated_previous["canbus"]["package"]["file"] = "/verified/previous-canbus.apk";
            qinggan09_owned_packages_preserve_canbus_role(navigation_candidate, relocated_previous);
            check(navigation_candidate.at("canbus") == accepted_canbus.at("canbus") &&
                          navigation_candidate.at("canbus").at("package").at("file") ==
                                  candidate_canbus_file &&
                          navigation_candidate.at("navigation_bus") ==
                                  candidate.at("navigation_bus"),
                  "production helper did not preserve CanBus lineage and candidate file independently");

            for (const auto *field : {"package", "sha256", "bytes"}) {
                auto changed = candidate;
                changed["canbus"]["package"][field] =
                        std::string_view(field) == "bytes"
                                ? Json(1)
                                : Json(std::string_view(field) == "sha256" ? std::string(64, '0')
                                                                           : "foreign.package");
                const auto before = changed;
                bool denied = false;
                try {
                    qinggan09_owned_packages_preserve_canbus_role(changed, relocated_previous);
                } catch (...) {
                    denied = true;
                }

                check(denied && changed == before,
                      "production helper accepted changed CanBus desired payload or partially mutated rejection");
            }

            spec["qinggan09_owned_packages"] = navigation_candidate;

            for (const auto *invalid : {"unknown-nav", "unknown-canbus"}) {
                mode = invalid;

                for (unsigned helper = 0; helper < 3; ++helper) {
                    bool denied = false;
                    reads = 0;
                    try {
                        if (helper == 0) {
                            ensure_qinggan09_canbus_package(spec, nonce, boot, run, [] {});
                        } else if (helper == 1) {
                            ensure_qinggan09_navigation_bus_package(spec, nonce, boot, run, [] {});
                        } else {
                            ensure_qinggan09_owned_packages(spec, nonce, boot, run, [] {});
                        }
                    } catch (...) {
                        denied = true;
                    }

                    check(denied && reads == (mode == "unknown-nav" ? 4 : 2) && mutations == 0,
                          "unknown role predecessor reached any package mutation");
                }
            }

            for (const auto *accepted : {"baseline", "current"}) {
                mode = accepted;
                reads = 0;
                qinggan09_owned_packages_preflight_roles(spec, nonce, boot, run, [] {});
                check(reads == 4 && mutations == 0, "known role predecessor denied");
            }

            // Fixture bytes model the accepted OEM predecessor -> desired package transition.
            std::string installed_canbus(64, 'c'), selected_path, directory, staged;
            unsigned installs = 0, pushes = 0, installer_mutations = 0;
            bool created = false;
            auto install_run = [&](const std::vector<std::string> &args,
                                   int budget) -> ProcessResult {
                check(budget > 0 && budget <= 60000, "navigation bounded actual installer");

                if (args.at(0) == "push") {
                    check(created && args.size() == 3 && args.at(2) == directory + "/provider.apk",
                          "navigation confined payload push");
                    staged = sha256_file(args.at(1));
                    ++pushes;

                    return {0, "pushed", false};
                }

                check(args.size() == 2 && args.at(0) == "shell",
                      "navigation framed installer transport");
                const auto &script = args.at(1);
                const auto at = script.find("VOYEMU_STARTUP_");
                check(at != std::string::npos, "navigation installer frame");
                const auto frame = script.substr(at, 47);
                std::string body;

                if (script.find("'pm' 'path'") != std::string::npos) {
                    selected_path =
                            script.find("com.qinggan.QGBus") != std::string::npos
                                    ? "/data/app/com.qinggan.QGBus-fixture/base.apk"
                                    : "/data/app/com.qinggan.canbus.service-fixture/base.apk";
                    body = "package:" + selected_path + "\n";
                } else if (script.find("'sha256sum'") != std::string::npos) {
                    const bool staging = script.find("/provider.apk") != std::string::npos;
                    const auto sha = staging ? staged
                                     : selected_path.find("QGBus") != std::string::npos
                                             ? std::string(64, 'e')
                                             : installed_canbus;
                    body = sha + "  " + (staging ? directory + "/provider.apk" : selected_path) +
                           "\n";
                } else if (script.find("'mkdir' '-m' '0700'") != std::string::npos) {
                    check(!created, "navigation staging must be exclusive");
                    const auto begin = script.find("/data/local/tmp/voyemu-owned-");
                    check(begin != std::string::npos, "navigation staging directory");
                    directory = script.substr(begin, script.find('\'', begin) - begin);
                    created = true;
                    ++installer_mutations;
                } else if (script.find("'pm' 'install'") != std::string::npos) {
                    check(created && staged == canbus.at("sha256").get<std::string>() &&
                                  script.find(
                                          "'pm' 'install' '-r' '--pkg' 'com.qinggan.canbus.service' '-S'") !=
                                          std::string::npos &&
                                  script.find(" < '" + directory + "/provider.apk'") !=
                                          std::string::npos &&
                                  script.find("'-'") == std::string::npos,
                          "navigation exact CanBus API28 stdin replacement");
                    installed_canbus = staged;
                    ++installs;
                    ++installer_mutations;
                    body = "Success\n";
                } else if (script.find("'rm' '-f'") != std::string::npos) {
                    check(created, "navigation owned staged cleanup");
                    staged.clear();
                    ++installer_mutations;
                } else if (script.find("'rmdir'") != std::string::npos) {
                    check(created && staged.empty(), "navigation cleanup ordering");
                    created = false;
                    ++installer_mutations;
                } else {
                    throw std::runtime_error("navigation unexpected installer operation");
                }

                return {0,
                        frame + ":b1:" + boot + "\n" + frame + ":n1:" + nonce + "\n" + frame +
                                ":body\n" + body + "\n" + frame + ":exit:0\n" + frame +
                                ":n2:" + nonce + "\n" + frame + ":b2:" + boot + "\n" + frame +
                                ":utc:1700000000\n",
                        false};
            };
            const auto updated = ensure_qinggan09_canbus_package(spec, nonce, boot, install_run,
                                                                 [] {});
            check(updated.at("status") == "updated" && updated.at("installed") == true &&
                          installs == 1 && pushes == 1 &&
                          installed_canbus == canbus.at("sha256").get<std::string>() && !created &&
                          staged.empty(),
                  "navigation preserved CanBus predecessor failed actual installer transition");
            const auto mutations_after_update = installer_mutations;
            const auto current = ensure_qinggan09_canbus_package(spec, nonce, boot, install_run,
                                                                 [] {});
            check(current.at("status") == "current" && current.at("installed") == false &&
                          installs == 1 && pushes == 1 &&
                          installer_mutations == mutations_after_update,
                  "navigation current CanBus was not idempotent");
            std::cout << "A9 finite navigation bus and portable role payloads passed\n";
        }

        // CanBus is a persistent system app: the running system APK is verified, never replaced.
        {
            Fixture f;
            const std::string bytes = "system CanBus APK fixture", other(64, 'd');
            const auto sha = sha256_bytes(bytes);
            std::ofstream(f.machine / "packages" / (sha + ".apk")) << bytes;
            const Json package = {{"package", "com.qinggan.canbus.service"},
                                  {"file", (f.machine / "packages" / (sha + ".apk")).string()},
                                  {"sha256", sha},
                                  {"bytes", bytes.size()}};
            auto spec = f.spec;
            spec["qinggan09_owned_packages"]["canbus"] = {
                    {"package", package},
                    {"expected_previous_sha256", sha},
                    {"predecessor_receipt_sha256", std::string(64, 'b')}};
            const std::string nonce(32, 'b'), boot = "12345678-1234-1234-1234-123456789abc";
            std::string path, digest;
            unsigned mutations = 0;
            auto run = [&](const std::vector<std::string> &args, int budget) -> ProcessResult {
                check(budget > 0 && budget <= 60000, "system CanBus bounded transport");

                if (args.at(0) == "push") {
                    ++mutations;

                    return {0, "pushed", false};
                }

                const auto &script = args.at(1);
                const auto at = script.find("VOYEMU_STARTUP_");
                check(at != std::string::npos, "system CanBus framed transport");
                const auto frame = script.substr(at, 47);
                std::string body;

                if (script.find("'pm' 'path'") != std::string::npos) {
                    body = "package:" + path + "\n";
                } else if (script.find("'sha256sum'") != std::string::npos) {
                    body = digest + "  " + path + "\n";
                } else {
                    ++mutations;
                }

                return {0,
                        frame + ":b1:" + boot + "\n" + frame + ":n1:" + nonce + "\n" + frame +
                                ":body\n" + body + "\n" + frame + ":exit:0\n" + frame +
                                ":n2:" + nonce + "\n" + frame + ":b2:" + boot + "\n" + frame +
                                ":utc:1700000000\n",
                        false};
            };
            auto ensure = [&](const Json &value) {
                return ensure_qinggan09_canbus_package(value, nonce, boot, run, [] {});
            };
            auto refused = [&](const Json &value) {
                try {
                    (void)ensure(value);
                } catch (const std::exception &) {
                    return mutations == 0;
                }

                return false;
            };

            path = std::string(qinggan09_canbus_system_apk);
            digest = sha;
            const auto current = ensure(spec);
            check(current.at("status") == "current" && current.at("installed") == false &&
                          mutations == 0,
                  "prepared system CanBus not accepted as current");
            path = "/data/app/com.qinggan.canbus.service-fixture/base.apk";
            check(ensure(spec).at("status") == "current" && mutations == 0,
                  "data CanBus of an earlier machine not accepted as current");
            path = std::string(qinggan09_canbus_system_apk);
            digest = other;
            check(refused(spec), "differing system CanBus accepted or mutated");
            // A refresh whose predecessor is the running system APK still cannot replace it.
            auto refresh = spec;
            refresh["qinggan09_owned_packages"]["canbus"]["expected_previous_sha256"] = other;
            check(refused(refresh), "persistent system CanBus replacement dispatched");
            path = "/system/priv-app/Other/Other.apk";
            digest = sha;
            check(refused(spec), "foreign system APK accepted as CanBus");
            std::cout << "A9 system CanBus verification passed\n";
        }

        Fixture f;
        unsigned cases = 0;
        const auto data_identity = cache_file_identity(f.data);
        const auto data_sha = sha256_file(f.data);
        const std::string nonce(32, 'b'), boot = "12345678-1234-1234-1234-123456789abc";

        for (const std::string mode : {"update", "current", "missing", "pm-failed", "bad-readback",
                                       "wrong-nonce", "new-boot", "timeout"}) {
            std::array<std::string, 2> installed = {std::string(64, '0'), std::string(64, '0')};

            if (mode == "current") {
                for (unsigned i = 0; i < 2; ++i) {
                    installed[i] = f.packages.at(i).at("sha256").get<std::string>();
                }
            }

            bool absent = mode == "missing", created = false, pushed = false;
            unsigned selected = 0, installs = 0, commands = 0, fences = 0;
            std::string directory, staged;
            auto run = [&](const std::vector<std::string> &args, int budget) -> ProcessResult {
                ++commands;
                check(budget > 0 && budget <= 60000, "bounded installer");

                for (const auto &arg : args) {
                    check(arg != "uninstall" && arg != "clear", "destructive operation");
                }

                if (args.at(0) == "push") {
                    check(created && args.size() == 3 && args.at(2) == directory + "/provider.apk",
                          "confined push");
                    staged = std::filesystem::path(args.at(1)).stem().string();
                    pushed = true;

                    return {0, "pushed", false};
                }

                check(args.size() == 2 && args.at(0) == "shell", "framed transport");
                const auto &script = args.at(1);
                const auto at = script.find("VOYEMU_STARTUP_");
                check(at != std::string::npos, "missing frame");
                const auto frame = script.substr(at, 47);
                std::string body;
                int status = 0;

                if (script.find("'pm' 'path'") != std::string::npos) {
                    selected = script.find(std::string(qinggan09_owned_package_names[0])) !=
                                               std::string::npos
                                       ? 0
                                       : 1;

                    if (absent) {
                        status = 1;
                    } else {
                        body = "package:/data/app/~~owned/" +
                               std::string(qinggan09_owned_package_names[selected]) +
                               "-test==/base.apk\n";
                    }
                } else if (script.find("'sha256sum'") != std::string::npos) {
                    const bool staging = script.find("/provider.apk") != std::string::npos;
                    body = (staging ? staged : installed[selected]) + "  " +
                           (staging ? directory + "/provider.apk" : "/data/app/~~owned/base.apk") +
                           "\n";
                } else if (script.find("'mkdir' '-m' '0700'") != std::string::npos) {
                    check(!created, "foreign staging");
                    const auto begin = script.find("/data/local/tmp/voyemu-owned-");
                    directory = script.substr(begin, script.find('\'', begin) - begin);
                    check(directory.starts_with("/data/local/tmp/voyemu-owned-" + nonce + "-"),
                          "nonce staging");
                    created = true;
                } else if (script.find("'pm' 'install'") != std::string::npos) {
                    check(created && pushed &&
                                  script.find("'--pkg' '" +
                                              std::string(qinggan09_owned_package_names[selected]) +
                                              "'") != std::string::npos,
                          "exact package replacement");
                    ++installs;

                    if (mode == "timeout") {
                        installed[selected] = staged;

                        return {0, "", true};
                    }

                    if (mode == "pm-failed") {
                        status = 1;
                        body = "Failure [INSTALL_FAILED_INVALID_APK]";
                    } else {
                        installed[selected] = mode == "bad-readback" ? std::string(64, 'c')
                                                                     : staged;
                        absent = false;
                        body = "Success\n";
                    }
                } else if (script.find("'rm' '-f'") != std::string::npos) {
                    check(created, "foreign cleanup");
                    pushed = false;
                } else if (script.find("'rmdir'") != std::string::npos) {
                    check(created && !pushed, "cleanup order");
                    created = false;
                } else {
                    throw std::runtime_error("unexpected installer command");
                }

                const auto n = mode == "wrong-nonce" ? std::string(32, 'a') : nonce;
                const auto b = mode == "new-boot" ? "12345678-1234-1234-1234-123456789abd" : boot;

                return {0,
                        frame + ":b1:" + b + "\n" + frame + ":n1:" + n + "\n" + frame + ":body\n" +
                                body + "\n" + frame + ":exit:" + std::to_string(status) + "\n" +
                                frame + ":n2:" + n + "\n" + frame + ":b2:" + b + "\n" + frame +
                                ":utc:1700000000\n",
                        false};
            };
            bool failed = false;
            Json result;
            try {
                result = ensure_qinggan09_owned_packages(f.spec, nonce, boot, run, [&] {
                    ++fences;
                });
            } catch (...) {
                failed = true;
            }

            const bool success = mode == "update" || mode == "current" || mode == "missing";
            check(failed != success, "installer outcome");

            if (success) {
                check(result.size() == 2, "two package outcomes");
                check(installs == (mode == "current" ? 0 : 2), "idempotent pair");
                check(!created && !pushed, "staging leaked");
            }

            check(fences >= commands * 2, "missing owner fences");
            check(cache_file_identity(f.data) == data_identity && sha256_file(f.data) == data_sha,
                  "userdata changed");
            ++cases;
        }

        auto reject_spec = [&](const Json &spec) {
            bool denied = false, reached = false;
            try {
                ensure_qinggan09_owned_packages(
                        spec, nonce, boot,
                        [&](const auto &, int) -> ProcessResult {
                            reached = true;
                            throw std::runtime_error("transport reached");
                        },
                        [] {});
            } catch (...) {
                denied = true;
            }

            check(denied && !reached, "unsafe pair reached guest");
            ++cases;
        };
        auto bad = f.spec;
        bad["api"] = 30;
        reject_spec(bad);
        bad = f.spec;
        bad["api"] = 28.5;
        reject_spec(bad);
        bad = f.spec;
        bad["profile"] = "other";
        reject_spec(bad);

        for (const auto *field : {"schema", "api"}) {
            bad = f.spec;
            bad["qinggan09_owned_packages"][field] = field == std::string("api") ? 28.5 : 1.5;
            reject_spec(bad);
        }

        for (const auto *field : {"certificate_sha256", "receipt_sha256"}) {
            bad = f.spec;
            bad["qinggan09_owned_packages"][field] = "bad";
            reject_spec(bad);
        }

        bad = f.spec;
        bad["qinggan09_owned_packages"]["extra"] = true;
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_owned_packages"]["packages"][1]["package"] = "foreign.package";
        reject_spec(bad);
        bad = f.spec;
        bad["qinggan09_owned_packages"]["packages"][1]["bytes"] = (16ULL << 20) + 1;
        reject_spec(bad);
        const auto second = std::filesystem::path(f.packages.at(1).at("file").get<std::string>());
        std::ofstream(second, std::ios::trunc) << std::string(f.bytes[1].size(), 'X');
        reject_spec(f.spec);
        std::ofstream(second, std::ios::trunc) << f.bytes[1];
        std::filesystem::create_hard_link(second, f.root / "alias");
        reject_spec(f.spec);
        std::filesystem::remove(f.root / "alias");
        std::filesystem::remove(second);
        reject_spec(f.spec);
        std::ofstream(second) << f.bytes[1];
        auto portable = f.descriptor;

        for (auto &p : portable["packages"]) {
            p["file"] = "android/packages/" + p.at("sha256").get<std::string>() + ".apk";
        }

        qinggan09_owned_packages_descriptor(portable, true);
        std::cout
                << "A9 two-package prevalidation, bounded fenced install, outcome uncertainty, strict descriptor and userdata preservation: "
                << cases << " cases\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
