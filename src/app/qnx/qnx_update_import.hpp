#pragma once
#include "model.hpp"
#include <filesystem>
namespace voyemu {
// Exact owner archive and raw-image pins are required; no guessed firmware adapter is selected.
Json import_qnx_update(const std::filesystem::path &archive, std::string_view archive_sha256,
                       const Json &originals, const std::filesystem::path &codec,
                       std::string_view codec_sha256, const std::filesystem::path &fresh_directory,
                       std::uint64_t reserve_bytes = 20ULL << 30);
} // namespace voyemu
