#include "texture_pixels.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <exception>
#include <iostream>

namespace {
double edge_over_background(const std::array<std::uint8_t, 8> &pixels, double background) {
    const double color = (pixels[0] + pixels[4]) / 2.0;
    const double alpha = (pixels[3] + pixels[7]) / (2.0 * 255);

    return color * alpha + background * (1 - alpha);
}
} // namespace

int main() try {
    std::array<std::uint8_t, 8> edge{255, 255, 255, 255, 0, 0, 0, 0};

    assert(edge_over_background(edge, 241) < 241); // Existing sampler paints a dark fringe.
    voyemu::media::fill_transparent_rgb(edge, 2, 1);

    if (std::abs(edge_over_background(edge, 241) - 248) > 1e-6) {
        std::cerr << "transparent RGB produces a dark interpolation fringe\n";

        return 1;
    }

    assert(edge[3] == 255 && edge[7] == 0);
    const auto once = edge;
    voyemu::media::fill_transparent_rgb(edge, 2, 1);
    assert(edge == once);

    std::array<std::uint8_t, 12> weighted{100, 0, 0, 128, 0, 0, 0, 0, 0, 100, 0, 64};
    const auto before = weighted;
    voyemu::media::fill_transparent_rgb(weighted, 3, 1);
    assert(weighted[4] == 67 && weighted[5] == 33 && weighted[6] == 0 && weighted[7] == 0);

    for (unsigned i : {0u, 1u, 2u, 3u, 8u, 9u, 10u, 11u}) {
        assert(weighted[i] == before[i]);
    }

    std::array<std::uint8_t, 16> corner{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 40, 60, 255};
    voyemu::media::fill_transparent_rgb(corner, 2, 2);
    assert(corner[0] == 20 && corner[1] == 40 && corner[2] == 60 && corner[3] == 0);

    for (std::size_t i = 0; i < corner.size(); i += 4) {
        assert(corner[i + 3] == (i == 12 ? 255 : 0));
    }

    std::array<std::uint8_t, 16> empty{};
    voyemu::media::fill_transparent_rgb(empty, 2, 2);
    assert((empty == std::array<std::uint8_t, 16>{}));

    for (const auto dimensions :
         std::array<std::array<std::size_t, 2>, 4>{{{0, 2}, {2, 0}, {1025, 1}, {1, 1025}}}) {
        bool rejected = false;
        try {
            voyemu::media::fill_transparent_rgb(edge, dimensions[0], dimensions[1]);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }

        assert(rejected);
    }

    bool rejected = false;
    try {
        voyemu::media::fill_transparent_rgb(edge, 1, 1);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }

    assert(rejected);
    std::cout << "texture transparent-edge guards: pass\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
