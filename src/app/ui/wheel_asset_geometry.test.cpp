#include "cluster_ui.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "wheel_geometry.hpp"
#include "wheel_hover.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace voyemu;
using namespace voyemu::cluster;
namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void verify_hover_fill(const Region &region, bool antialias = false, float origin = 959.63556f) {
    std::vector<Point> contour;
    std::vector<ImVec2> vertices;

    for (const auto point : region.polygon) {
        // Use native screen coordinates and float vertices, as the hover draw does.
        const float x = origin +
                        static_cast<float>((region.rect[0] + point.x * region.rect[2]) * 194.4);
        const float y = 180.0f +
                        static_cast<float>((region.rect[1] + point.y * region.rect[3]) * 166.32);
        vertices.emplace_back(x, y);
        contour.push_back({x, y});
    }

    double polygon_area = 0;

    for (std::size_t i = 1; i + 1 < contour.size(); ++i) {
        polygon_area += wheel_geometry::cross(contour[0], contour[i], contour[i + 1]) / 2;
    }

    check(polygon_area > 0, "hover contour must be clockwise: " + region.id);
    ImDrawListSharedData shared;
    ImDrawList draw(&shared);
    draw._ResetForNewFrame();

    if (antialias) {
        draw.Flags |= ImDrawListFlags_AntiAliasedFill;
    }

    draw_wheel_hover(draw, vertices, IM_COL32_WHITE);
    check(draw.IdxBuffer.Size == static_cast<int>((vertices.size() - 2) * 3 +
                                                  (antialias ? vertices.size() * 6 : 0)),
          "every hover contour produces a complete triangulation: " + region.id);

    if (antialias) {
        // The AA fringe meets the exact source contour, with no inward vertex displacement.
        contour.clear();

        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto point = draw.VtxBuffer[static_cast<int>(i) * 2].pos;
            contour.push_back({point.x, point.y});
            check(point.x == vertices[i].x && point.y == vertices[i].y,
                  "AA must not inset the triangulated source: " + region.id);
            check((draw.VtxBuffer[static_cast<int>(i) * 2 + 1].col & IM_COL32_A_MASK) == 0,
                  "AA fringe retains transparent outer vertices: " + region.id);
        }

        polygon_area = 0;

        for (std::size_t i = 1; i + 1 < contour.size(); ++i) {
            polygon_area += wheel_geometry::cross(contour[0], contour[i], contour[i + 1]) / 2;
        }
    }

    double triangles_area = 0;

    for (int i = 0; i < static_cast<int>((vertices.size() - 2) * 3); i += 3) {
        const auto a = draw.VtxBuffer[draw.IdxBuffer[i]].pos,
                   b = draw.VtxBuffer[draw.IdxBuffer[i + 1]].pos,
                   c = draw.VtxBuffer[draw.IdxBuffer[i + 2]].pos;
        const Point first{a.x, a.y}, second{b.x, b.y}, third{c.x, c.y};
        const double area = wheel_geometry::cross(first, second, third) / 2;
        check(area >= -1e-8, "hover triangulation cannot reverse a triangle: " + region.id);

        if (area > 1e-8) {
            const Point center{(first.x + second.x + third.x) / 3,
                               (first.y + second.y + third.y) / 3};
            check(wheel_geometry::contains(contour, center),
                  "hover triangle paints outside its concave source: " + region.id +
                          " origin=" + std::to_string(origin) + " area=" + std::to_string(area) +
                          " center=" + std::to_string(center.x) + "," + std::to_string(center.y));
        }

        triangles_area += std::abs(area);
    }

    check(std::abs(triangles_area - polygon_area) <= polygon_area * 1e-5,
          "hover triangles preserve the exact contour area: " + region.id);

    if (!antialias && (region.id == "custom" || region.id == "phone")) {
        double convex_fan_area = 0;

        for (std::size_t i = 1; i + 1 < contour.size(); ++i) {
            convex_fan_area +=
                    std::abs(wheel_geometry::cross(contour[0], contour[i], contour[i + 1])) / 2;
        }

        check(convex_fan_area > polygon_area + 100,
              "regression fixture must expose the diagonal convex-fill artifact: " + region.id);
    }
}

