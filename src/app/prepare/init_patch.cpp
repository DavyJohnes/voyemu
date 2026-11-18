#include "init_patch.hpp"
#include "error.hpp"
#include <stdexcept>
namespace voyemu {
namespace {
// Coordinates refer to SHA-pinned owner inputs; no original service blocks are distributed.
constexpr InitPatchRecipe recipes[] = {
        {"disable-broadcastradio-a9", 3}, {"disable-incidentd-a9", 17},
        {"disable-mediametrics-a9", 3},   {"disable-perfprofd-a9", 3},
        {"disable-rild-a9", 3},           {"disable-statsd-a9", 17}};
} // namespace

std::span<const InitPatchRecipe> init_patch_recipes() {
    return recipes;
}

std::string apply_init_patch(std::string_view id, unsigned api, std::string_view input) {
    if (api != 28) {
        throw VoyemuError("INIT_PATCH_API", "recipe requires Android API 28");
    }

    for (const auto &recipe : recipes) {
        if (recipe.id == id) {
            std::size_t position = 0;

            for (unsigned line = 1; line < recipe.insert_line; ++line) {
                const auto end = input.find('\n', position);

                if (end == std::string_view::npos) {
                    throw VoyemuError("INIT_PATCH_MISMATCH", "source line extent differs");
                }

                position = end + 1;
            }

            if (input.substr(position).starts_with("    disabled\n")) {
                throw VoyemuError("INIT_PATCH_MISMATCH", "already disabled");
            }

            std::string result(input);
            result.insert(position, "    disabled\n");

            return result;
        }
    }

    throw VoyemuError("INIT_PATCH_NOT_FOUND", "no native recipe");
}
} // namespace voyemu
