#pragma once
#include "model.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
namespace voyemu {
struct AndroidBaseIdentity {
    unsigned api;
    std::string_view abi;
};

inline AndroidBaseIdentity android_package_identity(std::string_view package) {
    if (package == "system-images;android-30;default;x86_64") {
        return {30, "x86_64"};
    }

    if (package == "system-images;android-30;default;arm64-v8a") {
        return {30, "arm64-v8a"};
    }

    if (package == "system-images;android-28;default;arm64-v8a") {
        return {28, "arm64-v8a"};
    }

    return {};
}

// Verifies the SDK package, revision, URL and archive pin declared by the profile.
AndroidBaseIdentity native_base_identity(const Json &descriptor);
// Validates the guest accelerator contract independently of the current host.
bool android_kvm(const Json &definition);
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
