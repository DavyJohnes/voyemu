#pragma once
#include <span>
#include <string>
#include <string_view>
namespace voyemu {
struct InitPatchRecipe {
    std::string_view id;
    unsigned insert_line;
};
std::span<const InitPatchRecipe> init_patch_recipes();
// Returns a candidate only; callers must verify input pins before atomic publication.
std::string apply_init_patch(std::string_view id, unsigned api, std::string_view input);
} // namespace voyemu
