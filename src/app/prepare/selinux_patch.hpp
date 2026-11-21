#pragma once
#include "error.hpp"
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
namespace voyemu {
// Pure pinned Android context transforms. API30 hash consumes patched CIL plus its mapping.
// Caller verifies whole-file input/output pins and dependency provenance.
std::string apply_can_early_policy_patch(std::string_view profile, unsigned api,
                                         std::string_view input);
std::string apply_can_early_file_context_patch(std::string_view profile, unsigned api,
                                               std::string_view input);
std::string apply_can_early_property_patch(std::string_view profile, unsigned api,
                                           std::string_view input);
std::string apply_can_simulator_policy_patch(std::string_view profile, unsigned api,
                                             std::string_view input);
std::string apply_can_simulator_context_patch(std::string_view profile, unsigned api,
                                              std::string_view input);
std::string apply_selinux_context_patch(std::string_view id, unsigned api, std::string_view input,
                                        bool qinggan11_731 = false, std::string_view profile = {});
std::string apply_qinggan11_can_simulator_context_patch(std::string_view profile, unsigned api,
                                                        std::string_view input);
std::string apply_qinggan11_can_early_context_patch(std::string_view profile, unsigned api,
                                                    std::string_view input);
std::string apply_qinggan11_can_simulator_policy_patch(std::string_view profile, unsigned api,
                                                       std::string_view input);
// original_navi adds the su read of mac_perms_file that the original_navi feature needs.
std::string apply_qinggan11_can_early_policy_patch(std::string_view profile, unsigned api,
                                                   std::string_view input, bool original_navi);
std::string apply_qinggan11_can_early_property_patch(std::string_view profile, unsigned api,
                                                     std::string_view input);
std::string apply_passion_selinux_patch(std::string_view profile, unsigned api, std::string_view id,
                                        std::string_view input);
struct Api30FileRecipe {
    std::string_view id, target, input_sha256, output_sha256;
};
inline constexpr std::array<Api30FileRecipe, 3> api30_file_recipes = {
        {{"selinux-services-api30", "/system/etc/selinux/plat_sepolicy.cil",
          "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
          "ff445a22bb8718bed947ca97f05d091d0e62a87bb309165cc2c67945e7bd2d14"},
         {"selinux-service-contexts-api30", "/system/etc/selinux/plat_service_contexts",
          "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
          "ac366650a5d9e011d299845155d1319a1f35ec92e107cc1cded0973fa945f1b5"},
         {"selinux-hash-api30", "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
          "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
          "99c1b908750a0a1320e160aa9b20dc0129c332e5667dbdb3c29f33982a55561a"}}};
inline constexpr std::array<Api30FileRecipe, 10> qinggan11_731_api30_file_recipes = {
        {{"selinux-services-api30", "/system/etc/selinux/plat_sepolicy.cil",
          "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
          "b07971ed43c0ccfb97e69af4b251f58a0e021cb93c1e68d3ccbfc664845bb55d"},
         {"keymanager-contexts-api30", "/system/etc/selinux/plat_service_contexts",
          "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
          "d73bcfe6f82f18f52e40e86d55f2a1163d2b526a0143bee9322d0ea5da368061"},
         {"public-qg-runtime-api30", "/system/etc/public.libraries.txt",
          "34c43bf7099fc57daf2db5de07891eb20f76eafa6d9d8a4f4f01e0cefb48c070",
          "61307bd17de318871f19ed5effede9e65204d7c510e246d91155cc7162744763"},
         {"property-contexts-api30", "/system/etc/selinux/plat_property_contexts",
          "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
          "50c13ef43f195b1b94c5e1ed85f3a33578220e6074d9d244414ae73c9837d2db"},
         {"can-simulator-contexts-api30", "/system/etc/selinux/plat_file_contexts",
          "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c",
          "7fc4c203185a6036dafacc401472a152111b9d313702f28befb1327b5dab1f5e"},
         {"can-early-file-contexts-api30", "/system/etc/selinux/plat_file_contexts",
          "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c",
          "873da6472b242a86d657900bdd90d4fa458dbde615c75ea406c6806f06ce8ecc"},
         {"can-simulator-policy-api30", "/system/etc/selinux/plat_sepolicy.cil",
          "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
          "da0a10c128b5c52c67e52795090755fd97fc5d02999c26d150bd9afa27d6fdb1"},
         {"can-early-policy-api30", "/system/etc/selinux/plat_sepolicy.cil",
          "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
          "e8f3b8cb54b3ec09280cf5c630f495b3202f1c9994e796c5a1cf707a5dcd6cf6"},
         {"can-early-property-contexts-api30", "/system/etc/selinux/plat_property_contexts",
          "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
          "6618819c1a2cf856486d66e270459aeccfe03d8d7837c9cc946940cd5a104493"},
         {"selinux-hash-api30", "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
          "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
          "7293ab8b5ff6f3890a7f60b158e6bf28c882896c755edf1e996c9231519c37de"}}};
inline constexpr std::array<Api30FileRecipe, 9> qinggan11_731_original_navi_api30_file_recipes = {
        {qinggan11_731_api30_file_recipes[1],
         qinggan11_731_api30_file_recipes[2],
         {"original-navi-selinux", "/system/etc/selinux/plat_mac_permissions.xml",
          "34309b705a40fbc6a52a96cfc6713c90a683223e0652b936b7227584546169d8",
          "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392"},
         qinggan11_731_api30_file_recipes[4],
         qinggan11_731_api30_file_recipes[5],
         qinggan11_731_api30_file_recipes[6],
         qinggan11_731_api30_file_recipes[7],
         qinggan11_731_api30_file_recipes[8],
         qinggan11_731_api30_file_recipes[9]}};
inline constexpr std::array<Api30FileRecipe, 8> qinggan11_611_api30_file_recipes = {
        {qinggan11_731_api30_file_recipes[1],
         qinggan11_731_api30_file_recipes[2],
         qinggan11_731_api30_file_recipes[4],
         qinggan11_731_api30_file_recipes[5],
         qinggan11_731_api30_file_recipes[6],
         {"can-early-policy-api30", qinggan11_731_api30_file_recipes[7].target,
          qinggan11_731_api30_file_recipes[7].input_sha256,
          "50e7563396d73b053a36a9022d31807121731ecc0ce51b695df65ec7d6838db6"},
         qinggan11_731_api30_file_recipes[8],
         {"selinux-hash-api30", qinggan11_731_api30_file_recipes[9].target,
          qinggan11_731_api30_file_recipes[9].input_sha256,
          "2f158789774a59073cf85bf2c91272416b0da948eea037f9e13586187338f2c9"}}};
inline constexpr std::array<Api30FileRecipe, 4> passion_api30_file_recipes = {
        {{"passion-services", "/system/etc/selinux/plat_sepolicy.cil",
          "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
          "008fade9e6f183e228e7210a7bc9c30750c45f83ecfdb932ad92f61f0eeaadc4"},
         {"passion-service-contexts", "/system/etc/selinux/plat_service_contexts",
          "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
          "25c66b18abdb1bf102f5012387d54336c12a67c80e4ab540b90fdb863c1b82b6"},
         {"passion-property-contexts", "/system/etc/selinux/plat_property_contexts",
          "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
          "1f322fc1fdea910bee266cc00126d2e8429b82a4a283250f12655e5d49b83d2c"},
         {"passion-policy-hash", "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
          "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
          "243e1ba72d2c8e67417aa9443424c2063732ca932ce9add8530e4fdc907c2f10"}}};

inline std::span<const Api30FileRecipe> api30_file_recipes_for(std::string_view profile,
                                                               bool original_navi = false) {
    if (profile == "free-11-chinese-20260731-7.3.1") {
        return original_navi ? std::span<const Api30FileRecipe>(
                                       qinggan11_731_original_navi_api30_file_recipes)
                             : std::span<const Api30FileRecipe>(qinggan11_731_api30_file_recipes);
    }

    if (profile == "free-11-russian-20260318-rc1") {
        if (original_navi) {
            throw VoyemuError("QINGGAN11_ORIGINAL_NAVI",
                              "original Navi file recipes require Free A11 7.3.1");
        }

        return qinggan11_611_api30_file_recipes;
    }

    if (original_navi) {
        throw VoyemuError("QINGGAN11_ORIGINAL_NAVI",
                          "original Navi file recipes require Free A11 7.3.1");
    }

    if (profile == "free-11-chinese-20250710-6.11.1") {
        return qinggan11_611_api30_file_recipes;
    }

    if (profile == "passion-11-russian-20260403-2.1.4.80") {
        return passion_api30_file_recipes;
    }

    return api30_file_recipes;
}

struct BoundedCanbusPolicy {
    std::string cil;
    std::size_t applied, skipped;
};
// Bounds su by the OEM platform su rules (plat for API 28, system_ext for API 30) and the OEM
// vendor su_28_0 or su_30_0 rules whose target is self or a type the cil declares; API 28 also adds
// the A9 canbus services.
BoundedCanbusPolicy apply_bounded_canbus_policy(unsigned api, std::string_view cil,
                                                std::string_view oem_plat,
                                                std::string_view oem_vendor);
} // namespace voyemu
