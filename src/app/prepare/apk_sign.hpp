#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
namespace voyemu {
// Publishes a new v2-only RSA/SHA256 signed APK; Android 7+ only.
// Caller must bind certificate_sha256 to the selected clean Android base identity.
// Source must already be aligned and unsigned. Never replaces destination.
std::string sign_pinned_apk_v2(const std::filesystem::path &source,
                               const std::string &source_sha256, std::string_view pkcs8,
                               const std::string &key_sha256, std::string_view certificate_der,
                               const std::string &certificate_sha256,
                               const std::filesystem::path &destination,
                               std::uint64_t reserve_bytes = 12ULL << 30);
} // namespace voyemu