void verify(const std::filesystem::path &assets) {
    std::ifstream source(assets / "wheel-hotspots.json");
    Json document;

    source >> document;
    const auto geometry = read_wheel_geometry(assets / "wheel-hotspots.json");
    check(geometry.regions.size() == 17 && geometry.rollers.size() == 2,
          "all seventeen native actions and both physical rollers");
    std::set<std::string> ids;

    for (const auto &region : geometry.regions) {
        ids.insert(region.id);

        for (float origin :
             {479.63556f, 620.03556f, 719.63556f, 860.03556f, 959.63556f, 1100.03556f}) {
            for (bool antialias : {false, true}) {
                verify_hover_fill(region, antialias, origin);
            }
        }

        const Json *fixture = nullptr;

        for (const auto &entry : document.at("regions")) {
            if (entry.at("id") == region.id) {
                fixture = &entry;
            }
        }

        check(fixture, "missing source fixture: " + region.id);
        const auto interior = fixture->at("interior_point_normalized").get<std::array<double, 2>>();
        check(wheel_geometry::hit(geometry.regions, region.photo, interior[0], interior[1]) ==
                      &region,
              "exclusive source fixture: " + region.id);
        check(!region.contains(-1, -1) && !region.contains(2, 2),
              "outside photo cannot hit: " + region.id);

        for (std::size_t index = 0; index < region.polygon.size(); ++index) {
            const auto a = region.polygon[index],
                       b = region.polygon[(index + 1) % region.polygon.size()];
            const Point first{(region.rect[0] + a.x * region.rect[2]) * 180,
                              (region.rect[1] + a.y * region.rect[3]) * 154};
            const Point last{(region.rect[0] + b.x * region.rect[2]) * 180,
                             (region.rect[1] + b.y * region.rect[3]) * 154};
            const Point midpoint{(first.x + last.x) / 2, (first.y + last.y) / 2};
            const double length = std::hypot(last.x - first.x, last.y - first.y);
            const Point normal{(first.y - last.y) / length * .001,
                               (last.x - first.x) / length * .001};
            check(region.contains(first.x / 180, first.y / 154) &&
                          region.contains(midpoint.x / 180, midpoint.y / 154),
                  "exact overlay edge is hittable: " + region.id);
            check(region.contains((midpoint.x + normal.x) / 180, (midpoint.y + normal.y) / 154),
                  "adjacent interior is hittable: " + region.id);
            check(!region.contains((midpoint.x - normal.x) / 180, (midpoint.y - normal.y) / 154),
                  "adjacent outside stays unhighlighted: " + region.id);
        }
    }

    check(ids.size() == 17, "no duplicate actions");

    for (const auto side : {"left", "right"}) {
        for (int y = 0; y < 308; ++y) {
            for (int x = 0; x < 360; ++x) {
                const double px = (x + .5) / 360, py = (y + .5) / 308;
                std::size_t interiors = 0;

                for (const auto &region : geometry.regions) {
                    if (region.photo != side) {
                        continue;
                    }

                    const Point point{(px - region.rect[0]) / region.rect[2],
                                      (py - region.rect[1]) / region.rect[3]};

                    if (wheel_geometry::contains(region.polygon, point, false)) {
                        ++interiors;
                    }
                }

                check(interiors <= 1, "button interiors overlap in displayed geometry");
            }
        }

        check(!wheel_geometry::hit(geometry.regions, side, 0, 0),
              "background corner cannot press a button");
    }

    for (const auto &roller : geometry.rollers) {
        check((roller.photo == "left" && roller.up == "cruise_up" &&
               roller.down == "cruise_down") ||
                      (roller.photo == "right" && roller.up == "volume_up" &&
                       roller.down == "volume_down"),
              "roller routes match native physical actions");
        check(!roller.contains(0, 0) && !roller.contains(1, 1),
              "roller hit region excludes the plate/background gaps");

        for (int y = 1; y < 40; ++y) {
            for (int x = 1; x < 40; ++x) {
                const Point local{x / 40.0, y / 40.0};

                if (!wheel_geometry::contains(roller.polygon, local, false)) {
                    continue;
                }

                const double px = roller.rect[0] + local.x * roller.rect[2],
                             py = roller.rect[1] + local.y * roller.rect[3];
                std::size_t hits = 0;

                for (const auto &region : geometry.regions) {
                    if (region.photo == roller.photo && region.contains(px, py)) {
                        ++hits;
                        check(roller.photo == "right" && region.id == "ok",
                              "only the right roller has a center press: " + region.id);
                    }
                }

                check(hits <= 1, "hover regions cannot intersect on the physical roller");
                check(hits == (roller.photo == "left" ? 0 : 1),
                      "left rocker has no click press and right roller keeps OK");
                check(wheel_geometry::detent_count(1, roller.contains(px, py), false) == 1,
                      "empty pointer hover seam preserves full roller wheel-detent coverage");
            }
        }
    }
}

