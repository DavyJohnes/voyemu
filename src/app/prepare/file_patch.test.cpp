#include "file_patch.hpp"
#include "init_patch.hpp"
#include "model.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "selinux_patch.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
using namespace voyemu;

constexpr ProfileScope qinggan09_scope{ProfilePlatform::qinggan09, ProfileBoard::sa8155},
        nxp_scope{ProfilePlatform::qinggan09, ProfileBoard::nxp},
        qinggan11_scope{ProfilePlatform::qinggan11, ProfileBoard::sa8155},
        passion_scope{ProfilePlatform::passion, ProfileBoard::sa8155};

static void check(bool ok, const char *text) {
    if (!ok) {
        throw std::runtime_error(text);
    }
}

int main(int argc, char **argv) {
    try {
        if (argc == 7 && (std::string_view(argv[1]) == "--api30-early-policy" ||
                          std::string_view(argv[1]) == "--api30-early-policy-611")) {
            const bool older = std::string_view(argv[1]) == "--api30-early-policy-611";
            const auto cil = std::filesystem::canonical(argv[2]),
                       mapping = std::filesystem::canonical(argv[3]),
                       hash = std::filesystem::canonical(argv[4]),
                       system_ext = std::filesystem::canonical(argv[5]),
                       vendor = std::filesystem::canonical(argv[6]);
            const auto recipes = older ? api30_file_recipes_for("qinggan11")
                                       : api30_file_recipes_for("qinggan11", true);
            const auto &policy = recipes[older ? 5 : 6];
            const auto &hash_spec = recipes.back();
            check(policy.id == "can-early-policy-api30" && hash_spec.id == "selinux-hash-api30",
                  "registered early policy dependency order required");
            PinnedFilePatch early{std::string(policy.id), 30, cil, std::string(policy.input_sha256),
                                  std::string(policy.output_sha256)};
            early.profile = "test-qinggan11";
            early.scope = qinggan11_scope;
            // Only the original_navi feature adds the su read of mac_perms_file.
            early.original_navi = !older;
            early.donor = PinnedFileDependency{system_ext, sha256_file(system_ext)};
            early.vendor_donor = PinnedFileDependency{vendor, sha256_file(vendor)};
            const auto policy_bytes =
                    prepare_file_patches(std::vector<PinnedFilePatch>{early}).front();
            check(policy_bytes.find("(allow su adbd (unix_stream_socket (accept)))\n") !=
                                  std::string::npos &&
                          policy_bytes.find(
                                  "(allow su can_simulator_device (chr_file (getattr)))\n") !=
                                  std::string::npos &&
                          (policy_bytes.ends_with(
                                   "(allow su mac_perms_file (file (getattr open read)))\n") !=
                           older) &&
                          policy_bytes.find("(typepermissive su)") == std::string::npos,
                  "rooted adbd may accept on its inherited Unix socket with su still enforcing");
            check(sha256_bytes(policy_bytes) ==
                                  (older ? "50e7563396d73b053a36a9022d31807121731ecc0ce51b695df65ec7d6838db6"
                                         : "e8f3b8cb54b3ec09280cf5c630f495b3202f1c9994e796c5a1cf707a5dcd6cf6") &&
                          sha256_file(mapping) ==
                                  "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60",
                  "early policy and unchanged mapping bytes remain exact");
            std::ifstream input(mapping, std::ios::binary);
            const std::string mapping_bytes{std::istreambuf_iterator<char>(input), {}};
            const auto combined = sha256_bytes(policy_bytes + mapping_bytes),
                       expected_file = combined + "\n";
            check(combined == (older ? "38be4a4cdae3f201ae05de187700538cb460ff7958025ac1066208018c388387"
                                     : "8b7d6819e4429cec7a98a86d31366d6b7fc75ae03033d418b15f84ec1f9161c3") &&
                          sha256_bytes(expected_file) ==
                                  (older ? "2f158789774a59073cf85bf2c91272416b0da948eea037f9e13586187338f2c9"
                                         : "7293ab8b5ff6f3890a7f60b158e6bf28c882896c755edf1e996c9231519c37de"),
                  "independent combined digest and hash-file SHA are distinct");
            PinnedFilePatch hashed{
                    std::string(hash_spec.id),
                    30,
                    hash,
                    std::string(hash_spec.input_sha256),
                    std::string(hash_spec.output_sha256),
                    PinnedFileDependency{
                            mapping,
                            "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60"},
                    std::string(policy.id),
                    std::string(policy.output_sha256)};
            hashed.profile = early.profile;
            hashed.scope = early.scope;
            hashed.original_navi = early.original_navi;
            const std::vector<PinnedFilePatch> nodes{early, hashed};
            const auto output = prepare_file_patches(nodes);
            check(output.size() == 2 && output[1] == expected_file,
                  "registered early graph publishes exact derived hash-file bytes");

            if (!older) {
                auto observed = nodes;
                observed[0].ui_observer_root_accessibility = true;
                observed[0].output_sha256 = qinggan11_ui_observer_policy_sha256;
                observed[1].ui_observer_root_accessibility = true;
                observed[1].output_sha256 = qinggan11_ui_observer_policy_hash_file_sha256;
                observed[1].dependency_sha256 = qinggan11_ui_observer_policy_sha256;
                const auto output_observer = prepare_file_patches(observed);
                check(output_observer[0] ==
                                      policy_bytes +
                                              "(allow su accessibility_service (service_manager (find)))\n" &&
                              output_observer[1] ==
                                      std::string(qinggan11_ui_observer_policy_mapping_sha256) +
                                              "\n",
                      "rooted observer adds only one service lookup and its derived digest");

                for (unsigned variant = 0; variant < 5; ++variant) {
                    auto invalid = observed;

                    if (variant == 0) {
                        invalid[0].original_navi = false;
                    } else if (variant == 1) {
                        invalid[0].scope = passion_scope;
                    } else if (variant == 2) {
                        invalid[1].donor.value().sha256 = std::string(64, '0');
                    } else if (variant == 3) {
                        invalid[0].api = 28;
                    } else {
                        invalid[0].input_sha256 = qinggan11_ui_observer_policy_sha256;
                    }

                    bool rejected = false;
                    try {
                        (void)prepare_file_patches(invalid);
                    } catch (const VoyemuError &) {
                        rejected = true;
                    }

                    check(rejected, "observer policy rejects foreign scope and changed inputs");
                }
            }

            for (unsigned variant = 0; variant < 11; ++variant) {
                auto bad = nodes;

                switch (variant) {
                case 0:
                    bad[1].output_sha256 = combined;
                    break;
                case 1:
                    bad[1].donor.reset();
                    break;
                case 2:
                    bad[1].donor.value().sha256 = std::string(64, '0');
                    break;
                case 3:
                    bad[1].dependency_sha256 = std::string(64, '0');
                    break;
                case 4:
                    bad[1].patched_dependency = "missing";
                    break;
                case 5:
                    bad[1].api = 28;
                    break;
                case 6:
                    bad[1].scope.reset();
                    break;
                case 7:
                    bad[0].scope = passion_scope;
                    break;
                case 8:
                    bad[0].vendor_donor.reset();
                    break;
                case 9:
                    bad[0].donor.value().sha256 = std::string(64, '0');
                    break;
                case 10:
                    bad[1].original_navi = older;
                    break;
                default:
                    break;
                }

                bool denied = false;
                try {
                    prepare_file_patches(bad);
                } catch (...) {
                    denied = true;
                }

                check(denied,
                      "early hash graph preserves profile/API/donor/dependency/input/content-SHA "
                      "boundaries");
            }

            std::cout
                    << "early policy, unchanged mapping, exact hash and eleven negative cases passed\n";

            return 0;
        }

        if (argc == 5 && std::string_view(argv[1]) == "--can-simulator-policy") {
            const auto source = std::filesystem::canonical(argv[2]),
                       plat = std::filesystem::canonical(argv[3]),
                       vendor = std::filesystem::canonical(argv[4]);
            const std::string
                    input = "a735efbbf4f1aea52e43fb57c31974fe4a2deed52ef91e5da276f67c60d0a711",
                    plat_pin = "0306472bcb8a95ffe97e52b83089056cf0b911541717ca1f47ff062eb7c93338",
                    vendor_pin = "47ae473c9702ad2f64d156e98d8400f2328bcfba4a3a7f314e0c6bfdd379564c",
                    output = "ea017c277007b5ba8401ab214b2d607d72ddd39b7a303db05eb5281cb405fd22";
            PinnedFilePatch node{"can-simulator-policy-a9", 28, source, input, output};
            node.scope = qinggan09_scope;
            node.donor = PinnedFileDependency{plat, plat_pin};
            node.vendor_donor = PinnedFileDependency{vendor, vendor_pin};
            check(native_file_patch_steps(node.id).size() == 3 &&
                          sha256_bytes(prepare_file_patches(
                                  std::vector<PinnedFilePatch>{node})[0]) == output,
                  "pinned simulator policy node");

            for (int i = 0; i < 8; ++i) {
                auto bad = node;

                if (i == 0) {
                    bad.scope = nxp_scope;
                }

                if (i == 1) {
                    bad.api = 30;
                }

                if (i == 2) {
                    bad.input_sha256 = std::string(64, '0');
                }

                if (i == 3) {
                    bad.output_sha256 = std::string(64, '0');
                }

                if (i == 4) {
                    bad.donor.reset();
                }

                if (i == 5) {
                    bad.donor.value().sha256 = std::string(64, '0');
                }

                if (i == 6) {
                    bad.vendor_donor.reset();
                }

                if (i == 7) {
                    bad.vendor_donor.value().sha256 = std::string(64, '0');
                }

                bool denied = false;
                try {
                    prepare_file_patches(std::vector<PinnedFilePatch>{bad});
                } catch (...) {
                    denied = true;
                }

                check(denied, "simulator policy node scope");
            }

            std::cout << "simulator policy node guards passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--can-simulator-contexts") {
            const auto source = std::filesystem::canonical(argv[2]);
            const std::string
                    input = "88e8b4bf854406de5a5464ea37f0996487ae60e7e12ae1302fc9119e9b46456c",
                    output = "b140f7f850f0279e8935053b948784708c9c62de4aeb59019a7c07ef0d9f5813";
            PinnedFilePatch node{"can-simulator-contexts-a9", 28, source, input, output};
            node.scope = qinggan09_scope;
            check(native_file_patch_steps(node.id) ==
                          std::vector<std::string>{"can-simulator-contexts-a9.json"},
                  "exact simulator step required");
            check(sha256_bytes(prepare_file_patches(std::vector<PinnedFilePatch>{node})[0]) ==
                          output,
                  "native simulator output pin");

            for (int i = 0; i < 7; ++i) {
                auto bad = node;

                if (i == 0) {
                    bad.scope = nxp_scope;
                }

                if (i == 1) {
                    bad.scope.reset();
                }

                if (i == 2) {
                    bad.api = 30;
                }

                if (i == 3) {
                    bad.input_sha256 = std::string(64, '0');
                }

                if (i == 4) {
                    bad.output_sha256 = std::string(64, '0');
                }

                if (i == 5) {
                    bad.donor = PinnedFileDependency{source, input};
                }

                if (i == 6) {
                    bad.patched_dependency = "unexpected";
                }

                bool denied = false;
                try {
                    prepare_file_patches(std::vector<PinnedFilePatch>{bad});
                } catch (...) {
                    denied = true;
                }

                check(denied, "simulator node boundary");
            }

            check(sha256_file(source) == input, "simulator transform must not change input");
            std::cout << "simulator file node guards passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--codec-input") {
            const auto source = std::filesystem::canonical(argv[2]);
            const std::string
                    before = "2262964922a876a51c3b5186f094afb136ce9daae0678f9820f11e86e058e353",
                    after = "11d1e2cb76995985aa319e08fca8a6286bc7c701ecb71ed5a7822b4816bce86f";
            check(sha256_file(source) == before && std::filesystem::file_size(source) == 632488,
                  "exact extracted A9 codec input required");
            PinnedFilePatch node{"ccodec-allocation-a9", 28, source, before, after};
            node.profile = "test-qinggan09";
            node.scope = qinggan09_scope;
            const auto output = prepare_file_patches(std::vector<PinnedFilePatch>{node});
            check(output.size() == 1 && sha256_bytes(output[0]) == after,
                  "native codec node must dispatch exact guarded transform");
            std::ifstream input(source, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)), {});

            for (std::size_t i = 0; i < bytes.size(); ++i) {
                if ((i < 0x1bcf0 || i >= 0x1bcf8) && (i < 0x1646c || i >= 0x16470) &&
                    (i < 0x16474 || i >= 0x16478)) {
                    check(bytes[i] == output[0][i],
                          "codec node changed bytes outside guarded extent");
                }
            }

            for (unsigned which = 0; which < 6; ++which) {
                auto invalid = node;

                if (which == 0) {
                    invalid.api = 30;
                }

                if (which == 1) {
                    invalid.scope = passion_scope;
                }

                if (which == 2) {
                    invalid.scope.reset();
                }

                if (which == 3) {
                    invalid.output_sha256 = before;
                }

                if (which == 4) {
                    invalid.donor = PinnedFileDependency{source, before};
                }

                if (which == 5) {
                    invalid.input_sha256 = after;
                }

                bool refused = false;
                try {
                    prepare_file_patches(std::vector<PinnedFilePatch>{invalid});
                } catch (...) {
                    refused = true;
                }

                check(refused, "codec profile/API/dependency/pin boundary weakened");
            }

            const auto draft = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                               ("voyemu-codec-file-node-" + random_token());
            struct Cleanup {
                std::filesystem::path p;

                ~Cleanup() {
                    std::error_code e;
                    std::filesystem::remove_all(p, e);
                }
            } cleanup{draft};
            publish_file_patches(std::vector<PinnedFilePatch>{node}, draft);
            const auto receipt = read_private_json(draft / "receipt.json");
            check(receipt.at("files")[0].at("profile") == node.profile &&
                          sha256_file(draft / "ccodec-allocation-a9.output") == after &&
                          sha256_file(source) == before,
                  "codec publication lost source/profile/output provenance");
            std::cout
                    << "pinned codec native node, publication, guarded extents and six strict negatives passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11-731-public-library") {
            const auto source = std::filesystem::canonical(argv[2]);
            const std::string
                    pin = "34c43bf7099fc57daf2db5de07891eb20f76eafa6d9d8a4f4f01e0cefb48c070",
                    after_pin = "61307bd17de318871f19ed5effede9e65204d7c510e246d91155cc7162744763";
            check(sha256_file(source) == pin,
                  "exact native-extracted API30 platform public list required");
            std::ifstream input(source, std::ios::binary);
            const std::string baseline((std::istreambuf_iterator<char>(input)), {});
            PinnedFilePatch node{"public-qg-runtime-api30", 30, source, pin, after_pin};
            node.scope = qinggan11_scope;
            const auto output = prepare_file_patches(std::vector<PinnedFilePatch>{node});
            check(output.size() == 1 && baseline.size() == 471 && output[0].size() == 491 &&
                          output[0].substr(0, baseline.size()) == baseline &&
                          output[0].substr(baseline.size()) == "libqg_runtime.so 64\n",
                  "all existing platform bytes preserved with one own 64-bit library");
            const auto scratch =
                    std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                    ("qinggan11-731-public-library-" + random_token());
            ensure_private_directory(scratch);
            struct Cleanup {
                std::filesystem::path p;

                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(p, error);
                }
            } cleanup{scratch};

            for (unsigned mode = 0; mode < 12; ++mode) {
                auto bad = node;
                std::vector<PinnedFilePatch> nodes;

                if (mode == 0) {
                    bad.api = 28;
                }

                if (mode == 1) {
                    bad.scope = nxp_scope;
                }

                if (mode == 2) {
                    bad.scope.reset();
                }

                if (mode == 3) {
                    bad.input_sha256 = std::string(64, '0');
                }

                if (mode == 4) {
                    bad.output_sha256 = std::string(64, '0');
                }

                if (mode == 5) {
                    bad.donor = PinnedFileDependency{source, pin};
                }

                if (mode == 6) {
                    bad.patched_dependency = "unexpected";
                }

                if (mode == 7) {
                    bad.dependency_sha256 = pin;
                }

                if (mode >= 8 && mode <= 10) {
                    const std::string bytes =
                            mode == 8   ? output[0]
                            : mode == 9 ? baseline + "libunrelated_private.so 64\n"
                                        : baseline + "libqg_runtime.so 64\nlibqg_runtime.so 64\n";
                    bad.input = scratch / ("input-" + std::to_string(mode));
                    std::ofstream file(bad.input, std::ios::binary);
                    file << bytes;
                    file.close();
                    bad.input_sha256 = sha256_bytes(bytes);
                }

                nodes.push_back(bad);

                if (mode == 11) {
                    nodes.push_back(bad);
                }

                bool denied = false;
                try {
                    prepare_file_patches(nodes);
                } catch (const std::exception &) {
                    denied = true;
                }

                check(denied, "unsupported public library graph accepted");
            }

            check(sha256_file(source) == pin, "platform list input unchanged");
            std::cout
                    << "actual platform public list bytes/own64 entry and twelve negatives passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11-731-cluster-property") {
            const auto source = std::filesystem::canonical(argv[2]);
            const std::string
                    pin = "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
                    after_pin = "50c13ef43f195b1b94c5e1ed85f3a33578220e6074d9d244414ae73c9837d2db";
            check(sha256_file(source) == pin,
                  "exact native-extracted API30 platform property context required");
            std::ifstream input(source, std::ios::binary);
            const std::string baseline((std::istreambuf_iterator<char>(input)), {});
            PinnedFilePatch node{"property-contexts-api30", 30, source, pin, after_pin};
            node.scope = qinggan11_scope;
            const auto output = prepare_file_patches(std::vector<PinnedFilePatch>{node});
            check(output.size() == 1 && baseline.size() == 46954 && output[0].size() == 47071 &&
                          output[0].substr(0, baseline.size()) == baseline &&
                          output[0].substr(baseline.size()) ==
                                  "persist.clusterCardInfo u:object_r:system_prop:s0 exact string\nignoreActivation u:object_r:system_prop:s0 exact bool\n",
                  "all existing platform bytes preserved with exact cluster-card and Hiboard activation properties");
            const auto scratch =
                    std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                    ("qinggan11-731-cluster-property-" + random_token());
            ensure_private_directory(scratch);
            struct Cleanup {
                std::filesystem::path p;

                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(p, error);
                }
            } cleanup{scratch};

            for (unsigned mode = 0; mode < 16; ++mode) {
                auto bad = node;
                std::vector<PinnedFilePatch> nodes;

                if (mode == 0) {
                    bad.api = 28;
                }

                if (mode == 1) {
                    bad.scope = nxp_scope;
                }

                if (mode == 2) {
                    bad.scope.reset();
                }

                if (mode == 3) {
                    bad.input_sha256 = std::string(64, '0');
                }

                if (mode == 4) {
                    bad.output_sha256 = std::string(64, '0');
                }

                if (mode == 5) {
                    bad.donor = PinnedFileDependency{source, pin};
                }

                if (mode == 6) {
                    bad.patched_dependency = "unexpected";
                }

                if (mode == 7) {
                    bad.dependency_sha256 = pin;
                }

                if (mode >= 8 && mode <= 10) {
                    const std::string bytes =
                            mode == 8 ? output[0]
                            : mode == 9
                                    ? baseline +
                                              "persist.unrelated u:object_r:system_prop:s0 exact string\n"
                                    : baseline +
                                              "persist.clusterCardInfo u:object_r:system_prop:s0 exact string\npersist.clusterCardInfo u:object_r:system_prop:s0 exact string\n";
                    bad.input = scratch / ("input-" + std::to_string(mode));
                    std::ofstream file(bad.input, std::ios::binary);
                    file << bytes;
                    file.close();
                    bad.input_sha256 = sha256_bytes(bytes);
                }

                if (mode >= 12 && mode <= 14) {
                    const std::string bytes =
                            baseline +
                            (mode == 12
                                     ? "ignoreActivation u:object_r:system_prop:s0 exact bool\nignoreActivation u:object_r:system_prop:s0 exact bool\n"
                             : mode == 13
                                     ? "ignoreActivation u:object_r:default_prop:s0 exact bool\n"
                                     : "ignoreActivation u:object_r:system_prop:s0 exact string\n");
                    bad.input = scratch / ("input-" + std::to_string(mode));
                    std::ofstream file(bad.input, std::ios::binary);
                    file << bytes;
                    file.close();
                    bad.input_sha256 = sha256_bytes(bytes);
                }

                if (mode == 15) {
                    bad.output_sha256 =
                            "72107797ea47a13d8d3404b77c4d842390a712c81c4ceacaaeb787afba67c1b8";
                }

                nodes.push_back(bad);

                if (mode == 11) {
                    nodes.push_back(bad);
                }

                bool denied = false;
                try {
                    prepare_file_patches(nodes);
                } catch (const std::exception &) {
                    denied = true;
                }

                check(denied, "unsupported cluster/Hiboard property graph accepted");
            }

            check(sha256_file(source) == pin, "platform list input unchanged");
            std::cout
                    << "actual platform property context bytes/exact cluster-card string/Hiboard bool entries and sixteen negatives passed\n";

            return 0;
        }

        if (argc == 9 && std::string_view(argv[1]) == "--passion-api30-policy") {
            const auto cil = std::filesystem::canonical(argv[2]),
                       contexts = std::filesystem::canonical(argv[3]),
                       mapping = std::filesystem::canonical(argv[4]),
                       hash = std::filesystem::canonical(argv[5]),
                       properties = std::filesystem::canonical(argv[6]),
                       hwservices = std::filesystem::canonical(argv[7]),
                       files = std::filesystem::canonical(argv[8]);
            const std::string
                    cil_pin = "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                    patched_pin =
                            "538c1d01b76918f891eb48f7e1d901f9227276ed2ab4711212844337294fe0e9";
            std::vector<PinnedFilePatch> nodes{
                    {"passion-services", 30, cil, cil_pin, patched_pin},
                    {"passion-service-contexts", 30, contexts,
                     "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
                     "25c66b18abdb1bf102f5012387d54336c12a67c80e4ab540b90fdb863c1b82b6"},
                    {"passion-property-contexts", 30, properties,
                     "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
                     "1f322fc1fdea910bee266cc00126d2e8429b82a4a283250f12655e5d49b83d2c"},
                    {"passion-hwservice-contexts", 30, hwservices,
                     "8b4e2ffcb1a9f8e56423961a561534dd56f9f6b35d26fc0be2b8492e86797c2e",
                     "2b2ebe82d3b754dc9a00d50de124d7c07ca7c847db91e918d8e1e08778d7ce19"},
                    {"passion-file-contexts", 30, files,
                     "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c",
                     "7666f90b4e59f6744335d9ea7fccfabf512a70d0911aef9d56533f13c876811e"},
                    {"passion-policy-hash", 30, hash,
                     "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                     "aa150c95b8e4f51d3c648273f1eb36d42d2aad9f7f3860e09648633484550bda",
                     PinnedFileDependency{
                             mapping,
                             "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60"},
                     "passion-services", patched_pin}};

            for (auto &node : nodes) {
                node.scope = passion_scope;
            }

            const auto output = prepare_file_patches(nodes);
            check(output.size() == 6 &&
                          output[5] ==
                                  "9a666d2c89e4adbe1d3e46d126ec1c32be94e708150c0e7937d1395b604eaef7\n",
                  "actual API30 native graph derives correct combined hash");

            for (unsigned variant = 0; variant < 18; ++variant) {
                auto bad = nodes;

                switch (variant) {
                case 0:
                    bad[5].donor.reset();
                    break;
                case 1:
                    bad[5].donor.value().sha256 = std::string(64, '0');
                    break;
                case 2:
                    bad[5].patched_dependency = "missing";
                    break;
                case 3:
                    bad[5].dependency_sha256 = std::string(64, '0');
                    break;
                case 4:
                    bad[5].api = 28;
                    break;
                case 5:
                    bad[5].input_sha256 = std::string(64, '0');
                    break;
                case 6:
                    bad[5].output_sha256 = std::string(64, '0');
                    break;
                case 7:
                    bad[0].scope = qinggan11_scope;
                    break;
                case 8:
                    bad[1].scope.reset();
                    break;
                case 9:
                    bad[0].id = "passion-unknown";
                    break;
                case 10:
                    bad[0].api = 28;
                    break;
                case 11:
                    bad[2].scope.reset();
                    break;
                case 12:
                    bad[2].input_sha256 = std::string(64, '0');
                    break;
                case 13:
                    bad[2].output_sha256 = std::string(64, '0');
                    break;
                case 14:
                    bad[3].input_sha256 = std::string(64, '0');
                    break;
                case 15:
                    bad[3].output_sha256 = std::string(64, '0');
                    break;
                case 16:
                    bad[4].input_sha256 = std::string(64, '0');
                    break;
                case 17:
                    bad[4].output_sha256 = std::string(64, '0');
                    break;
                default:
                    break;
                }

                bool denied = false;
                try {
                    prepare_file_patches(bad);
                } catch (...) {
                    denied = true;
                }

                check(denied, "incorrect Passion native graph provenance rejected");
            }

            check(sha256_file(cil) == cil_pin && sha256_file(contexts) == nodes[1].input_sha256 &&
                          sha256_file(hash) == nodes[5].input_sha256 &&
                          sha256_file(properties) == nodes[2].input_sha256 &&
                          sha256_file(hwservices) == nodes[3].input_sha256 &&
                          sha256_file(files) == nodes[4].input_sha256,
                  "all exact policy original inputs preserved");
            std::cout
                    << "actual Passion native policy dependency/hash graph and eighteen negatives passed\n";

            return 0;
        }

        if (argc == 6 && std::string_view(argv[1]) == "--api30-policy") {
            const auto cil = std::filesystem::canonical(argv[2]),
                       contexts = std::filesystem::canonical(argv[3]),
                       mapping = std::filesystem::canonical(argv[4]),
                       hash = std::filesystem::canonical(argv[5]);
            const std::string
                    cil_pin = "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                    patched_pin =
                            "b07971ed43c0ccfb97e69af4b251f58a0e021cb93c1e68d3ccbfc664845bb55d";
            std::vector<PinnedFilePatch> nodes{
                    {"selinux-services-api30", 30, cil, cil_pin, patched_pin},
                    {"selinux-service-contexts-api30", 30, contexts,
                     "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
                     "ac366650a5d9e011d299845155d1319a1f35ec92e107cc1cded0973fa945f1b5"},
                    {"selinux-hash-api30", 30, hash,
                     "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                     "b6b92bac4f3fd25f844993319fdee6401d62ff395d9a44b3c474109d40cf5998",
                     PinnedFileDependency{
                             mapping,
                             "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60"},
                     "selinux-services-api30", patched_pin}};
            nodes[0].scope = qinggan11_scope;
            nodes[2].scope = qinggan11_scope;
            PinnedFilePatch key{"keymanager-contexts-api30", 30, contexts,
                                "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
                                "d73bcfe6f82f18f52e40e86d55f2a1163d2b526a0143bee9322d0ea5da368061"};
            key.scope = qinggan11_scope;
            const auto keyoutput = prepare_file_patches(std::vector<PinnedFilePatch>{key});
            const auto legacy_context =
                    prepare_file_patches(std::vector<PinnedFilePatch>{nodes[1]})[0];
            check(keyoutput[0] ==
                          legacy_context +
                                  "qg.key_manager u:object_r:voyemu_cockpit_service:s0\nqg.power_manager u:object_r:voyemu_cockpit_service:s0\nqg.cluster.service u:object_r:voyemu_cockpit_service:s0\nqg.system_policy u:object_r:voyemu_cockpit_service:s0\nqg.qgbus_service u:object_r:voyemu_cockpit_service:s0\nqg.carsignal u:object_r:voyemu_cockpit_service:s0\nqg.audio_policy u:object_r:voyemu_cockpit_service:s0\n",
                  "Free OEM key, power, cluster, SystemPolicy, QGBus, car signal and AudioPolicy service"
                  " contexts only; generic context preserved");

            for (unsigned mode = 0; mode < 8; ++mode) {
                auto bad = key;

                if (mode == 0) {
                    bad.scope.reset();
                }

                if (mode == 1) {
                    bad.scope = passion_scope;
                }

                if (mode == 2) {
                    bad.api = 28;
                }

                if (mode == 3) {
                    bad.input_sha256 = std::string(64, '0');
                }

                if (mode == 4) {
                    bad.output_sha256 = std::string(64, '0');
                }

                if (mode == 5) {
                    bad.donor = PinnedFileDependency{
                            mapping,
                            "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60"};
                }

                if (mode == 6) {
                    bad.patched_dependency = "selinux-services-api30";
                }

                if (mode == 7) {
                    bad.dependency_sha256 = patched_pin;
                }

                bool denied = false;
                try {
                    prepare_file_patches(std::vector<PinnedFilePatch>{bad});
                } catch (...) {
                    denied = true;
                }

                check(denied,
                      "Free KeyManager context scope/API/pin/dependency negatives must reject");
            }

            for (int i : {0, 2}) {
                for (const auto scope :
                     {std::optional<ProfileScope>(), std::optional<ProfileScope>(passion_scope)}) {
                    auto bad = nodes;
                    bad[i].scope = scope;
                    bool denied = false;
                    try {
                        prepare_file_patches(bad);
                    } catch (...) {
                        denied = true;
                    }

                    check(denied, "Free policy and hash reject missing or foreign scope");
                }
            }

            const auto output = prepare_file_patches(nodes);
            check(output.size() == 3 &&
                          output[2] ==
                                  "d312218f439e3f970934791cdfa572078b5d78b21a3d5eeddff3f2a1c5b6e364\n",
                  "actual API30 native graph derives correct combined hash");

            for (unsigned variant = 0; variant < 7; ++variant) {
                auto bad = nodes;

                switch (variant) {
                case 0:
                    bad[2].donor.reset();
                    break;
                case 1:
                    bad[2].donor.value().sha256 = std::string(64, '0');
                    break;
                case 2:
                    bad[2].patched_dependency = "missing";
                    break;
                case 3:
                    bad[2].dependency_sha256 = std::string(64, '0');
                    break;
                case 4:
                    bad[2].api = 28;
                    break;
                case 5:
                    bad[2].input_sha256 = std::string(64, '0');
                    break;
                case 6:
                    bad[2].output_sha256 = std::string(64, '0');
                    break;
                default:
                    break;
                }

                bool denied = false;
                try {
                    prepare_file_patches(bad);
                } catch (...) {
                    denied = true;
                }

                check(denied, "incorrect API30 native graph provenance rejected");
            }

            check(sha256_file(cil) == cil_pin && sha256_file(contexts) == nodes[1].input_sha256 &&
                          sha256_file(hash) == nodes[2].input_sha256,
                  "all exact policy original inputs preserved");
            std::cout
                    << "actual API30 native policy dependency/hash graph and seven negatives passed\n";

            return 0;
        }

        check(argc == 1, "unknown test argument");
        const auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                               ("voyemu-init-pins-" + random_token());
        ensure_private_directory(directory);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{directory};
        const auto &recipe = init_patch_recipes().front();
        const auto source = directory / "input.rc";
        std::string input;

        for (unsigned n = 1; n < recipe.insert_line; ++n) {
            input += "fixture line\n";
        }

        input += "untouched tail\n";
        const auto output = apply_init_patch(recipe.id, 28, input);
        {
            std::ofstream file(source, std::ios::binary);
            file << input;
        }
        PinnedFilePatch pinned{std::string(recipe.id), 28, source, sha256_bytes(input),
                               sha256_bytes(output)};
        auto wrong_profile = pinned;
        wrong_profile.scope = passion_scope;
        bool scoped = false;
        try {
            prepare_file_patches(std::vector<PinnedFilePatch>{wrong_profile});
        } catch (...) {
            scoped = true;
        }

        check(scoped, "Passion profile cannot dispatch legacy Free/init recipes");
        std::vector<PinnedFilePatch> batch{pinned};
        check(prepare_file_patches(batch) == std::vector<std::string>{output},
              "entire pinned batch verified");
        check(sha256_file(source) == pinned.input_sha256, "preparation never modifies input");
        std::vector<PinnedFilePatch> single{pinned};
        const auto draft = directory / "draft";
        publish_file_patches(single, draft);
        auto receipt = read_private_json(draft / "receipt.json");
        check(receipt.at("files").size() == 1 &&
                      receipt.at("files")[0].at("input_sha256") == pinned.input_sha256,
              "draft records verified provenance");
        check(sha256_file(draft / (pinned.id + ".output")) == pinned.output_sha256,
              "published candidate matches pin");
        const auto receipt_pin = sha256_file(draft / "receipt.json");
        bool overwrite = false;
        try {
            publish_file_patches(single, draft);
        } catch (...) {
            overwrite = true;
        }

        check(overwrite && sha256_file(draft / "receipt.json") == receipt_pin,
              "existing draft never replaced");
        bool duplicate = false;
        try {
            publish_file_patches(std::vector<PinnedFilePatch>{pinned, pinned},
                                 directory / "duplicate");
        } catch (...) {
            duplicate = true;
        }

        check(duplicate && !std::filesystem::exists(directory / "duplicate"),
              "duplicate output rejected without draft");
        auto bad_output = single;
        bad_output.front().output_sha256 = std::string(64, '0');
        const auto failed = directory / "absent-parent/failed";
        bool partial = false;
        try {
            publish_file_patches(bad_output, failed);
        } catch (...) {
            partial = true;
        }

        check(partial && !std::filesystem::exists(failed.parent_path()),
              "pin mismatch creates neither draft nor parent");
        std::atomic<bool> go = false;
        std::atomic<unsigned> successes = 0;
        const auto raced = directory / "raced";
        auto contender = [&] {
            while (!go.load()) {
                std::this_thread::yield();
            }

            try {
                publish_file_patches(single, raced);
                ++successes;
            } catch (const std::exception &) { /* Ignored: only successful racers are counted. */
            }
        };
        std::thread first(contender), second(contender);
        go = true;
        first.join();
        second.join();
        check(successes == 1 &&
                      sha256_file(raced / (pinned.id + ".output")) == pinned.output_sha256,
              "exactly one racing publisher succeeds without replacement");

        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            check(entry.path().filename().string().find(".pending-") == std::string::npos,
                  "successful publication leaves no partial directory");
        }

        for (bool input_pin : {false, true}) {
            auto invalid = batch;

            if (input_pin) {
                invalid.back().input_sha256 = std::string(64, '0');
            } else {
                invalid.back().output_sha256 = std::string(64, '0');
            }

            std::vector<std::string> published;
            bool rejected = false;
            try {
                published = prepare_file_patches(invalid);
            } catch (...) {
                rejected = true;
            }

            check(rejected && published.empty(), "later mismatch returns no partial batch");
        }

        auto invalid = batch;
        invalid.front().input_sha256 = "bad";
        bool invalid_pin = false;
        try {
            prepare_file_patches(invalid);
        } catch (...) {
            invalid_pin = true;
        }

        check(invalid_pin, "malformed SHA rejected");

        // Creating a symbolic link needs a privilege on Windows.
        if (!voyemu::os::host_windows) {
            const auto link = directory / "linked.rc";
            std::filesystem::create_symlink(source, link);
            invalid = batch;
            invalid.front().input = link;
            bool rejected_link = false;
            try {
                prepare_file_patches(invalid);
            } catch (...) {
                rejected_link = true;
            }

            check(rejected_link, "linked source rejected");
        }

        auto store = [&](const std::string &name, const std::string &bytes) {
            auto path = directory / name;
            std::ofstream file(path, std::ios::binary);
            file << bytes;
            file.close();
            check(bool(file), "fixture persisted");

            return path;
        };
        std::vector<PinnedFilePatch> all;

        for (const auto &init : init_patch_recipes()) {
            std::string before;

            for (unsigned n = 1; n < init.insert_line; ++n) {
                before += "fixture line\n";
            }

            before += "tail\n";
            const auto after = apply_init_patch(init.id, 28, before);
            all.push_back({std::string(init.id), 28, store(std::string(init.id) + ".input", before),
                           sha256_bytes(before), sha256_bytes(after)});
        }

        for (const std::string id :
             {"selinux-service-contexts-a9", "selinux-property-contexts-a9"}) {
            const std::string before = "old\n\n";
            const auto after = apply_selinux_context_patch(id, 28, before);
            all.push_back({id, 28, store(id + ".input", before), sha256_bytes(before),
                           sha256_bytes(after)});
        }

        const std::string
                cil = "(type su)\n(type present)\n(typealias alias)\n(typeattribute service_manager_type)\n(typepermissive su)\n",
                donor_bytes =
                        "(allow su present (file (read)))\n(allow su absent_oem (file (write)))\n",
                vendor_bytes = "(allow su_28_0 present_28_0 (dir (search)))\n";
        const auto bounded = apply_bounded_canbus_policy(28, cil, donor_bytes, vendor_bytes).cil;
        PinnedFilePatch policy{"selinux-canbus-a9", 28, store("policy.input", cil),
                               sha256_bytes(cil), sha256_bytes(bounded)};
        policy.donor = PinnedFileDependency{store("donor.input", donor_bytes),
                                            sha256_bytes(donor_bytes)};
        policy.vendor_donor = PinnedFileDependency{store("vendor-donor.input", vendor_bytes),
                                                   sha256_bytes(vendor_bytes)};
        all.push_back(policy);
        const std::string old_hash = std::string(64, '0') + "\n",
                          new_hash = sha256_bytes(bounded) + "\n";
        PinnedFilePatch hash{"selinux-hash-a9", 28, store("hash.input", old_hash),
                             sha256_bytes(old_hash), sha256_bytes(new_hash)};
        hash.patched_dependency = policy.id;
        hash.dependency_sha256 = policy.output_sha256;
        all.insert(all.begin(), hash);
        const auto full = prepare_file_patches(all);
        check(full.size() == 10 && full.front() == new_hash,
              "all ten file artifacts prepared even when consumer precedes dependency");
        const auto full_draft = directory / "all-files";
        publish_file_patches(all, full_draft);
        const auto full_receipt = read_private_json(full_draft / "receipt.json");
        check(full_receipt.at("files").size() == 10 &&
                      full_receipt.at("files")[0].at("patched_dependency").at("sha256") ==
                              policy.output_sha256,
              "published receipt binds patched dependency SHA");
        check(full_receipt.at("files").back().at("donor").at("sha256") == policy.donor->sha256 &&
                      full_receipt.at("files").back().at("vendor_donor").at("sha256") ==
                              policy.vendor_donor->sha256,
              "published receipt binds OEM plat and vendor donor SHA");

        for (unsigned scenario = 0; scenario < 7; ++scenario) {
            auto invalid_all = all;

            if (scenario == 0) {
                invalid_all.back().donor.value().sha256 = std::string(64, '0');
            }

            if (scenario == 4) {
                invalid_all.back().vendor_donor.value().sha256 = std::string(64, '0');
            }

            if (scenario == 5) {
                invalid_all.back().vendor_donor.reset();
            }

            if (scenario == 6) {
                invalid_all.front().vendor_donor = policy.vendor_donor;
            }

            if (scenario == 1) {
                invalid_all.front().dependency_sha256 = std::string(64, '0');
            }

            if (scenario == 2) {
                invalid_all.front().patched_dependency = "missing";
            }

            if (scenario == 3) {
                invalid_all.front().patched_dependency = invalid_all.front().id;
            }

            const auto target = directory / ("rejected-" + std::to_string(scenario));
            bool rejected = false;
            try {
                publish_file_patches(invalid_all, target);
            } catch (...) {
                rejected = true;
            }

            check(rejected && !std::filesystem::exists(target),
                  "donor/dependency mismatch missing reference or cycle cannot publish partial batch");
        }

        {
            std::ofstream file(source, std::ios::binary | std::ios::trunc);
            file << std::string(1024 * 1024 + 1, 'x');
        }
        bool oversized = false;
        try {
            prepare_file_patches(batch);
        } catch (...) {
            oversized = true;
        }

        check(oversized, "oversized input rejected before patching");
        std::cout << "pinned file batch and atomic publication passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
