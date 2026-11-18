#include "media_window.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <vector>

int main() try {
    using namespace voyemu::media;
    assert(activate_window(false, true));
    assert(!activate_window(true, true));
    assert(!activate_window(false, false));
    assert(!activate_window(true, false));
    VoyemuWindowRect monitor{0, 0, 1512, 944}, window{0, 0, 1920, 388};
    auto first = restore_position(monitor, window, std::nullopt);
    assert(first.x == 0 && first.y == 556 && first.width == 1920);
    auto saved = restore_position(monitor, window, {{-970, 40}});
    assert(saved.x == -970 && saved.y == 516);
    auto visible = restore_position(monitor, window, {{-90000, 90000}});
    assert(visible.x == -1800 && visible.y == -360);
    auto moved = restore_position({-1920, 100, 1920, 1080}, window, {{200, 20}});
    assert(moved.x == -1720 && moved.y == 772);
    auto primary = touch_point(959.9, 180, 1920, 360);
    assert(primary && primary->display == 0 && primary->x == 1919 && primary->y == 360);
    auto secondary = touch_point(960, 0, 1920, 360);
    assert(secondary && secondary->display == 1 && secondary->x == 0 && secondary->y == 0);
    auto dpi = touch_point(1280, 240, 2560, 480);
    assert(dpi && dpi->display == 1 && dpi->x == 0 && dpi->y == 360);
    assert(!touch_point(-1, 0, 1920, 360) && !touch_point(1920, 0, 1920, 360) &&
           !touch_point(NAN, 0, 1920, 360));
    assert(canvas_height(false) == 1440 && canvas_height(true) == 1440);

    for (unsigned scale : {50u, 75u, 100u}) {
        const auto on = canvas_geometry(scale, 2, true), off = canvas_geometry(scale, 2, false);
        assert(on && off && on->pixel_height == off->pixel_height &&
               on->media_pixels.y == off->media_pixels.y);
        assert(ui_point(100, 100, 960, 360, true, scale) ==
               ui_point(100, 100, 960, 360, false, scale));
        assert(view_point(100, 100, 960, 360, true, scale) ==
               view_point(100, 100, 960, 360, false, scale));
    }

    assert(capture_stride(3840) == 15360 && capture_stride(2296) == 9216 &&
           capture_stride(1920) == 7680);

    for (const double backing : {1., 1.5, 2.}) {
        for (const unsigned scale : {50u, 75u, 100u}) {
            const auto geometry = canvas_geometry(scale, backing, true);
            assert(geometry);
            assert(geometry->pixel_width == 3840u * scale / 100 &&
                   geometry->pixel_height == 1440u * scale / 100);
            assert(geometry->content_width * backing == geometry->pixel_width &&
                   geometry->content_height * backing == geometry->pixel_height);
            const auto point = view_point(1116, 102, geometry->content_width,
                                          geometry->content_height, true, scale);
            assert(point);
            const auto canonical = ui_point(point->first, point->second, geometry->content_width,
                                            geometry->content_height, true, scale);
            assert(canonical);
            assert(std::abs(canonical->first - 1116) < 1e-9 &&
                   std::abs(canonical->second - 102) < 1e-9);
            assert(std::abs(point->first * backing -
                            1116.0 * geometry->pixel_width /
                                    voyemu::cluster::ui_design_width(scale)) < 1e-9 &&
                   std::abs(point->second * backing -
                            102.0 * geometry->pixel_height /
                                    voyemu::cluster::ui_design_height(scale)) <
                           1e-9); // Rendering, mouse and IME share the canonical design plane.
            const auto row = media_row(geometry->content_width, geometry->content_height, true,
                                       scale);
            assert(row);
            assert(std::abs(row->y * backing - 720u * scale / 100.0) < 1e-9 &&
                   std::abs(row->width * backing - 3840 * scale / 100.0) < 1e-9 &&
                   std::abs(row->height * backing - 720 * scale / 100.0) < 1e-9);
            const auto touch = canvas_touch_point(row->width * .75, row->y + row->height * .5,
                                                  geometry->content_width, geometry->content_height,
                                                  true, scale);
            assert(touch && touch->display == 1 && touch->x == 960 && touch->y == 360);
            assert(!canvas_touch_point(row->width, row->y, geometry->content_width,
                                       geometry->content_height, true, scale));
        }
    }

    assert(splash_uv[0] == 681.f / 1920 && splash_uv[1] == 144.f / 720 &&
           splash_uv[2] == 1239.f / 1920 && splash_uv[3] == 576.f / 720);

    for (bool cluster : {false, true}) {
        for (unsigned scale : {50u, 75u, 100u}) {
            const auto geometry = canvas_geometry(scale, 1, cluster);
            const auto row = media_row(geometry.value().pixel_width, geometry.value().pixel_height,
                                       cluster, scale);

            for (unsigned display = 0; display < 2; ++display) {
                const auto logo = splash_rect(geometry.value().pixel_width,
                                              geometry.value().pixel_height, cluster, display,
                                              scale);
                assert(logo);
                assert(std::abs(logo->x + logo->width / 2 - row.value().x -
                                row.value().width * (2 * display + 1) / 4) < 1e-9 &&
                       logo->y + logo->height / 2 == row.value().y + row.value().height / 2 &&
                       logo->height == row.value().height * .6);
                assert(std::abs(logo->width / logo->height - 558. / 432) < 1e-9 &&
                       logo->height == 432u * scale / 100.0);
            }
        }
    }

    assert(!splash_rect(0, 720, true, 0) && !splash_rect(3840, 720, true, 0, 60) &&
           !splash_rect(3840, 1440, true, 2));
    const auto compact = canvas_geometry(50, 2, true);
    assert(compact && compact->content_width == 960 && compact->content_height == 360);
    assert(compact->media_pixels.y == 360 && compact->media_pixels.width == 1920 &&
           compact->media_pixels.height == 360);
    auto compact_left = canvas_touch_point(479.99, 180, 960, 360, true, 50),
         compact_right = canvas_touch_point(480, 359.99, 960, 360, true, 50);
    assert(compact_left && compact_left->display == 0 && compact_left->x == 1919 &&
           compact_left->y == 0);
    assert(compact_right && compact_right->display == 1 && compact_right->x == 0 &&
           compact_right->y == 719);
    assert(!canvas_touch_point(960, 180, 960, 360, true, 50) &&
           !canvas_touch_point(1000, 280, 960, 360, true, 50) &&
           !canvas_touch_point(0, 179.99, 960, 360, true, 50));
    const auto compact_only = canvas_geometry(50, 2, false);
    assert(compact_only && compact_only->pixel_width == 1920 && compact_only->pixel_height == 720);
    assert(!canvas_geometry(60, 2, true) && !canvas_geometry(100, 0, true) &&
           !canvas_geometry(50, NAN, true) && !canvas_geometry(100, INFINITY, true));
    const auto middle = canvas_geometry(75, 2, true);
    assert(middle && middle->content_width == 1440 && middle->content_height == 540);
    assert(middle->media_pixels.y == 540 && middle->media_pixels.width == 2880 &&
           middle->media_pixels.height == 540);
    const auto middle_left = canvas_touch_point(719.99, 270, 1440, 540, true, 75),
               middle_right = canvas_touch_point(720, 539.99, 1440, 540, true, 75);
    assert(middle_left && middle_left->display == 0 && middle_left->x == 1919 &&
           middle_left->y == 0);
    assert(middle_right && middle_right->display == 1 && middle_right->x == 0 &&
           middle_right->y == 719);
    assert(!canvas_touch_point(0, 269.99, 1440, 540, true, 75) &&
           !canvas_touch_point(1440, 270, 1440, 540, true, 75));
    const auto middle_only = canvas_geometry(75, 2, false);
    assert(middle_only && middle_only->pixel_width == 2880 && middle_only->pixel_height == 1080);
    assert(!ui_point(1, 2, 0, 360, true) && !ui_point(NAN, 2, 960, 360, true) &&
           !view_point(1, 2, 960, INFINITY, true));
    auto resized = preserve_top_left({-400, 100, 1920, 748}, {0, 0, 960, 388});
    assert(resized.x == -400 && resized.y == 460 && resized.y + resized.height == 848);
    const auto primary_quad = display_quad(0, true), secondary_quad = display_quad(1, true);
    assert(primary_quad[0] == -1 && primary_quad[1] == -1 && primary_quad[2] == 1 &&
           primary_quad[3] == 1 && secondary_quad[0] == 0);
    assert(display_quad(0, false)[3] == 1);
    const auto compact_quad = display_quad(0, true, 50),
               compact_secondary = display_quad(1, true, 50);
    assert(compact_quad[0] == -1 && compact_quad[2] == 1 && compact_quad[3] == 1);
    assert(compact_secondary[0] == 0 && compact_secondary[0] + compact_secondary[2] == 1);
    assert(!canvas_touch_point(20, 359.99, 1920, 720, true));
    auto lower_left = canvas_touch_point(959.9, 360, 1920, 720, true);
    assert(lower_left && lower_left->display == 0 && lower_left->x == 1919 && lower_left->y == 0);
    auto lower_right = canvas_touch_point(960, 719.99, 1920, 720, true);
    assert(lower_right && lower_right->display == 1 && lower_right->x == 0 &&
           lower_right->y == 719);
    auto full_dpi = canvas_touch_point(1920, 720, 3840, 1440, true);
    assert(full_dpi && full_dpi->display == 1 && full_dpi->x == 0 && full_dpi->y == 0);
    assert(!canvas_touch_point(0, 720, 1920, 720, true) &&
           !canvas_touch_point(NAN, 400, 1920, 720, true));
    auto standalone = canvas_touch_point(960, 180, 1920, 360, false);
    assert(standalone && standalone->display == 1 && standalone->x == 0 && standalone->y == 0);
    std::vector<std::uint8_t> before(static_cast<std::size_t>(1920u * 720u) * 4u), after = before;
    assert(!scene_changed(before, after));

    for (unsigned y = 0; y < 50; ++y) {
        for (unsigned x = 0; x < 1920; ++x) {
            after[static_cast<std::size_t>(y * 1920 + x) * 4] = 255;
        }
    }

    assert(!scene_changed(before, after));

    for (unsigned y = 60; y < 720; ++y) {
        for (unsigned x = 200; x < 1200; ++x) {
            after[static_cast<std::size_t>(y * 1920 + x) * 4] = 255;
        }
    }

    assert(scene_changed(before, after));
    assert(!scene_changed(std::span(before).first(100), after));
    assert(render_enabled(false) && !render_enabled(true));
    assert(render_enabled(false) &&
           !render_visible(true, false, false, false)); // Covered inactive: render offscreen.
    assert(render_enabled(false) &&
           !render_visible(true, true, false, false)); // Minimized: render offscreen.
    assert(render_enabled(false) &&
           !render_visible(false, false, false, false)); // Hidden live window: render offscreen.
    assert(render_visible(true, false, true, false) && render_visible(true, false, false, true));
    assert(!render_visible(false, false, true, true) && !render_visible(true, true, true, true) &&
           !render_visible(true, false, false, false));
    assert(render_visible(
            true, false, false, false,
            true)); // A visible foreground window must animate without a focus round trip.
    assert(!render_visible(false, false, false, false, true) &&
           !render_visible(true, true, false, false, true));
    // The screens window: the rear screen alone, then a lower screen centred beside it.
    const std::vector<ScreenSize> rear{{1902, 540}};

    for (const unsigned scale : {50u, 75u, 100u}) {
        const auto canvas = screens_canvas(rear, scale);
        assert(canvas && canvas->pixel_width == 1902 * scale / 100 &&
               canvas->pixel_height == 540 * scale / 100 && canvas->screens.size() == 1);
        // Window pixels may differ from the canvas after DPI rounding; touches stay proportional.
        const auto corner = screens_touch_point(
                canvas->pixel_width * 2 - .01, canvas->pixel_height * 2 - .01,
                canvas->pixel_width * 2, canvas->pixel_height * 2, *canvas, rear);
        assert(corner && corner->screen == 0 && corner->x == 1901 && corner->y == 539);
        const auto origin = screens_touch_point(0, 0, canvas->pixel_width, canvas->pixel_height,
                                                *canvas, rear);
        assert(origin && origin->x == 0 && origin->y == 0);
    }

    const std::vector<ScreenSize> pair{{1902, 540}, {1280, 720}};
    const auto both = screens_canvas(pair, 50);
    assert(both && both->pixel_width == 951 + 640 && both->pixel_height == 360);
    assert(both->screens[0].y == 45 && both->screens[1].x == 951 && both->screens[1].y == 0);
    // Above the shorter screen is the window background, not a screen.
    assert(!screens_touch_point(10, 10, 1591, 360, *both, pair));
    const auto hud = screens_touch_point(951 + 320, 180, 1591, 360, *both, pair);
    assert(hud && hud->screen == 1 && hud->x == 640 && hud->y == 360);
    const auto pinned = screens_drag_point(1500, -40, 1591, 360, *both, pair, 0);
    assert(pinned && pinned->screen == 0 && pinned->x == 1901 && pinned->y == 0);
    assert(!screens_drag_point(10, 10, 1591, 360, *both, pair, 2));
    assert(!screens_canvas(rear, 60) && !screens_canvas({}, 100) &&
           !screens_canvas(std::vector<ScreenSize>{{0, 540}}, 100));
    assert(!screens_touch_point(NAN, 0, 1591, 360, *both, pair) &&
           !screens_touch_point(0, 0, 1591, 360, *both, rear));
    std::cout << "placement, input mapping and live render policy passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