void verify_qinggan11(const std::filesystem::path &reference) {
    const auto assets = reference.parent_path() / "free-2024";
    const auto original = read_wheel_geometry(reference / "wheel-hotspots.json");
    const auto current = read_wheel_geometry(assets / "wheel-hotspots.json");

    check(current.joined_offset == original.joined_offset,
          "A11 retains the exact approved A9 join");
    check(current.regions.size() == 15 && current.rollers.size() == 1,
          "A11 exposes fifteen physical faces and one verified rocker");

    for (const auto &region : current.regions) {
        const auto source_id = region.id == "menu" ? "cancel"
                               : region.id == "ok" ? "menu"
                                                   : region.id.c_str();
        const auto source = std::find_if(original.regions.begin(), original.regions.end(),
                                         [&](const auto &value) {
                                             return value.id == source_id;
                                         });
        check(source != original.regions.end() && source->photo == region.photo &&
                      source->rect == region.rect &&
                      source->polygon.size() == region.polygon.size(),
              "A11 shares the approved contour bounds: " + region.id);

        for (std::size_t i = 0; i < region.polygon.size(); ++i) {
            check(region.polygon[i].x == source->polygon[i].x &&
                          region.polygon[i].y == source->polygon[i].y,
                  "A11 reuses every exact A9 contour vertex: " + region.id);
        }

        for (float origin : {479.63556f, 959.63556f, 1100.03556f}) {
            for (bool antialias : {false, true}) {
                verify_hover_fill(region, antialias, origin);
            }
        }
    }

    for (const auto &roller : current.rollers) {
        const auto source = std::find_if(original.rollers.begin(), original.rollers.end(),
                                         [&](const auto &value) {
                                             return value.photo == roller.photo;
                                         });
        check(source != original.rollers.end() && source->rect == roller.rect &&
                      source->polygon.size() == roller.polygon.size(),
              "A11 uses the approved roller bounds");

        for (std::size_t i = 0; i < roller.polygon.size(); ++i) {
            check(roller.polygon[i].x == source->polygon[i].x &&
                          roller.polygon[i].y == source->polygon[i].y,
                  "A11 uses the approved rounded roller contour");
        }

        for (int y = 1; y < 40; ++y) {
            for (int x = 1; x < 40; ++x) {
                const Point local{x / 40.0, y / 40.0};

                if (!wheel_geometry::contains(roller.polygon, local, false)) {
                    continue;
                }

                const double px = roller.rect[0] + local.x * roller.rect[2],
                             py = roller.rect[1] + local.y * roller.rect[3];
                check(!wheel_geometry::hit(current.regions, roller.photo, px, py),
                      "A11 rocker center dispatches no press");
            }
        }
    }

    for (const auto side : {"left", "right"}) {
        for (int y = 0; y < 308; ++y) {
            for (int x = 0; x < 360; ++x) {
                const double px = (x + .5) / 360, py = (y + .5) / 308;
                std::size_t count = 0;

                for (const auto &region : current.regions) {
                    if (region.photo == side && region.contains(px, py)) {
                        ++count;
                    }
                }

                check(count <= 1, "A11 hover contours do not overlap");
            }
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "test requires generated contour asset path");
        verify(argv[1]);
        verify_qinggan11(argv[1]);
        std::cout << "wheel asset geometry guards: pass\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "wheel asset geometry guards: " << error.what() << '\n';

        return 1;
    }
}
