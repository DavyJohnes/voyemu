#include "parameters_layout.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace voyemu::cluster;
namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(float a, float b) {
    return std::abs(a - b) < .01f;
}

// Rectangular silhouette: columns [first, last) and rows [top, bottom) are opaque.
WheelInk ink(int first, int last, int top, int bottom) {
    WheelInk result{360, 308, std::vector<int>(360), std::vector<int>(360)};

    for (int x = first; x < last; ++x) {
        result.top[x] = top;
        result.bottom[x] = bottom;
    }

    return result;
}

constexpr float side = 6.8f / 28, power_top = 6.f / 28, power_bottom = 6.8f / 28,
                restart_top = 4.876f / 28, restart_bottom = 6.8f / 28;
} // namespace

int main() {
    try {
        {
            std::vector<std::uint8_t> rgba(4 * 3 * 4);
            rgba[(1 * 4 + 1) * 4 + 3] = 31;
            rgba[(0 * 4 + 2) * 4 + 3] = 32;
            rgba[(2 * 4 + 2) * 4 + 3] = 255;
            const auto measured = wheel_ink(rgba.data(), 4, 3, 16);
            require(measured.width == 4 && measured.height == 3, "ink keeps photo size");
            require(measured.bottom[1] == 0, "faint antialiasing is not visible wheel");
            require(measured.top[2] == 0 && measured.bottom[2] == 3,
                    "ink spans the first to one past the last visible row");
        }
        require(ui_scale_percent(canonical_ui_scale) == 50 && ui_scale_percent(75) == 75 &&
                        ui_scale_percent(100) == 100,
                "layout scale maps back to the window percent");
        const auto design = view_layout(100);
        const auto left = ink(20, 360, 40, 280), right = ink(0, 340, 40, 280);
        const auto wheel = placed_wheel(100, left, right, 122.4f);
        const auto column = wheel_column(100, left, right, 122.4f, 30);
        const auto left_photo = wheel_photo(wheel, 122.4f, false, 360, 308),
                   right_photo = wheel_photo(wheel, 122.4f, true, 360, 308);
        const float pitch = left_photo.width / 360;
        const float visible_left = std::max(wheel.x, left_photo.x + 20 * pitch),
                    visible_right = std::min(wheel.right(), right_photo.x + 340 * pitch),
                    visible_top = std::max(wheel.y, left_photo.y + 40 * pitch);
        require(near(wheel.x, design.wheel.x) && near(wheel.width, design.wheel.width) &&
                        near(wheel.height, design.wheel.height) &&
                        near(left_photo.y + 280 * pitch, column.scale_choice.y - 20),
                "the visible wheel bottom ends 40 pixels above the choices");
        require(near(placed_wheel(75, left, right, 122.4f).y - view_layout(75).wheel.y,
                     (wheel.y - design.wheel.y) * .75f) &&
                        near(placed_wheel(100, {}, {}, 122.4f).y, design.wheel.y),
                "the wheel moves proportionally at 75 percent and stays centred without a "
                "silhouette");
        require(column.scale == 1 && column.cluster_power.width == 40 &&
                        column.media_restart.height == 40,
                "buttons are 40 points at 100 percent");
        require(column.choice_cell <= std::ceil((30 + 2 * .7f * wheel_choice_font) * 2) / 2 &&
                        near(column.choice_font, wheel_choice_font) &&
                        near(column.scale_choice.width, 3 * column.choice_cell) &&
                        near(column.language_choice.width, 3 * column.choice_cell) &&
                        near(column.scale_choice.height, 40),
                "choices share one cell: widest caption plus at most 0.7 font on each side");
        require(near(gear_radio(parameters_layout(0, 0, 0, 100), column.choice_cell).width,
                     4 * column.choice_cell),
                "gear segments are as wide as the scale and language segments");
        const float glyph = 40 * (1 - 2 * side), stack_left = column.cluster_power.x + 40 * side;
        require(near(stack_left, visible_left) &&
                        near(column.media_power.x, column.cluster_power.x),
                "left controls start at the visible wheel inset");
        require(near(column.language_choice.right(), visible_right),
                "language choice ends at the visible wheel inset");
        require(near(column.cluster_restart.x, column.cluster_power.x) &&
                        near(column.cluster_restart.y + 40 * restart_top -
                                     (column.cluster_power.bottom() - 40 * power_bottom),
                             16),
                "cluster restart sits 16 points below power");
        require(near(column.media_restart.x, column.media_power.x) &&
                        near(column.media_power.y + 40 * power_top -
                                     (column.media_restart.bottom() - 40 * restart_bottom),
                             16),
                "multimedia restart sits 16 points above power, mirroring the cluster pair");
        require(near(column.screens_toggle.x + 40 * side - (stack_left + glyph), 16) &&
                        near(column.screens_toggle.width, 40),
                "the screens toggle sits 16 points right of multimedia power");
        require(near(column.usb_toggle.x, column.screens_toggle.x) &&
                        near(column.usb_toggle.y, column.media_restart.y) &&
                        near(column.usb_toggle.width, column.media_restart.width),
                "the USB drive toggle sits right of multimedia restart on its centre line");
        require(near(column.screens_toggle.x - column.media_power.x,
                     2 * column.button_hit_radius) &&
                        column.cluster_restart.y - column.cluster_power.y >=
                                2 * column.button_hit_radius &&
                        column.button_hit_radius > 40 * (.5f - side),
                "hit circles cover each glyph and meet halfway across the visible gap");
        const float pair_right = column.screens_toggle.x + column.screens_toggle.width / 2 +
                                 column.button_hit_radius;
        require(near(column.scale_choice.x - pair_right,
                     column.language_choice.x - column.scale_choice.right()) &&
                        column.scale_choice.x > pair_right,
                "scale choice is centred between the multimedia hit circle and language");
        const auto same_row = [](const WheelColumn &c, unsigned scale) {
            const auto gear = gear_radio(parameters_layout(0, 0, 0, scale), c.choice_cell);

            return near(c.scale_choice.y, gear.y) && near(c.language_choice.y, gear.y) &&
                   near(c.scale_choice.height, gear.height) &&
                   near(c.language_choice.height, gear.height) &&
                   near(c.choice_font, gear_font_size(scale));
        };
        require(same_row(column, 100), "choices share the gear row: top, height and caption size");
        // 40 physical pixels at 100 percent are 20 points.
        require(near(column.cluster_power.y + 40 * power_top, 20) &&
                        near(column.media_power.bottom() - 40 * power_bottom,
                             design.top_height - 20) &&
                        near(column.scale_choice.bottom(), design.top_height - 20) &&
                        near(gear_radio(parameters_layout(0, 0, 0, 100), column.choice_cell)
                                     .bottom(),
                             design.top_height - 20),
                "cluster power starts 40 pixels below the top; multimedia power, the choices and "
                "the gear radio end 40 pixels above the bottom");
        require(near(column.screens_toggle.y + 20,
                     column.media_power.y + 40 * (power_top + 1 - power_bottom) / 2),
                "the screens toggle shares the centre line of the multimedia power glyph");
        require(near(column.wordmark.x + column.wordmark.width / 2, wheel.x + wheel.width / 2) &&
                        near(column.wordmark.height, 32) &&
                        near(column.wordmark.width, 32 * wordmark_aspect) &&
                        near(2 * column.wordmark.y + column.wordmark.height * wordmark_body,
                             visible_top),
                "wordmark letters are centred above the visible wheel");

        for (const unsigned scale : {canonical_ui_scale, 75u}) {
            const auto scaled = wheel_column(scale, left, right, 122.4f, 30);
            const auto actual = view_layout(scale).wheel;
            const float k = actual.width / wheel.width;
            require(near(scaled.scale, k), "one factor scales the column");
            const auto same = [&](const ParameterRect &a, const ParameterRect &b) {
                return near(a.x, actual.x + (b.x - wheel.x) * k) && near(a.y, b.y * k) &&
                       near(a.width, b.width * k) && near(a.height, b.height * k);
            };
            const auto same_x = [&](const ParameterRect &a, const ParameterRect &b) {
                return near(a.x, actual.x + (b.x - wheel.x) * k) && near(a.width, b.width * k);
            };
            require(same(scaled.wordmark, column.wordmark) &&
                            same(scaled.cluster_power, column.cluster_power) &&
                            same(scaled.cluster_restart, column.cluster_restart) &&
                            same(scaled.media_power, column.media_power) &&
                            same(scaled.media_restart, column.media_restart) &&
                            same(scaled.screens_toggle, column.screens_toggle) &&
                            near(scaled.button_hit_radius, column.button_hit_radius * k),
                    "wordmark and buttons at 50 and 75 percent scale with the wheel");
            // The form and the wheel scale by 250.5 / 360 and 320 / 460 at 50 percent, which
            // differ by a fifth of a thousandth.
            const auto proportional = [&](const ParameterRect &a, const ParameterRect &b) {
                return near(a.x, actual.x + (b.x - wheel.x) * k) && near(a.width, b.width * k) &&
                       std::abs(a.y - b.y * k) < .1f && std::abs(a.height - b.height * k) < .01f;
            };
            require(same_row(scaled, scale) &&
                            proportional(scaled.scale_choice, column.scale_choice) &&
                            proportional(scaled.language_choice, column.language_choice) &&
                            near(scaled.choice_cell, column.choice_cell * k) &&
                            std::abs(scaled.choice_font - column.choice_font * k) < .01f,
                    "50 and 75 percent choices are the 100 percent choices scaled by one factor");
        }

        const auto plain = wheel_column(100, {}, {}, 130, 30);
        require(near(plain.cluster_power.x + 40 * side, wheel.x) &&
                        near(plain.language_choice.right(), wheel.right()),
                "without a silhouette the wheel rectangle bounds the column");
        std::cout << "wheel column layout passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
