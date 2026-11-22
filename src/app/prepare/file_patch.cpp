#include "file_patch.hpp"
#include "apk_base.hpp"
#include "check.hpp"
#include "codec_timing.hpp"
#include "init_patch.hpp"
#include "model.hpp"
#include "original_navi_selinux.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "selinux_patch.hpp"
#include <fstream>
#include <functional>
#include <map>
#include <set>
#ifdef __APPLE__
#include <stdio.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
namespace voyemu {
namespace {
constexpr ProfileScope qinggan09_scope{ProfilePlatform::qinggan09, ProfileBoard::sa8155},
        qinggan11_scope{ProfilePlatform::qinggan11, ProfileBoard::sa8155},
        passion_scope{ProfilePlatform::passion, ProfileBoard::sa8155};

bool valid_pin(const std::string &pin) {
    return sha256_text(pin);
}

std::string read_pinned(const std::filesystem::path &path, const std::string &pin,
                        std::size_t maximum) {
    if (!valid_pin(pin)) {
        throw VoyemuError("FILE_PATCH_PIN", "lowercase SHA256 required");
    }

    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) > maximum) {
        throw VoyemuError("FILE_PATCH_INPUT", "bounded ordinary input required");
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw VoyemuError("FILE_PATCH_INPUT", "cannot open input");
    }

    std::string input;
    char buffer[4096];

    while (file) {
        file.read(buffer, sizeof buffer);
        input.append(buffer, static_cast<std::size_t>(file.gcount()));

        if (input.size() > maximum) {
            throw VoyemuError("FILE_PATCH_INPUT", "input grew beyond bound");
        }
    }

    if (!file.eof()) {
        throw VoyemuError("FILE_PATCH_INPUT", "read failed");
    }

    if (sha256_bytes(input) != pin) {
        throw VoyemuError("FILE_PATCH_INPUT_SHA", "pinned input mismatch");
    }

    return input;
}
} // namespace

std::vector<std::string> native_file_patch_steps(std::string_view id) {
    if (id == "original-navi-selinux") {
        return {"original-navi-selinux.json"};
    }

    if (id == "ccodec-allocation-a9") {
        return {"ccodec-allocation-a9.json"};
    }

    for (const auto &recipe : init_patch_recipes()) {
        if (recipe.id == id) {
            return {std::string(id) + ".json"};
        }
    }

    if (id == "selinux-services-api30" || id == "selinux-service-contexts-api30" ||
        id == "selinux-hash-api30") {
        return {std::string(id) + ".json"};
    }

    if (id == "passion-services" || id == "passion-service-contexts" ||
        id == "passion-property-contexts" || id == "passion-hwservice-contexts" ||
        id == "passion-file-contexts" || id == "passion-policy-hash") {
        return {std::string(id) + ".json"};
    }

    if (id == "keymanager-contexts-api30" || id == "public-qg-runtime-api30") {
        return {std::string(id) + ".json"};
    }

    if (id == "can-simulator-contexts-api30") {
        return {"can-simulator-contexts-api30.json"};
    }

    if (id == "can-early-file-contexts-api30") {
        return {"can-simulator-contexts-api30.json", "can-early-file-contexts-api30.json"};
    }

    if (id == "can-simulator-policy-api30") {
        return {"selinux-services-api30.json", "can-simulator-policy-api30.json"};
    }

    if (id == "can-early-policy-api30") {
        return {"selinux-services-api30.json", "can-simulator-policy-api30.json",
                "can-early-policy-api30.json"};
    }

    if (id == "can-early-property-contexts-api30") {
        return {"property-contexts-api30.json", "can-early-property-contexts-api30.json"};
    }

    if (id == "can-early-policy-a9") {
        return {"selinux-canbus-a9.json", "selinux-bounded-su-a9.json",
                "can-simulator-policy-a9.json", "can-early-policy-a9.json"};
    }

    if (id == "can-early-file-contexts-a9") {
        return {"can-simulator-contexts-a9.json", "can-early-file-contexts-a9.json"};
    }

    if (id == "can-early-property-contexts-a9") {
        return {"selinux-property-contexts-a9.json", "can-early-property-contexts-a9.json"};
    }

    if (id == "can-simulator-policy-a9") {
        return {"selinux-canbus-a9.json", "selinux-bounded-su-a9.json",
                "can-simulator-policy-a9.json"};
    }

    if (id == "can-simulator-contexts-a9") {
        return {"can-simulator-contexts-a9.json"};
    }

    if (id == "selinux-canbus-a9") {
        return {"selinux-canbus-a9.json", "selinux-bounded-su-a9.json"};
    }

    if (id == "selinux-hash-a9") {
        return {"selinux-hash.json"};
    }

    if (id == "selinux-service-contexts-a9" || id == "selinux-property-contexts-a9") {
        return {std::string(id) + ".json"};
    }

    return {};
}

