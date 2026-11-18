#pragma once
#include "model.hpp"
#include <cstdint>
#include <map>
#include <string>
namespace voyemu {
// Validates the descriptor of a supported clean ARM64 image, the android object of profile.json
// with its revision, URL and archive pin, and returns its API.
unsigned native_base_api(const Json &descriptor);
struct CleanBasePartition {
    std::uint64_t offset{}, bytes{};
};
// The pins of the image contents that a descriptor declares in clean_base: receipt_sha256 of the
// extraction receipt, boot_files, images and guest files by name, and on API30 the metadata pin
// and the logical system and system_ext partitions of the super image.
struct CleanBase {
    std::string receipt_sha256;
    std::map<std::string, std::string> boot_files, images, files;
    std::string metadata_sha256;
    CleanBasePartition system, system_ext;
    // The SHA-256 of name in group, such as images and system.img.
    std::string pin(const char *group, const std::string &name) const;
};
CleanBase native_clean_base(const Json &descriptor);
// The SHA-256 of name in the clean_base group of descriptor, such as images and system.img.
std::string native_base_pin(const Json &descriptor, const char *group, const std::string &name);
// Verifies the complete immutable extracted inventory against its registered SDK receipt.
// Does not require the original ZIP after a successful pinned extraction.
Json verify_native_base_image(const Json &descriptor, const std::filesystem::path &directory);
// Expands a pinned clean AOSP archive into a fresh immutable cache; never imports an AVD or
// userdata.
Json prepare_native_base_image(const Json &descriptor, const std::filesystem::path &archive,
                               const std::filesystem::path &destination,
                               std::uint64_t reserve_bytes = 20ULL << 30);
} // namespace voyemu
