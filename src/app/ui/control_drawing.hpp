#pragma once
#include "imgui.h"
#include "parameters_layout.hpp"
#include <array>
#include <cmath>
namespace voyemu::cluster {
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

struct ColorStop {
    float fraction;
    ImU32 color;
};

// The colour at a fraction of a vertical gradient given by ascending stops.
template <std::size_t N>
ImU32 gradient_color(const std::array<ColorStop, N> &stops, float fraction) {
    fraction = std::clamp(fraction, stops.front().fraction, stops.back().fraction);
    std::size_t upper = 1;

    while (upper + 1 < N && stops[upper].fraction < fraction) {
        ++upper;
    }

    const auto &a = stops[upper - 1], &b = stops[upper];
    const float t = b.fraction > a.fraction ? (fraction - a.fraction) / (b.fraction - a.fraction)
                                            : 0;
    ImU32 result = 0;

    for (unsigned shift = 0; shift < 32; shift += 8) {
        const float from = float((a.color >> shift) & 0xff), to = float((b.color >> shift) & 0xff);
        result |= ImU32(std::lround(from + (to - from) * t)) << shift;
    }

    return result;
}

// The selected radio segment: the flat light accent, or the dark blue gradient lit from the top.
inline ImU32 radio_selection_color(bool light, float fraction) {
    static constexpr std::array<ColorStop, 3> dark{{{0, IM_COL32(85, 162, 239, 255)},
                                                    {.5f, IM_COL32(38, 129, 221, 255)},
                                                    {1, IM_COL32(25, 117, 210, 255)}}};

    return light ? IM_COL32(66, 158, 253, 255) : gradient_color(dark, fraction);
}

// The toggle switch: a 41.5x21 track with a round knob that travels 19 points when on.
struct ToggleInk {
    ImU32 track, border, on;
    float knob;
};

inline ToggleInk toggle_ink(bool light) {
    return light ? ToggleInk{IM_COL32(213, 222, 236, 255), 0, IM_COL32(64, 153, 243, 255), 17}
                 : ToggleInk{IM_COL32(128, 135, 149, 36), IM_COL32(70, 79, 85, 135),
                             IM_COL32(64, 153, 243, 255), 20};
}

// The white knob: a flat light disc, or a dark disc shaded like brushed metal.
inline ImU32 toggle_knob_color(bool light, float fraction) {
    static constexpr std::array<ColorStop, 3> metal{{{0, IM_COL32(255, 255, 255, 238)},
                                                     {.5f, IM_COL32(255, 255, 255, 130)},
                                                     {1, IM_COL32(255, 255, 255, 245)}}};

    return light ? IM_COL32(255, 255, 255, 242) : gradient_color(metal, fraction);
}

// The light panel background behind both control columns: a pale sky over a lilac floor.
inline constexpr std::array<ColorStop, 9> light_background{
        {{0, IM_COL32(166, 197, 233, 255)},
         {100.f / 720, IM_COL32(203, 223, 246, 255)},
         {180.f / 720, IM_COL32(218, 232, 250, 255)},
         {210.f / 720, IM_COL32(219, 230, 247, 255)},
         {220.f / 720, IM_COL32(230, 233, 246, 255)},
         {240.f / 720, IM_COL32(222, 225, 243, 255)},
         {400.f / 720, IM_COL32(189, 195, 222, 255)},
         {600.f / 720, IM_COL32(148, 154, 186, 255)},
         {1, IM_COL32(129, 136, 168, 255)}}};
} // namespace voyemu::cluster
