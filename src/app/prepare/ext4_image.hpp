#pragma once
#include "model.hpp"
namespace voyemu {
// Read-only extraction from a pinned raw ext4 filesystem or its single GPT partition.
// Paths are inside the filesystem, not host paths; aliases and unsupported features fail closed.
Json extract_pinned_ext4_files(const std::filesystem::path &image, const std::string &image_sha256,
                               const std::vector<std::string> &paths,
                               const std::filesystem::path &destination,
                               std::uint64_t reserve_bytes = 20ULL << 30,
                               std::string_view logical_partition = {});
} // namespace voyemu
