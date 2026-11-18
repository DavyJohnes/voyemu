#pragma once
#include "model.hpp"
#include <string_view>
namespace voyemu {
void validate_statusbar_overlay_manifest(std::string_view binary_axml);
// Registered API28 statusbar-overlay only. Native source receipt required.
// Publishes an unsigned resource-only APK; alignment/signing are separate producers.
Json build_resource_component(const std::filesystem::path &source,
                              const std::filesystem::path &aapt2, const std::string &aapt2_sha256,
                              const std::filesystem::path &framework,
                              const std::string &framework_sha256,
                              const std::filesystem::path &destination,
                              std::uint64_t reserve_bytes = 20ULL << 30);
} // namespace voyemu
