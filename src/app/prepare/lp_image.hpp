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
enum class LpScratchPlacement {
    within_super,
    grow_terminal_super,
};
inline constexpr std::uint64_t lp_scratch_growth_limit = 256ULL << 20;
inline constexpr std::uint64_t lp_scratch_max_growth = 512ULL << 20;
// Disk growth is the aligned free-space deficit, bounded by the selected profile and RAW limit.
std::uint64_t lp_scratch_growth_bytes(std::uint64_t deficit, std::uint64_t alignment,
                                      std::uint64_t disk_bytes, std::uint64_t limit);

// Preseed only a fresh unformatted scratch extent; never move existing payloads.
// Growth is only for a private raw candidate and requires both GPT copies.
Json seed_lp_overlay_scratch(const std::filesystem::path &image, const std::string &sha256,
                             std::uint64_t bytes,
                             LpScratchPlacement placement = LpScratchPlacement::within_super,
                             std::uint64_t growth_limit = lp_scratch_growth_limit);
} // namespace voyemu