std::vector<std::string> prepare_file_patches(std::span<const PinnedFilePatch> patches) {
    if (patches.size() > 64) {
        throw VoyemuError("FILE_PATCH_BOUND", "at most 64 operations per batch");
    }

    std::map<std::string, std::size_t> index;

    for (std::size_t i = 0; i < patches.size(); ++i) {
        if (!index.emplace(patches[i].id, i).second) {
            throw VoyemuError("FILE_PATCH_DUPLICATE", "duplicate recipe ID");
        }
    }

    std::vector<std::string> candidates(patches.size());
    std::vector<unsigned> state(patches.size());
    std::function<void(std::size_t)> visit;
    visit = [&](std::size_t i) {
        if (state[i] == 2) {
            return;
        }

        if (state[i] == 1) {
            throw VoyemuError("FILE_PATCH_CYCLE", "dependency cycle");
        }

        state[i] = 1;
        const auto &patch = patches[i];

        if (!valid_pin(patch.output_sha256)) {
            throw VoyemuError("FILE_PATCH_PIN", "lowercase output SHA256 required");
        }

        const bool passion = patch.id == "passion-services" ||
                             patch.id == "passion-service-contexts" ||
                             patch.id == "passion-property-contexts" ||
                             patch.id == "passion-hwservice-contexts" ||
                             patch.id == "passion-file-contexts" ||
                             patch.id == "passion-policy-hash";
        const bool free_public = patch.id == "public-qg-runtime-api30",
                   free_property = patch.id == "property-contexts-api30",
                   free_key = patch.id == "keymanager-contexts-api30",
                   navi_selinux = patch.id == "original-navi-selinux";
        const bool qinggan11_can = (patch.id == "can-simulator-contexts-api30" ||
                                    patch.id == "can-early-file-contexts-api30" ||
                                    patch.id == "can-simulator-policy-api30" ||
                                    patch.id == "can-early-policy-api30" ||
                                    patch.id == "can-early-property-contexts-api30");
        const bool free_policy = (patch.id == "selinux-services-api30" ||
                                  patch.id == "selinux-hash-api30") &&
                                 patch.scope.has_value();
        const bool codec = patch.id == "ccodec-allocation-a9";

        if (patch.certificate_apk && !navi_selinux) {
            throw VoyemuError("FILE_PATCH_DEPENDENCY",
                              "certificate APK is restricted to original Navi SELinux");
        }

        const bool bounded = patch.id == "selinux-canbus-a9" ||
                             patch.id == "can-simulator-policy-a9" ||
                             patch.id == "can-early-policy-a9";
        const bool bounded_api30 = patch.id == "can-simulator-policy-api30" ||
                                   patch.id == "can-early-policy-api30";

        if (patch.vendor_donor && !bounded && !bounded_api30) {
            throw VoyemuError("FILE_PATCH_DEPENDENCY",
                              "vendor donor is restricted to the bounded su policies");
        }

        if (patch.id == "can-simulator-contexts-a9" || patch.id == "can-simulator-policy-a9" ||
            patch.id == "can-early-property-contexts-a9" ||
            patch.id == "can-early-file-contexts-a9" || patch.id == "can-early-policy-a9") {
            if (patch.scope != qinggan09_scope || patch.api != 28) {
                throw VoyemuError("FILE_PATCH_PROFILE", "exact A9 API28 family required");
            }
        } else if (codec) {
            if (patch.scope != qinggan09_scope || patch.api != 28) {
                throw VoyemuError("FILE_PATCH_PROFILE", "exact A9 API28 codec profile required");
            }
        } else if (navi_selinux) {
            if (!patch.original_navi || patch.api != 30) {
                throw VoyemuError("FILE_PATCH_PROFILE",
                                  "API30 profile with the original_navi feature required");
            }
        } else if (free_public || free_property || free_key || free_policy) {
            if (patch.scope != qinggan11_scope || patch.api != 30) {
                throw VoyemuError("FILE_PATCH_PROFILE",
                                  "exact Free A11 API30 file profile required");
            }
        } else if (qinggan11_can) {
            if (patch.scope != qinggan11_scope || patch.api != 30) {
                throw VoyemuError("FILE_PATCH_PROFILE",
                                  "exact Free A11 API30 can-early profile required");
            }
        } else if (passion ? (patch.scope != passion_scope || patch.api != 30)
                           : patch.scope.has_value()) {
            throw VoyemuError("FILE_PATCH_PROFILE", "exact scoped Passion recipe/profile required");
        }

        const bool init = patch.id.starts_with("disable-");
        const auto input = read_pinned(patch.input, patch.input_sha256,
                                       init ? 1024 * 1024 : 32 * 1024 * 1024);
        std::string output;

        if (patch.id == "can-early-file-contexts-a9") {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "unexpected early file context dependency");
            }

            output = apply_can_early_file_context_patch(
                    *patch.scope, patch.api,
                    apply_can_simulator_context_patch(*patch.scope, patch.api, input));
        } else if (patch.id == "can-early-property-contexts-a9") {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY", "unexpected early property dependency");
            }

            output = apply_can_early_property_patch(
                    *patch.scope, patch.api,
                    apply_selinux_context_patch("selinux-property-contexts-a9", patch.api, input));
        } else if (patch.id == "can-simulator-contexts-a9") {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "unexpected simulator context dependency");
            }

            output = apply_can_simulator_context_patch(*patch.scope, patch.api, input);
        } else if (codec) {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY", "unexpected codec dependency");
            }

            output =
                    android_codec_timing_patch(
                            input,
                            patch.output_sha256 !=
                                    "36da952afee36f4e768ec74080aa157e92ba29881718dd9d65c8a5a8401b166a")
                            .bytes;
        } else if (patch.id == "original-navi-selinux") {
            if (!patch.original_navi || patch.api != 30 ||
                bool(patch.donor) == bool(patch.certificate_apk) ||
                patch.input_sha256 !=
                        "34309b705a40fbc6a52a96cfc6713c90a683223e0652b936b7227584546169d8" ||
                patch.output_sha256 !=
                        "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392" ||
                !patch.patched_dependency.empty() || !patch.dependency_sha256.empty()) {
                throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_SELINUX",
                                  "exact disabled recipe contract required");
            }

            std::string cert;

            if (patch.certificate_apk) {
                if (patch.certificate_apk->sha256 !=
                    "dceddff70db38bacb50ebffa46d36da76f70e5596c6184bca51c48c3ed562a05") {
                    throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_SELINUX",
                                      "original APK pin differs");
                }

                cert = pinned_apk_v2_certificate(patch.certificate_apk->input,
                                                 patch.certificate_apk->sha256);
            } else {
                if (patch.donor->sha256 !=
                    "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3") {
                    throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_SELINUX", "donor pin differs");
                }

                cert = read_pinned(patch.donor->input, patch.donor->sha256, 64 * 1024);
            }

            if (sha256_bytes(cert) !=
                "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3") {
                throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_SELINUX", "derived certificate differs");
            }

            output = apply_original_navi_selinux(patch.api, input, cert);
        } else if (patch.id == "can-simulator-contexts-api30") {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "unexpected API30 simulator context dependency");
            }

            output = apply_qinggan11_can_simulator_context_patch(*patch.scope, patch.api, input);
        } else if (patch.id == "can-early-file-contexts-api30") {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "unexpected API30 early context dependency");
            }

            output = apply_qinggan11_can_early_context_patch(
                    *patch.scope, patch.api,
                    apply_qinggan11_can_simulator_context_patch(*patch.scope, patch.api, input));
        } else if (bounded_api30) {
            if (!patch.donor || !patch.vendor_donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError(
                        "FILE_PATCH_DEPENDENCY",
                        "bounded API30 policy requires only pinned OEM system_ext and vendor donors");
            }

            const auto system_ext = read_pinned(patch.donor->input, patch.donor->sha256,
                                                32 * 1024 * 1024),
                       vendor = read_pinned(patch.vendor_donor->input, patch.vendor_donor->sha256,
                                            32 * 1024 * 1024);
            output = apply_qinggan11_can_simulator_policy_patch(
                    *patch.scope, patch.api,
                    apply_bounded_canbus_policy(
                            patch.api,
                            apply_selinux_context_patch("selinux-services-api30", patch.api, input,
                                                        true),
                            system_ext, vendor)
                            .cil);

            if (patch.id == "can-early-policy-api30") {
                output = apply_qinggan11_can_early_policy_patch(*patch.scope, patch.api, output,
                                                                patch.original_navi);
            }
        } else if (patch.id == "can-early-property-contexts-api30") {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "unexpected API30 early property dependency");
            }

            if (patch.input_sha256 !=
                "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e") {
                throw VoyemuError("FILE_PATCH_INPUT",
                                  "exact API30 property context baseline required");
            }

            output = apply_qinggan11_can_early_property_patch(
                    *patch.scope, patch.api,
                    input + "persist.clusterCardInfo u:object_r:system_prop:s0 exact string\nignoreActivation u:object_r:system_prop:s0 exact bool\n");
        } else if (free_public) {
            if (patch.input_sha256 !=
                        "34c43bf7099fc57daf2db5de07891eb20f76eafa6d9d8a4f4f01e0cefb48c070" ||
                patch.output_sha256 !=
                        "61307bd17de318871f19ed5effede9e65204d7c510e246d91155cc7162744763" ||
                input.size() != 471 || input.back() != '\n' ||
                input.find("libqg_runtime.so") != std::string::npos) {
                throw VoyemuError("FILE_PATCH_INPUT",
                                  "exact API30 public library baseline required");
            }

            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY", "unexpected public library dependency");
            }

            output = input + "libqg_runtime.so 64\n";
        } else if (free_property) {
            if (patch.input_sha256 !=
                        "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e" ||
                patch.output_sha256 !=
                        "50c13ef43f195b1b94c5e1ed85f3a33578220e6074d9d244414ae73c9837d2db" ||
                input.size() != 46954 || input.back() != '\n' ||
                input.find("persist.clusterCardInfo") != std::string::npos ||
                input.find("ignoreActivation") != std::string::npos) {
                throw VoyemuError("FILE_PATCH_INPUT",
                                  "exact API30 property context baseline required");
            }

            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "unexpected Free A11 7.3.1 property context dependency");
            }

            output =
                    input +
                    "persist.clusterCardInfo u:object_r:system_prop:s0 exact string\nignoreActivation u:object_r:system_prop:s0 exact bool\n";
        } else if (passion) {
            if (patch.id == "passion-policy-hash") {
                if (!patch.donor ||
                    patch.donor->sha256 !=
                            "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60" ||
                    patch.input_sha256 !=
                            "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766" ||
                    patch.patched_dependency != "passion-services" ||
                    patch.dependency_sha256 !=
                            "ca7207fdf6cf5b9e7755e33f6bc7eb800eccaedd962f66d30cb5ea195f8597b3") {
                    throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                      "exact Passion CIL/mapping/hash provenance required");
                }

                const auto dependency = index.find(patch.patched_dependency);

                if (dependency == index.end()) {
                    throw VoyemuError("FILE_PATCH_DEPENDENCY", "Passion patched CIL missing");
                }

                visit(dependency->second);
                const auto &cil = candidates[dependency->second];

                if (sha256_bytes(cil) != patch.dependency_sha256) {
                    throw VoyemuError("FILE_PATCH_DEPENDENCY_SHA", "Passion CIL differs");
                }

                const auto mapping = read_pinned(patch.donor->input, patch.donor->sha256,
                                                 1024 * 1024);
                output = apply_passion_selinux_patch(*patch.scope, patch.api, "policy-hash",
                                                     cil + mapping);
            } else {
                if (patch.donor || !patch.patched_dependency.empty() ||
                    !patch.dependency_sha256.empty()) {
                    throw VoyemuError("FILE_PATCH_DEPENDENCY", "unexpected Passion dependency");
                }

                output = apply_passion_selinux_patch(
                        *patch.scope, patch.api,
                        patch.id == "passion-services"             ? "services"
                        : patch.id == "passion-service-contexts"   ? "service-contexts"
                        : patch.id == "passion-hwservice-contexts" ? "hwservice-contexts"
                        : patch.id == "passion-file-contexts"      ? "file-contexts"
                                                                   : "property-contexts",
                        input);
            }
        } else if (bounded) {
            if (!patch.donor || !patch.vendor_donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "bounded policy requires only pinned OEM plat and vendor donors");
            }

            const auto plat = read_pinned(patch.donor->input, patch.donor->sha256,
                                          32 * 1024 * 1024),
                       vendor = read_pinned(patch.vendor_donor->input, patch.vendor_donor->sha256,
                                            32 * 1024 * 1024);
            output = apply_bounded_canbus_policy(patch.api, input, plat, vendor).cil;

            if (patch.id != "selinux-canbus-a9") {
                output = apply_can_simulator_policy_patch(*patch.scope, patch.api, output);
            }

            if (patch.id == "can-early-policy-a9") {
                output = apply_can_early_policy_patch(*patch.scope, patch.api, output);
            }
        } else if (patch.id == "selinux-hash-api30") {
            if (patch.api != 30 || !patch.donor ||
                patch.donor->sha256 !=
                        "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60" ||
                patch.input_sha256 !=
                        "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766" ||
                !(patch.patched_dependency == "can-early-policy-api30" ||
                  patch.patched_dependency == "selinux-services-api30")) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "exact API30 CIL/mapping/hash provenance required");
            }

            const auto dependency = index.find(patch.patched_dependency);

            if (dependency == index.end()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY", "API30 patched CIL missing");
            }

            visit(dependency->second);
            const auto &cil = candidates[dependency->second];

            if (sha256_bytes(cil) != patch.dependency_sha256) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY_SHA", "API30 CIL differs");
            }

            const auto mapping = read_pinned(patch.donor->input, patch.donor->sha256, 1024 * 1024);
            output = apply_selinux_context_patch(patch.id, patch.api, cil + mapping, free_policy,
                                                 patch.patched_dependency, patch.original_navi);
        } else if (patch.id == "selinux-hash-a9") {
            if (patch.donor || patch.patched_dependency.empty() ||
                !valid_pin(patch.dependency_sha256)) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY",
                                  "policy hash requires pinned patched dependency");
            }

            const auto dependency = index.find(patch.patched_dependency);

            if (dependency == index.end()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY", "unknown patched dependency");
            }

            visit(dependency->second);
            const auto &bytes = candidates[dependency->second];

            if (sha256_bytes(bytes) != patch.dependency_sha256) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY_SHA", "patched dependency mismatch");
            }

            output = apply_selinux_context_patch(patch.id, patch.api, bytes);
        } else {
            if (patch.donor || !patch.patched_dependency.empty() ||
                !patch.dependency_sha256.empty()) {
                throw VoyemuError("FILE_PATCH_DEPENDENCY", "unexpected dependency");
            }

            output = init ? apply_init_patch(patch.id, patch.api, input)
                          : apply_selinux_context_patch(patch.id, patch.api, input, free_policy);
        }

        if (sha256_bytes(output) != patch.output_sha256) {
            throw VoyemuError("FILE_PATCH_OUTPUT_SHA", patch.id + " pinned output mismatch");
        }

        candidates[i] = std::move(output);
        state[i] = 2;
    };

    for (std::size_t i = 0; i < patches.size(); ++i) {
        visit(i);
    }

    return candidates;
}

