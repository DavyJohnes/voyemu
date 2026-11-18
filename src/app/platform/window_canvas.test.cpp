#include "window_canvas.hpp"
#include <iostream>
#include <stdexcept>
using namespace voyemu::media;

static void need(bool yes, const char *message) {
    if (!yes) {
        throw std::runtime_error(message);
    }
}

int main() {
    try {
        // Actual historical Windows/WIC client was3024x1440, not requested3840x1440.
        const auto historical = window_canvas(100, true, 3024, 1440);
        need(bool(historical), "historical layout missing");
        need(std::abs(historical->scale_x - historical->scale_y) < 1e-9,
             "WINDOWS_CANVAS_ASPECT: historical client squeezes OEM screens/wheel/text");
        need(std::abs(historical->content.height - 1134) < 1e-9,
             "historical client content must letterbox at3024x1134");

        for (unsigned zoom : {50u, 75u, 100u}) {
            for (bool cluster : {false, true}) {
                for (double dpi : {1., 1.25, 1.5, 2.}) {
                    for (auto size : {std::pair{3024., 1440.}, std::pair{1200., 1600.},
                                      std::pair{3840., 1440.}, std::pair{1920., 720.}}) {
                        const auto fit = window_canvas(zoom, cluster, size.first * dpi,
                                                       size.second * dpi);
                        need(bool(fit), "fit missing");
                        need(std::abs(fit->scale_x - fit->scale_y) < 1e-9,
                             "all client/zoom/DPI transforms must be uniform");
                        need(fit->content.width <= size.first * dpi + 1e-8 &&
                                     fit->content.height <= size.second * dpi + 1e-8,
                             "content escaped client");
                        const auto row = media_row(fit->design_width, fit->design_height, cluster,
                                                   zoom);
                        need(bool(row), "row missing");
                        need(std::abs((row->width / 2) / row->height - 1920. / 720) < 1e-9,
                             "OEM logical screen ratio changed");
                        const auto touch = window_canvas_touch(
                                fit->content.width * .75, fit->content.height * .75,
                                size.first * dpi, size.second * dpi, cluster, zoom);
                        need(touch && touch->display == 1 && touch->x == 960 && touch->y == 360,
                             "render and pointer transforms differ");
                        need(std::abs(fit->logical_width * fit->scale_x - size.first * dpi) <
                                             1e-8 &&
                                     std::abs(fit->logical_height * fit->scale_y -
                                              size.second * dpi) < 1e-8,
                             "framebuffer does not cover physical client");

                        if (fit->content.height + 1 < size.second * dpi) {
                            need(!window_canvas_touch(10, fit->content.height + 1, size.first * dpi,
                                                      size.second * dpi, cluster, zoom),
                                 "bottom letterbox accepted pointer input");
                        }

                        if (fit->content.width + 1 < size.first * dpi) {
                            need(!window_canvas_touch(fit->content.width + 1,
                                                      fit->content.height * .75, size.first * dpi,
                                                      size.second * dpi, cluster, zoom),
                                 "right letterbox accepted pointer input");
                        }
                    }
                }
            }
        }

        need(!window_canvas(100, true, 0, 720) && !window_canvas(100, true, NAN, 720) &&
                     !window_canvas(60, true, 1920, 720),
             "invalid canvas accepted");
        std::cout
                << "Uniform canvas/OEM/control/input geometry passed across client sizes, zoom and DPI\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
