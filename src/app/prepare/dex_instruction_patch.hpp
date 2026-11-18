#pragma once
#include <cstdint>
#include <string>
namespace voyemu {
struct DexLiteralPatch {
    std::string source_sha256, owner, method, prototype, instructions_sha256, output_sha256;
    std::uint32_t access{}, code_unit{}, instruction_units{};
    std::uint16_t registers{}, inputs{}, outputs{}, tries{}, reg{}, before{}, after{};
};
// Fixed-width const/16 only. All other bytes except the DEX header digests survive unchanged.
std::string patch_dex_literal(const std::string &source, const DexLiteralPatch &recipe);
DexLiteralPatch launcher_ethernet_guard_recipe();
// Registered OEM recipe: zero-initialized EGL array stores become nop pairs;
// duplicate renderable attribute becomes EGL_NONE. No metadata relocation.
std::string patch_navi_msaa(const std::string &source);
} // namespace voyemu
