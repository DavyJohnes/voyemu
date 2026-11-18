#pragma once
#include "imgui.h"
#include <vector>

namespace voyemu::cluster {
// ImGui triangulates the source contour, then moves fill vertices inward for AA.
// That inset can fold thin triangles across a concave face. Keep the fill on the
// exact source contour and use only the outward transparent half of its AA fringe.
inline void draw_wheel_hover(ImDrawList &draw, const std::vector<ImVec2> &points, ImU32 color) {
    if (points.size() < 3 || !(color & IM_COL32_A_MASK)) {
        return;
    }

    const int first_vertex = draw.VtxBuffer.Size;
    draw.AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), color);

    if (!(draw.Flags & ImDrawListFlags_AntiAliasedFill)) {
        return;
    }

    for (std::size_t i = 0; i < points.size(); ++i) {
        draw.VtxBuffer[first_vertex + static_cast<int>(i) * 2].pos = points[i];
    }
}
} // namespace voyemu::cluster
