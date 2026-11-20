#pragma once
#include "model.hpp"
#include <functional>
namespace voyemu {
// One pinned factory Android image, using the existing native ZIP/ZSTD primitives; advance receives
// the byte count of each restored image block, for a progress meter. A volume without the reserve
// fails with the disk space error.
Json import_android_update_image(const std::filesystem::path &archive,
                                 std::string_view archive_sha256, const Json &original,
                                 const std::filesystem::path &codec, std::string_view codec_sha256,
                                 const std::filesystem::path &fresh_directory,
                                 std::uint64_t reserve_bytes = 20ULL << 30,
                                 const std::function<void(std::uint64_t)> &advance = {});
} // namespace voyemu
