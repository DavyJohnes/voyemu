#include "wheel_geometry.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Point {
    double x{}, y{};
};
struct Region {
    std::string photo;
    std::array<double, 4> rect;
    std::vector<Point> polygon;

    bool contains(double x, double y) const {
        return voyemu::cluster::wheel_geometry::contains(rect, polygon, x, y);
    }
};

void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() try {
    using namespace voyemu::cluster::wheel_geometry;
    const std::vector<Point> square{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const std::vector<Point> concave{{0, 0}, {1, 0}, {1, .3}, {.3, .3}, {.3, 1}, {0, 1}};
    check(valid_polygon(square) && valid_polygon(concave), "simple clockwise face paths");
    check(!valid_polygon(std::vector<Point>{{0, 0}, {0, 1}, {1, 1}, {1, 0}}),
          "reversed winding rejected");
    check(!valid_polygon(std::vector<Point>{{0, 0}, {1, 1}, {1, 0}, {0, 1}}),
          "crossed face rejected");
    check(!valid_polygon(std::vector<Point>{{0, 0}, {1, 0}, {1, 0}, {0, 1}}), "zero edge rejected");
    check(!valid_polygon(std::vector<Point>{{0, 0}, {1, 0}, {0, 0}}), "zero area rejected");
    check(!valid_polygon(std::vector<Point>{
                  {0, 0}, {1, 0}, {1, std::numeric_limits<double>::quiet_NaN()}}),
          "nonfinite face rejected");
    check(contains(concave, Point{.15, .5}) && !contains(concave, Point{.5, .5}),
          "concavity never fills a panel gap");

    for (const auto point : std::vector<Point>{{0, 0}, {1, 0}, {1, .5}, {.5, 1}, {0, 1}, {0, .5}}) {
        check(contains(square, point), "all face vertices and edges included");
        check(!contains(square, point, false), "strict interior excludes face boundary");
    }

    check(!contains(square, Point{1.00001, .5}) && !contains(square, Point{.5, -.00001}),
          "points outside face cannot hover");
    check(!contains(square, Point{std::numeric_limits<double>::infinity(), .5}),
          "nonfinite mouse cannot hover");
    const Region shifted{"left", {.2, .3, .4, .5}, square};
    check(shifted.contains(.4, .55) && shifted.contains(.2, .3) && !shifted.contains(.199, .4),
          "hit bounds and polygon share one transform");
    const std::vector<Region> adjacent{{"left", {0, 0, .5, 1}, square},
                                       {"left", {.5, 0, .5, 1}, square},
                                       {"right", {0, 0, 1, 1}, square}};
    check(hit(adjacent, "left", .25, .5) == &adjacent[0] &&
                  hit(adjacent, "left", .75, .5) == &adjacent[1],
          "correct virtual plate side");
    check(!hit(adjacent, "left", .5, .5), "shared divider cannot dispatch two buttons");
    check(hit(adjacent, "right", .5, .5) == &adjacent[2],
          "left and right wheel remain independent");
    check(!hit(adjacent, "left", 1.01, .5), "outside button group has no action");
    check(detent_count(.1, true, false) == 1 && detent_count(-2.4, true, false) == 3 &&
                  detent_count(1000, true, false) == 8,
          "roller phases remain bounded and signed separately");
    check(detent_count(0, true, false) == 0 && detent_count(1, false, false) == 0 &&
                  detent_count(1, true, true) == 0,
          "roller gaps and held presses suppress scrolling");
    check(detent_count(std::numeric_limits<double>::infinity(), true, false) == 0 &&
                  detent_count(std::numeric_limits<double>::quiet_NaN(), true, false) == 0,
          "invalid scroll cannot convert to an integer");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
