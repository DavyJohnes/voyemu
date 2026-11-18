#include "capture_png.hpp"
#include "error.hpp"
#include <miniz.h>
#include <stdexcept>
namespace voyemu::output {
// Hosts without a system image codec encode with the vendored miniz deflater.
std::vector<std::uint8_t> png_frame(std::span<const std::uint8_t> pixels, unsigned width,
                                    unsigned height, bool bgra) {
    if (!width || !height || width > 3840 || height > 2160 ||
        pixels.size() != std::size_t(width) * height * 4) {
        throw VoyemuError("TEST_FRAME", "complete bounded pixels required");
    }

    // RGBA keeps its alpha; BGRA input carries no alpha and becomes RGB.
    std::vector<std::uint8_t> rgb;
    const void *image = pixels.data();
    int channels = 4;

    if (bgra) {
        rgb.resize(std::size_t(width) * height * 3);

        for (std::size_t i = 0, o = 0; i < pixels.size(); i += 4, o += 3) {
            rgb[o] = pixels[i + 2];
            rgb[o + 1] = pixels[i + 1];
            rgb[o + 2] = pixels[i];
        }

        image = rgb.data();
        channels = 3;
    }

    std::size_t size = 0;
    void *encoded = tdefl_write_image_to_png_file_in_memory_ex(
            image, int(width), int(height), channels, &size, MZ_DEFAULT_LEVEL, MZ_FALSE);

    if (!encoded) {
        throw VoyemuError("TEST_PNG", "original frame encoding failed");
    }

    std::vector<std::uint8_t> result(static_cast<std::uint8_t *>(encoded),
                                     static_cast<std::uint8_t *>(encoded) + size);
    mz_free(encoded);

    return result;
}
} // namespace voyemu::output
