#pragma once
#include "model.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
namespace voyemu {
struct LpPartitionView {
    std::uint64_t offset = 0, bytes = 0, super_offset = 0, super_bytes = 0;
    std::string name, metadata_sha256;
};
// Strict immutable LP10.0, one physical device and one LINEAR extent per partition.
// Returns a bounded view into the original pinned GPT image; never copies an image.
LpPartitionView read_lp_partition(const std::filesystem::path &image, const std::string &sha256,
                                  std::string_view name = "system");
// Preseed only a fresh unformatted scratch extent; never move existing payloads.
Json seed_lp_overlay_scratch(const std::filesystem::path &image, const std::string &sha256,
                             std::uint64_t bytes);
} // namespace voyemu
