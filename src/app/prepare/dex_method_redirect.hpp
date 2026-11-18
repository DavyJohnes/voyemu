#pragma once
#include <string>
namespace voyemu {
inline constexpr const char *launcher_media_source_sha256 =
        "52af87fe8d3b03c55db7f1e628f856b11b9a006025ac27985960b3bf214393b0";
inline constexpr const char *launcher_media_output_sha256 =
        "27a9126dcab607e6a1d8216c1cb6f462d533383b31e0ff14c2400691bd77e79b";
// Registered media-card-music-fix recipe for original Launcher classes2.dex only.
// Four existing invoke-static method references; no pool, size, register or metadata edits.
std::string patch_launcher_media_redirect(const std::string &source);
} // namespace voyemu
