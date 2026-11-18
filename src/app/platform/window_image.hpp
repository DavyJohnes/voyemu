#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace voyemu::window {
// Straight (non-premultiplied) RGBA rows, four bytes per pixel, no padding.
struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
};
// The single PNG decoder of the window and its tests (SDL's pinned stb_image).
Image decode_png(const std::filesystem::path &path);
Image decode_png_bytes(std::string_view bytes);
// Writes tightly packed BGRA rows as an RGB PNG with alpha.
void save_png(const std::filesystem::path &path, int width, int height,
              const std::vector<std::uint8_t> &bgra);
} // namespace voyemu::window
