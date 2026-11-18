#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace voyemu::api {
struct PixelRect {
    std::int64_t x, y, width, height;
};
struct CroppedFrame {
    std::vector<std::uint8_t> pixels;
    PixelRect rect;
    bool bgra;
};
// Copies pixels without scaling, channel conversion or alpha modification.
// Explicit rectangles must be entirely inside the source. Row padding is omitted.
CroppedFrame crop_frame(std::span<const std::uint8_t> pixels, std::size_t source_width,
                        std::size_t source_height, std::size_t source_stride, PixelRect rect,
                        bool bgra = false);
} // namespace voyemu::api
