#pragma once
#include <filesystem>
#include <string_view>
namespace voyemu::cluster {
// Prepared machine artwork takes precedence over developer source-tree fallback.
inline std::filesystem::path control_artwork_directory(const std::filesystem::path &assets,
                                                       std::string_view profile) {
    if (assets.filename() == "wheel" && assets.parent_path().filename() == "ui") {
        return assets.parent_path() / "controls";
    }

    for (auto directory = assets; !directory.empty(); directory = directory.parent_path()) {
        if (directory.filename() == "profiles") {
            return directory.parent_path() / "machines" / std::string(profile) / "ui/controls";
        }

        if (directory == directory.parent_path()) {
            break;
        }
    }

    return assets / "controls";
}
} // namespace voyemu::cluster