Json file_patch_receipt_entry(const PinnedFilePatch &patch) {
    Json item = {{"id", patch.id},
                 {"api", patch.api},
                 {"input", patch.input.string()},
                 {"input_sha256", patch.input_sha256},
                 {"output", patch.id + ".output"},
                 {"output_sha256", patch.output_sha256}};

    if (!patch.profile.empty()) {
        item["profile"] = patch.profile;
    }

    if (patch.certificate_apk) {
        item["certificate_source"] = {
                {"source", "certificate-from-original:Navi"},
                {"apk", patch.certificate_apk->input.string()},
                {"apk_sha256", patch.certificate_apk->sha256},
                {"certificate_sha256",
                 "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3"}};
    }

    if (patch.donor) {
        item["donor"] = {{"input", patch.donor->input.string()}, {"sha256", patch.donor->sha256}};
    }

    if (patch.vendor_donor) {
        item["vendor_donor"] = {{"input", patch.vendor_donor->input.string()},
                                {"sha256", patch.vendor_donor->sha256}};
    }

    if (!patch.patched_dependency.empty()) {
        item["patched_dependency"] = {{"id", patch.patched_dependency},
                                      {"sha256", patch.dependency_sha256}};
    }

    return item;
}

void publish_file_patches(std::span<const PinnedFilePatch> patches,
                          const std::filesystem::path &destination) {
    if (patches.empty() || !destination.is_absolute() || destination.filename().empty() ||
        destination.filename() == "." || destination.filename() == "..") {
        throw VoyemuError("FILE_DRAFT_PATH", "new absolute draft directory required");
    }

    if (std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        throw VoyemuError("FILE_DRAFT_EXISTS", "destination must be new");
    }

    std::set<std::string> names;

    for (const auto &patch : patches) {
        if (!names.insert(patch.id).second) {
            throw VoyemuError("FILE_DRAFT_DUPLICATE", "duplicate recipe output");
        }
    }

    const auto candidates = prepare_file_patches(patches);
    ensure_private_directory(destination.parent_path());
    const auto pending = destination.parent_path() /
                         ("." + destination.filename().string() + ".pending-" + random_token());

    if (!std::filesystem::create_directory(pending)) {
        throw VoyemuError("FILE_DRAFT_CREATE", "exclusive draft creation failed");
    }

    struct Cleanup {
        std::filesystem::path path;
        bool owned = true;

        ~Cleanup() {
            if (owned) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup{pending};
    ensure_private_directory(pending);
    Json files = Json::array();

    for (std::size_t i = 0; i < patches.size(); ++i) {
        const auto &patch = patches[i];
        const auto name = patch.id + ".output";
        const auto file = pending / name;
        std::ofstream output(file, std::ios::binary);

        if (!output) {
            throw VoyemuError("FILE_DRAFT_WRITE", "cannot create candidate");
        }

        std::filesystem::permissions(file, std::filesystem::perms::owner_read |
                                                   std::filesystem::perms::owner_write);
        output.write(candidates[i].data(), static_cast<std::streamsize>(candidates[i].size()));
        output.close();

        if (!output) {
            throw VoyemuError("FILE_DRAFT_WRITE", "incomplete candidate");
        }

        if (sha256_file(file) != patch.output_sha256) {
            throw VoyemuError("FILE_DRAFT_SHA", "candidate changed while writing");
        }

        files.push_back(file_patch_receipt_entry(patch));
    }

    write_private_json(pending / "receipt.json",
                       {{"schema", 1}, {"kind", "native-file-draft"}, {"files", files}});

    if (!os::rename_exclusive(pending, destination)) {
        throw VoyemuError("FILE_DRAFT_PUBLISH", "exclusive directory rename failed");
    }

    cleanup.owned = false;
}
} // namespace voyemu
