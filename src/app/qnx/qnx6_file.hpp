#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
namespace voyemu {
// Validate both superblocks and the root directory of an owned, stopped QNX6 image.
void validate_qnx6_image(const std::filesystem::path &image);
// Read-only extraction of a declared regular file, validating short and long names from QNX6.
std::string read_qnx6_file(const std::filesystem::path &image, std::string_view sha256,
                           std::string_view relative);
// Replace equal-size file data in a freshly prepared image; metadata stays unchanged.
void replace_qnx6_file(const std::filesystem::path &image, std::string_view image_sha256,
                       std::string_view relative, std::string_view original_sha256,
                       std::string_view payload, std::uint64_t partition_offset = 0);
// Validate a stopped A9 system initialization against its pinned GPT-wrapped seed.
std::string validate_qnx6_system_initialization(const std::filesystem::path &source,
                                                std::string_view source_sha256,
                                                const std::filesystem::path &stopped,
                                                std::string_view stopped_sha256);
} // namespace voyemu
