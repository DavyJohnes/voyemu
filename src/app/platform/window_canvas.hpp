#pragma once
#include "media_window.hpp"
namespace voyemu::media {
struct WindowCanvas {
    double design_width, design_height, logical_width, logical_height, scale_x, scale_y;
    VoyemuWindowRect content;
};

inline std::optional<WindowCanvas> window_canvas(unsigned zoom, bool cluster, double width,
                                                 double height) {
    if (!canvas_geometry(zoom, 1, cluster) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0) {
        return std::nullopt;
    }

    const double design_width = voyemu::cluster::ui_design_width(zoom),
                 design_height = voyemu::cluster::ui_design_height(zoom);
    const double scale = std::min(width / design_width, height / design_height);

    return WindowCanvas{design_width,
                        design_height,
                        width / scale,
                        height / scale,
                        scale,
                        scale,
                        {0, 0, design_width * scale, design_height * scale}};
}

inline std::optional<TouchPoint> window_canvas_touch(double x, double y, double width,
                                                     double height, bool cluster, unsigned zoom) {
    const auto canvas = window_canvas(zoom, cluster, width, height);

    if (!canvas) {
        return std::nullopt;
    }

    return canvas_touch_point(x, y, canvas->content.width, canvas->content.height, cluster, zoom);
}
} // namespace voyemu::media
