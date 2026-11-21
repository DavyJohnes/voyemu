#pragma once
#include "check.hpp"
#include "model.hpp"
#include <cstdint>
#include <filesystem>

namespace voyemu {
// Input checks of binary profile steps; each failure carries the code of require.
struct ProfileStepInputs {
    Check require;

    // A canonical path without linked ancestors.
    void ordinary(const std::filesystem::path &path) const;
    // An ordinary single-link regular file of at most bound bytes.
    void file(const std::filesystem::path &path, std::uint64_t bound) const;
    // Path, SHA-256 and size of every file below source, sorted by path: at most 4096 ordinary
    // files and 32 MiB in total, each stable while hashed.
    Json inventory(const std::filesystem::path &source) const;
};
} // namespace voyemu
