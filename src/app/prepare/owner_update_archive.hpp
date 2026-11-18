#pragma once
#include "model.hpp"
#include <functional>
#include <vector>
namespace voyemu {
// Fresh output only: system.img plus exact raw QNX roles in qnx/. No archive paths are extracted.
Json import_owner_update_archive(const std::filesystem::path &archive, const Json &owner_update,
                                 const Json &system, const Json &qnx_originals,
                                 const std::filesystem::path &output,
                                 std::uint64_t reserve_bytes = 20ULL << 30);
// Internal acquisition extension: same pinned scatter, without reimporting existing QNX roles.
Json import_owner_update_system(const std::filesystem::path &archive, const Json &owner_update,
                                const Json &system, const std::filesystem::path &output,
                                std::uint64_t reserve_bytes = 20ULL << 30);
enum class PinnedZipImageKind {
    SplitFragments,
    SingleImage
};
Json owner_update_directory_identity(const std::filesystem::path &path);
// Streams contiguous pinned ZIP fragments directly into one fresh raw image; no part copies.
Json import_pinned_zip_image(const std::filesystem::path &archive, const Json &archive_pin,
                             const Json &image, const std::filesystem::path &output,
                             std::uint64_t reserve_bytes,
                             const std::function<bool()> &cancelled = {},
                             PinnedZipImageKind kind = PinnedZipImageKind::SplitFragments);
} // namespace voyemu
