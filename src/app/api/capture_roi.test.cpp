#include "capture_roi.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace voyemu::api;

int main() try {
    const std::array<std::uint8_t, 28> padded{1,  2,   3,  0,   5,  6,  7,  17, 9,  10,
                                              11, 255, 99, 98,  97, 96, 13, 14, 15, 93,
                                              17, 18,  19, 176, 21, 22, 23, 255};
    const auto full = crop_frame(padded, 3, 2, 16, {0, 0, 3, 2});
    const std::vector<std::uint8_t> packed{1,  2,  3,  0,  5,  6,  7,  17,  9,  10, 11, 255,
                                           13, 14, 15, 93, 17, 18, 19, 176, 21, 22, 23, 255};

    assert(full.pixels == packed && !full.bgra);
    assert(crop_frame(packed, 3, 2, 12, {0, 0, 3, 2}).pixels == packed);
    const auto roi = crop_frame(padded, 3, 2, 16, {1, 0, 2, 2}, true);
    assert((roi.pixels == std::vector<std::uint8_t>{5, 6, 7, 17, 9, 10, 11, 255, 17, 18, 19, 176,
                                                    21, 22, 23, 255}));
    assert(roi.bgra && roi.rect.x == 1 && roi.rect.y == 0 && roi.rect.width == 2 &&
           roi.rect.height == 2);
    assert((crop_frame(padded, 3, 2, 16, {2, 1, 1, 1}).pixels ==
            std::vector<std::uint8_t>{21, 22, 23, 255}));
    auto rejects = [&](std::span<const std::uint8_t> bytes, std::size_t w, std::size_t h,
                       std::size_t stride, PixelRect rect) {
        bool rejected = false;
        try {
            crop_frame(bytes, w, h, stride, rect);
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        assert(rejected);
    };

    for (const auto rect :
         std::array<PixelRect, 12>{{{-1, 0, 1, 1},
                                    {0, -1, 1, 1},
                                    {0, 0, 0, 1},
                                    {0, 0, 1, 0},
                                    {0, 0, -1, 1},
                                    {0, 0, 1, -1},
                                    {3, 0, 1, 1},
                                    {0, 2, 1, 1},
                                    {2, 0, 2, 1},
                                    {0, 1, 1, 2},
                                    {1, 0, std::numeric_limits<std::int64_t>::max(), 1},
                                    {0, 1, 1, std::numeric_limits<std::int64_t>::max()}}}) {
        rejects(padded, 3, 2, 16, rect);
    }

    rejects(padded, 0, 2, 16, {0, 0, 1, 1});
    rejects(padded, 3, 0, 16, {0, 0, 1, 1});
    rejects(padded, 3841, 1, 16, {0, 0, 1, 1});
    rejects(padded, 1, 2161, 16, {0, 0, 1, 1});
    rejects(padded, std::numeric_limits<std::size_t>::max(), 2, 16, {0, 0, 1, 1});
    rejects(padded, 3, 2, 11, {0, 0, 1, 1});
    rejects(std::span(padded).first(27), 3, 2, 16, {0, 0, 1, 1});
    rejects({}, 1, 1, 4, {0, 0, 1, 1});
    rejects(padded, 1, 3, std::numeric_limits<std::size_t>::max(), {0, 0, 1, 1});
    rejects(padded, 1, 2, std::numeric_limits<std::size_t>::max(), {0, 0, 1, 1});
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
