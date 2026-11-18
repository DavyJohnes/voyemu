#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace voyemu {
enum class ZipNamePolicy {
    portable,
    exact
};
struct ZipMember {
    std::string name;
    std::uint64_t bytes{}, compressed_bytes{}, data_offset{};
    unsigned mode{}, method{};
    std::uint32_t crc{};
    bool directory = false, symlink = false;
};
// Structural ZIP/ZIP64 index, including matching local headers. Does not establish a trusted pin.
std::vector<ZipMember> zip_layout(const std::filesystem::path &file,
                                  ZipNamePolicy policy = ZipNamePolicy::portable);
} // namespace voyemu
