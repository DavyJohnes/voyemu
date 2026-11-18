#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace voyemu::output {
// Encodes a complete copied frame; never scales, crops or adds host chrome.
std::vector<std::uint8_t> png_frame(std::span<const std::uint8_t> pixels, unsigned width,
                                    unsigned height, bool bgra = false);
void write_artifact(const std::filesystem::path &path, std::span<const std::uint8_t> bytes);
} // namespace voyemu::output
