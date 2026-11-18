#include "capture_roi.hpp"
#include "error.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace voyemu::api {
namespace {
std::size_t product(std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b) {
        throw VoyemuError("TEST_FRAME", "size overflow");
    }

    return a * b;
}
} // namespace

CroppedFrame crop_frame(std::span<const std::uint8_t> pixels, std::size_t source_width,
                        std::size_t source_height, std::size_t source_stride, PixelRect rect,
                        bool bgra) {
    if (!source_width || !source_height || source_width > 3840 || source_height > 2160) {
        throw VoyemuError("TEST_FRAME", "bounded positive dimensions required");
    }

    const auto source_row = product(source_width, 4);

    if (source_stride < source_row) {
        throw VoyemuError("TEST_FRAME", "stride smaller than pixel row");
    }

    const auto preceding_rows = product(source_height - 1, source_stride);

    if (preceding_rows > std::numeric_limits<std::size_t>::max() - source_row ||
        pixels.size() < preceding_rows + source_row) {
        throw VoyemuError("TEST_FRAME", "complete pixels required");
    }

    if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
        rect.x >= static_cast<std::int64_t>(source_width) ||
        rect.y >= static_cast<std::int64_t>(source_height) ||
        rect.width > static_cast<std::int64_t>(source_width) - rect.x ||
        rect.height > static_cast<std::int64_t>(source_height) - rect.y) {
        throw VoyemuError("TEST_ROI", "positive rectangle entirely inside frame required");
    }

    const auto width = static_cast<std::size_t>(rect.width),
               height = static_cast<std::size_t>(rect.height);
    const auto row = product(width, 4);
    CroppedFrame result{std::vector<std::uint8_t>(product(row, height)), rect, bgra};

    // Frame extent and rectangle validation above bound every row offset and copy.
    for (std::size_t y = 0; y < height; ++y) {
        const auto offset = (static_cast<std::size_t>(rect.y) + y) * source_stride +
                            static_cast<std::size_t>(rect.x) * 4;
        std::copy_n(pixels.data() + offset, row, result.pixels.data() + y * row);
    }

    return result;
}
} // namespace voyemu::api
