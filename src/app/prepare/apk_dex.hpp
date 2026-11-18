#pragma once
#include "dex_inventory.hpp"
#include <filesystem>
namespace voyemu {
struct ApkDexPin {
    std::string entry, sha256;
};
struct ApkDexTemplate {
    std::string bytes, sha256;
    std::vector<std::string> expected_classes;
};
struct ApkDexAppendResult {
    std::string sha256;
    std::vector<DexAppendSlot> slots;
};
// Pinned inputs and exact class allowlists are checked before an unsigned APK candidate is
// published. Original entries are preserved; APK alignment and signing must follow separately.
ApkDexAppendResult append_pinned_apk_dex(const std::filesystem::path &source,
                                         const std::string &source_sha256,
                                         std::span<const ApkDexPin> original,
                                         std::span<const ApkDexTemplate> templates,
                                         const std::filesystem::path &destination,
                                         std::uint64_t reserve_bytes = 12ULL << 30);
} // namespace voyemu
