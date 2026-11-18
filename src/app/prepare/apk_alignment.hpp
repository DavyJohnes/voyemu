#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace voyemu {
// Verifies classic single-disk APK ZIP metadata and stored payload alignment.
// Android 9 preparation uses zipalign -p 4: native libraries require 4096 bytes.
// Does not verify an APK signature or claim that the package is installable.
void verify_pinned_apk_alignment(const std::filesystem::path &, const std::string &sha256);
// Runs a pinned native zipalign from a private standalone stage, then verifies alignment.
// Returns output SHA; no download, signing or replacement of an existing destination.
std::string align_pinned_apk(const std::filesystem::path &source, const std::string &source_sha256,
                             const std::filesystem::path &zipalign, const std::string &tool_sha256,
                             const std::filesystem::path &destination,
                             std::uint64_t reserve_bytes = 12ULL << 30);
} // namespace voyemu
