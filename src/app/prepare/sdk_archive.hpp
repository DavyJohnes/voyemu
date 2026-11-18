#pragma once
#include "model.hpp"
#include <filesystem>
namespace voyemu {
// Fresh output only. Native compiler aliases are materialized; bundled interpreters/JAR tools are
// omitted.
Json extract_sdk_archive(const std::filesystem::path &archive, const std::string &sha256,
                         const std::filesystem::path &destination,
                         std::uint64_t reserve_bytes = 20ULL << 30,
                         std::uint64_t maximum_bytes = 8ULL << 30, bool strip_root = true);
} // namespace voyemu
