#pragma once
#include "parameters_layout.hpp"
#include "window_layout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace voyemu::media {
// Only an explicit foreground launch may request activation, exactly once.
inline bool activate_window(bool background, bool initial_show) {
    return initial_show && !background;
}

// Visibility selects a drawable; it never gates the live renderer or SDK stream.
inline bool render_enabled(bool stopped) {
    return !stopped;
}

// Foreground activation is authoritative even while AppKit key/occlusion notifications lag.
inline bool render_visible(bool visible, bool minimized, bool unoccluded, bool key,
                           bool active = false) {
    return visible && !minimized && (unoccluded || key || active);
}

struct TouchPoint {
    unsigned display;
    int x, y;
};

inline std::optional<TouchPoint> touch_point(double x, double y, double width, double height) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height) {
        return std::nullopt;
    }

    const auto px = std::clamp(static_cast<int>(x * 3840 / width), 0, 3839);

    return TouchPoint{unsigned(px / 1920), px % 1920,
                      std::clamp(static_cast<int>(y * 720 / height), 0, 719)};
}

inline unsigned canvas_height(bool) {
    return 1440u;
}

inline unsigned capture_stride(unsigned width) {
    return ((width * 4 + 255) / 256) * 256;
}

struct CanvasGeometry {
    unsigned pixel_width, pixel_height;
    double content_width, content_height;
    VoyemuWindowRect media_pixels;
};

inline std::optional<CanvasGeometry> canvas_geometry(unsigned percent, double backing, bool) {
    if ((percent != 50 && percent != 75 && percent != 100) || !std::isfinite(backing) ||
        backing <= 0) {
        return std::nullopt;
    }

    const unsigned media_width = 3840 * percent / 100, media_height = 720 * percent / 100,
                   top = media_height;
    const unsigned width = media_width, height = top + media_height;

    return CanvasGeometry{width,
                          height,
                          width / backing,
                          height / backing,
                          {0, double(top), double(media_width), double(media_height)}};
}

