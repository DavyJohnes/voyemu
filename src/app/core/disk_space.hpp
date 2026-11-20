#pragma once
#include "cli_messages.hpp"
#include "logging.hpp"
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
namespace voyemu {
// The Voyemu installation root of path: the directory that holds its .local or machines component,
// or path itself outside an installation.
inline std::filesystem::path installation_root_of(const std::filesystem::path &path) {
    std::filesystem::path prefix, root = path;

    for (const auto &part : path) {
        if (part == ".local" || part == "machines") {
            root = prefix;
            break;
        }

        prefix /= part;
    }

    return root;
}

// The disk space failure code for the user: whole GiB, free space rounded down, required space
// rounded up and the space to free their difference, so the three numbers agree.
inline cli::LocalizedError disk_space_error(const char *code, const std::filesystem::path &root,
                                            std::uint64_t free, std::uint64_t required) {
    constexpr std::uint64_t gib = 1ULL << 30;
    const auto free_gib = free / gib, required_gib = required / gib + (required % gib ? 1 : 0);

    return {code,
            "error.disk_space",
            {{"path", root.string()},
             {"free", std::to_string(free_gib)},
             {"required", std::to_string(required_gib)},
             {"missing", std::to_string(required_gib - free_gib)}}};
}

// Throws disk_space_error naming the installation root unless the volume of directory, or of its
// nearest existing ancestor, has needed bytes free in addition to the margin that preparation keeps
// free; a debug record keeps the exact numbers.
inline void require_disk_space(const char *code, const std::filesystem::path &directory,
                               std::uint64_t needed, std::uint64_t margin) {
    auto existing = directory;

    while (!std::filesystem::exists(existing) && existing != existing.parent_path()) {
        existing = existing.parent_path();
    }

    const auto free = std::filesystem::space(existing).available;
    const auto required = needed > std::numeric_limits<std::uint64_t>::max() - margin
                                  ? std::numeric_limits<std::uint64_t>::max()
                                  : needed + margin;

    if (free >= required) {
        return;
    }

    logging::write(logging::Level::debug, "disk", "space short",
                   {{"code", code},
                    {"directory", existing.string()},
                    {"free", free},
                    {"needed", needed},
                    {"margin", margin}});

    throw disk_space_error(code, installation_root_of(existing), free, required);
}
} // namespace voyemu
