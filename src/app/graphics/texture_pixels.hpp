#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace voyemu::media {
// ImageIO supplies straight RGBA after unpremultiplication. ImGui samples it
// linearly, so transparent-black neighbours otherwise create dark edge fringes.
inline void fill_transparent_rgb(std::span<std::uint8_t> rgba, std::size_t width,
                                 std::size_t height) {
    if (!width || !height || width > 1024 || height > 1024 || rgba.size() != width * height * 4) {
        throw std::invalid_argument("TEXTURE_RGBA: bounded complete image required");
    }

    for (int y = 0; y < static_cast<int>(height); ++y) {
        for (int x = 0; x < static_cast<int>(width); ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;

            if (rgba[offset + 3]) {
                continue;
            }

            unsigned sums[3]{}, weight = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;

                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) ||
                        ny >= static_cast<int>(height)) {
                        continue;
                    }

                    const auto neighbour = (static_cast<std::size_t>(ny) * width + nx) * 4;
                    const auto alpha = rgba[neighbour + 3];
                    weight += alpha;

                    for (unsigned channel = 0; channel < 3; ++channel) {
                        sums[channel] += rgba[neighbour + channel] * alpha;
                    }
                }
            }

            if (weight) {
                for (unsigned channel = 0; channel < 3; ++channel) {
                    rgba[offset + channel] =
                            static_cast<std::uint8_t>((sums[channel] + weight / 2) / weight);
                }
            }
        }
    }
}
} // namespace voyemu::media
