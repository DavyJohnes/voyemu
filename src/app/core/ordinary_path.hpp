#pragma once
#include "os/fs.hpp"
#include <filesystem>

namespace voyemu {
enum class PathForm {
    Normalized,
    Canonical,
    NormalizedCanonical,
    CanonicalAncestors
};

// Shared path checks keep the caller's error code/messages and the distinction between an
// existing canonical input and a normalized destination which may not exist yet. File type,
// extent, link count, ownership and open-handle checks remain at their respective boundaries.
template <typename Require>
void ordinary_path(const std::filesystem::path &path, Require require, PathForm form,
                   const char *path_error, const char *link_error) {
    require(path.is_absolute() &&
                    (form == PathForm::Canonical ? std::filesystem::canonical(path) == path
                                                 : path.lexically_normal() == path) &&
                    (form != PathForm::NormalizedCanonical ||
                     std::filesystem::canonical(path) == path),
            path_error);

    for (const auto &at : os::ancestors(path)) {
        require(!std::filesystem::is_symlink(std::filesystem::symlink_status(at)), link_error);

        if (form == PathForm::CanonicalAncestors && std::filesystem::exists(at)) {
            require(std::filesystem::canonical(at) == at, "canonical path required");
        }
    }
}
} // namespace voyemu
