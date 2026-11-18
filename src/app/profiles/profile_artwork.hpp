#pragma once
#include "model.hpp"
namespace voyemu {
// Extracts only declared, SHA-pinned owner PNGs. No OEM artwork is stored in source resources.
Json export_profile_artwork(const std::filesystem::path &root, std::string_view profile,
                            const std::filesystem::path &fresh_directory);
} // namespace voyemu
