#pragma once
#include "parameters_layout.hpp"
#include <array>
namespace voyemu::cluster {
struct RadioPatch {
    ParameterRect bounds;
    std::array<float, 4> uv;
};

// At an extreme, the selected end cap replaces the track cap. Painting both
// antialiased masks at the same outside edge leaves a visible white fringe.
inline ParameterRect radio_background_clip(ParameterRect track, float selected_left,
                                           float cell_width) {
    float left = track.x, right = track.right();

    if (selected_left <= track.x) {
        left = std::clamp(selected_left + cell_width / 2, track.x, track.right());
    }

    if (selected_left + cell_width >= track.right()) {
        right = std::clamp(selected_left + cell_width / 2, left, track.right());
    }

    return {left, track.y, right - left, track.height};
}

// OEM 94x70 PNG npTc: xDivs46,48 and yDivs34,35, measured in physical pixels.
inline std::array<RadioPatch, 9> radio_patches(ParameterRect target, float source_pixel_scale) {
    // Height is a uniform transform of the OEM mask; stretching only its
    // one-pixel middle band produces a flat/kinked cap at non-native heights.
    const float sx = std::min(source_pixel_scale, target.width / 92), sy = target.height / 70;
    const std::array<float, 4> x = {target.x, target.x + 46 * sx, target.right() - 46 * sx,
                                    target.right()};
    const std::array<float, 4> y = {target.y, target.y + 34 * sy, target.y + 35 * sy,
                                    target.bottom()};
    constexpr std::array<float, 4> u = {0, 46.f / 94, 48.f / 94, 1},
                                   v = {0, 34.f / 70, 35.f / 70, 1};
    std::array<RadioPatch, 9> result{};

    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            result[row * 3 + column] = {
                    {x[column], y[row], x[column + 1] - x[column], y[row + 1] - y[row]},
                    {u[column], v[row], u[column + 1], v[row + 1]}};
        }
    }

    return result;
}
} // namespace voyemu::cluster
