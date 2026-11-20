#pragma once
#include "model.hpp"
#include <filesystem>
#include <functional>
namespace voyemu {
// Exact owner archive and raw-image pins are required; no guessed firmware adapter is selected.
// advance receives the byte count of each restored image block, for a progress meter. A volume
// without the reserve fails with the disk space error.
Json import_qnx_update(const std::filesystem::path &archive, std::string_view archive_sha256,
                       const Json &originals, const std::filesystem::path &codec,
                       std::string_view codec_sha256, const std::filesystem::path &fresh_directory,
                       std::uint64_t reserve_bytes = 20ULL << 30,
                       const std::function<void(std::uint64_t)> &advance = {});
} // namespace voyemu