inline std::optional<VoyemuWindowRect> media_row(double width, double height, bool cluster,
                                                 unsigned percent = 100) {
    const auto geometry = canvas_geometry(percent, 1, cluster);

    if (!geometry || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    const auto row = geometry->media_pixels;

    return VoyemuWindowRect{row.x * width / geometry->pixel_width,
                            row.y * height / geometry->pixel_height,
                            row.width * width / geometry->pixel_width,
                            row.height * height / geometry->pixel_height};
}

// Tight visible bounds of the pinned 1920x720 logo, centered on one Android screen; the host
// paints the black row.
inline constexpr std::array<float, 4> splash_uv{681.f / 1920, 144.f / 720, 1239.f / 1920,
                                                576.f / 720};

inline std::optional<VoyemuWindowRect> splash_rect(double width, double height, bool cluster,
                                                   unsigned display, unsigned percent = 100) {
    const auto row = media_row(width, height, cluster, percent);

    if (!row || display > 1) {
        return std::nullopt;
    }

    const double screen = row->width / 2, logo_height = row->height * .6,
                 logo_width = logo_height * 558 / 432;

    return VoyemuWindowRect{row->x + display * screen + (screen - logo_width) / 2,
                            row->y + (row->height - logo_height) / 2, logo_width, logo_height};
}

inline std::optional<std::pair<double, double>>
ui_point(double x, double y, double width, double height, bool cluster, unsigned percent = 100) {
    const auto geometry = canvas_geometry(percent, 1, cluster);

    if (!geometry || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    return std::pair{x * voyemu::cluster::ui_design_width(percent) / width,
                     y * voyemu::cluster::ui_design_height(percent) / height};
}

inline std::optional<std::pair<double, double>>
view_point(double x, double y, double width, double height, bool cluster, unsigned percent = 100) {
    const auto geometry = canvas_geometry(percent, 1, cluster);

    if (!geometry || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    return std::pair{x * width / voyemu::cluster::ui_design_width(percent),
                     y * height / voyemu::cluster::ui_design_height(percent)};
}

inline VoyemuWindowRect preserve_top_left(VoyemuWindowRect before, VoyemuWindowRect resized) {
    resized.x = before.x;
    resized.y = before.y + before.height - resized.height;

    return resized;
}

inline std::array<float, 4> display_quad(unsigned display, bool cluster, unsigned percent = 100) {
    const auto geometry = canvas_geometry(percent, 1, cluster);

    if (!geometry || display > 1) {
        return {};
    }

    const float width = float(geometry->media_pixels.width / geometry->pixel_width);

    return {display * width - 1, -1, width,
            float(2 * geometry->media_pixels.height / geometry->pixel_height)};
}

inline std::optional<TouchPoint> canvas_touch_point(double x, double y, double width, double height,
                                                    bool cluster, unsigned percent = 100) {
    const auto row = media_row(width, height, cluster, percent);

    if (!row) {
        return std::nullopt;
    }

    return touch_point(x - row->x, y - row->y, row->width, row->height);
}

inline bool scene_changed(std::span<const std::uint8_t> before,
                          std::span<const std::uint8_t> after) {
    if (before.size() != static_cast<std::size_t>(1920u * 720u) * 4u ||
        after.size() != before.size()) {
        return false;
    }

    unsigned changed = 0, total = 0;

    for (unsigned y = 56; y < 720; y += 8) {
        for (unsigned x = 104; x < 1920; x += 8) {
            const auto offset = (y * 1920 + x) * 4;
            int difference = 0;

            for (unsigned c = 0; c < 3; ++c) {
                difference += std::abs(int(before[offset + c]) - int(after[offset + c]));
            }

            changed += difference > 24;
            ++total;
        }
    }

    return changed * 12 >
           total; // Excludes OEM bars/sidebar and rejects small clock/blinker changes.
}

// The screens window shows the screens outside the main window side by side at their own size,
// scaled like the main window, and centres each one vertically in the row.
struct ScreenSize {
    unsigned width, height;
};
struct ScreensCanvas {
    unsigned pixel_width, pixel_height;
    std::vector<VoyemuWindowRect> screens;
};

inline std::optional<ScreensCanvas> screens_canvas(std::span<const ScreenSize> sizes,
                                                   unsigned percent) {
    if ((percent != 50 && percent != 75 && percent != 100) || sizes.empty()) {
        return std::nullopt;
    }

    ScreensCanvas canvas{0, 0, {}};

    for (const auto size : sizes) {
        if (size.width == 0 || size.height == 0 || size.width > 4096 || size.height > 4096) {
            return std::nullopt;
        }

        canvas.pixel_height = std::max(canvas.pixel_height, size.height * percent / 100);
    }

    for (const auto size : sizes) {
        const unsigned width = size.width * percent / 100, height = size.height * percent / 100;
        canvas.screens.push_back({double(canvas.pixel_width),
                                  double(canvas.pixel_height - height) / 2, double(width),
                                  double(height)});
        canvas.pixel_width += width;
    }

    return canvas;
}

// A point of the screens window, `width` x `height` pixels, in the pixels of the screen under it.
struct ScreenTouch {
    std::size_t screen;
    int x, y;
};

inline std::optional<ScreenTouch> screens_touch_point(double x, double y, double width,
                                                      double height, const ScreensCanvas &canvas,
                                                      std::span<const ScreenSize> sizes) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0 || sizes.size() != canvas.screens.size()) {
        return std::nullopt;
    }

    const double cx = x * canvas.pixel_width / width, cy = y * canvas.pixel_height / height;

    for (std::size_t i = 0; i < canvas.screens.size(); ++i) {
        const auto &rect = canvas.screens[i];

        if (cx >= rect.x && cx < rect.x + rect.width && cy >= rect.y && cy < rect.y + rect.height) {
            const auto size = sizes[i];

            return ScreenTouch{i,
                               std::clamp(int((cx - rect.x) * size.width / rect.width), 0,
                                          int(size.width) - 1),
                               std::clamp(int((cy - rect.y) * size.height / rect.height), 0,
                                          int(size.height) - 1)};
        }
    }

    return std::nullopt;
}

// A drag stays on the screen it started on and is pinned to its edges.
inline std::optional<ScreenTouch> screens_drag_point(double x, double y, double width,
                                                     double height, const ScreensCanvas &canvas,
                                                     std::span<const ScreenSize> sizes,
                                                     std::size_t screen) {
    if (screen >= canvas.screens.size() || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0) {
        return std::nullopt;
    }

    const auto &rect = canvas.screens[screen];
    const double sx = width / canvas.pixel_width, sy = height / canvas.pixel_height;

    return screens_touch_point(std::clamp(x, rect.x * sx, (rect.x + rect.width) * sx - .01),
                               std::clamp(y, rect.y * sy, (rect.y + rect.height) * sy - .01), width,
                               height, canvas, sizes);
}

inline VoyemuWindowRect restore_position(VoyemuWindowRect screen, VoyemuWindowRect window,
                                         std::optional<std::pair<double, double>> offsets) {
    if (!offsets || !std::isfinite(offsets->first) || !std::isfinite(offsets->second)) {
        return voyemu_media_position(screen, window);
    }

    window.x = std::clamp(screen.x + offsets->first, screen.x - window.width + 120,
                          screen.x + screen.width - 120);
    window.y = std::clamp(screen.y + screen.height - window.height - offsets->second,
                          screen.y - window.height + 28, screen.y + screen.height - window.height);

    return window;
}
} // namespace voyemu::media
