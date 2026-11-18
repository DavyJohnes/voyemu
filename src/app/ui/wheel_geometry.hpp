#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace voyemu::cluster::wheel_geometry {
inline constexpr std::size_t maximum_vertices = 1024;
inline constexpr double coordinate_epsilon = 1e-10;

template <class Point> double cross(Point a, Point b, Point p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

template <class Point> bool on_segment(Point a, Point b, Point p) {
    return std::abs(cross(a, b, p)) <= coordinate_epsilon &&
           p.x >= std::min(a.x, b.x) - coordinate_epsilon &&
           p.x <= std::max(a.x, b.x) + coordinate_epsilon &&
           p.y >= std::min(a.y, b.y) - coordinate_epsilon &&
           p.y <= std::max(a.y, b.y) + coordinate_epsilon;
}

template <class Point>
bool contains(const std::vector<Point> &polygon, Point point, bool include_boundary = true) {
    if (polygon.size() < 3 || !std::isfinite(point.x) || !std::isfinite(point.y)) {
        return false;
    }

    bool inside = false;

    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto a = polygon[j], b = polygon[i];

        if (on_segment(a, b, point)) {
            return include_boundary;
        }

        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }

    return inside;
}

template <class Point>
bool contains(const std::array<double, 4> &rect, const std::vector<Point> &polygon, double x,
              double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || rect[2] <= 0 || rect[3] <= 0) {
        return false;
    }

    const Point point{(x - rect[0]) / rect[2], (y - rect[1]) / rect[3]};

    if (point.x < -coordinate_epsilon || point.x > 1 + coordinate_epsilon ||
        point.y < -coordinate_epsilon || point.y > 1 + coordinate_epsilon) {
        return false;
    }

    return contains(polygon, point);
}

template <class Point> bool intersects(Point a, Point b, Point c, Point d) {
    const double ab_c = cross(a, b, c), ab_d = cross(a, b, d), cd_a = cross(c, d, a),
                 cd_b = cross(c, d, b);

    if (((ab_c > coordinate_epsilon && ab_d < -coordinate_epsilon) ||
         (ab_c < -coordinate_epsilon && ab_d > coordinate_epsilon)) &&
        ((cd_a > coordinate_epsilon && cd_b < -coordinate_epsilon) ||
         (cd_a < -coordinate_epsilon && cd_b > coordinate_epsilon))) {
        return true;
    }

    return on_segment(a, b, c) || on_segment(a, b, d) || on_segment(c, d, a) || on_segment(c, d, b);
}

// Face paths are simple, clockwise contours in screen coordinates. Rectangles and
// self-intersections are never substituted for invalid paths.
template <class Point> bool valid_polygon(const std::vector<Point> &polygon) {
    if (polygon.size() < 3 || polygon.size() > maximum_vertices) {
        return false;
    }

    double twice_area = 0;

    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];

        if (!std::isfinite(a.x) || !std::isfinite(a.y) || a.x < 0 || a.x > 1 || a.y < 0 ||
            a.y > 1 ||
            (std::abs(a.x - b.x) <= coordinate_epsilon &&
             std::abs(a.y - b.y) <= coordinate_epsilon)) {
            return false;
        }

        twice_area += a.x * b.y - b.x * a.y;

        for (std::size_t j = i + 1; j < polygon.size(); ++j) {
            if (j == i + 1 || (i == 0 && j == polygon.size() - 1)) {
                continue;
            }

            if (intersects(a, b, polygon[j], polygon[(j + 1) % polygon.size()])) {
                return false;
            }
        }
    }

    return twice_area > coordinate_epsilon;
}

template <class Region>
const Region *hit(const std::vector<Region> &regions, std::string_view photo, double x, double y) {
    const Region *selected = nullptr;

    for (const auto &region : regions) {
        if (region.photo == photo && region.contains(x, y)) {
            // An exact shared virtual divider is deliberately inactive instead of dispatching two
            // actions.
            if (selected) {
                return nullptr;
            }

            selected = &region;
        }
    }

    return selected;
}

inline int detent_count(double delta, bool over_roller, bool has_held_button) {
    if (!over_roller || has_held_button || !std::isfinite(delta) || delta == 0) {
        return 0;
    }

    return static_cast<int>(std::ceil(std::min(8.0, std::abs(delta))));
}
} // namespace voyemu::cluster::wheel_geometry
