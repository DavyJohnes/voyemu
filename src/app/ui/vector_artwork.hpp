#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
namespace voyemu::ui {
// Flattened filled SVG paths. A path with its own fill is an accent shape; the others take the
// theme colour when drawn.
struct VectorArtwork {
    struct Shape {
        std::vector<std::vector<std::array<float, 2>>> contours;
        bool accent{};
    };
    std::vector<Shape> shapes;
    std::array<std::uint8_t, 3> accent{};
    float x{}, y{}, width{}, height{};
};
VectorArtwork parse_vector_artwork(std::string_view svg);
inline constexpr int artwork_layer_gap = 2;
// Straight-alpha RGBA of width x (2 * height + artwork_layer_gap): the theme layer in white on
// top, then a transparent gap, then the accent layer in its own colour. Even-odd fill.
std::vector<std::uint8_t> rasterize_artwork(const VectorArtwork &artwork, int width, int height);
} // namespace voyemu::ui
