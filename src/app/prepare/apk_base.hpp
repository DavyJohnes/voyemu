#pragma once
#include <filesystem>
#include <string>
namespace voyemu {
// Extracts the single v2 signer certificate from an immutable, already trusted APK pin.
// Not a general APK signature verifier. The identity loader validates X.509/key matching.
std::string pinned_apk_v2_certificate(const std::filesystem::path &, const std::string &apk_sha256);
} // namespace voyemu
