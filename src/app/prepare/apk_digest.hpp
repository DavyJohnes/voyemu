#pragma once
#include <filesystem>
#include <string>
namespace voyemu {
// SHA256 content digest for a pinned aligned unsigned APK (v2 / algorithm 0x0103).
// Returns lowercase hex; hashes bounded 1 MiB chunks without loading the whole APK.
// Does not sign, publish or verify an APK signature.
std::string unsigned_apk_v2_digest(const std::filesystem::path &, const std::string &sha256);
} // namespace voyemu
