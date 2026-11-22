#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace voyemu::cluster {
struct ParameterRect {
    float x{}, y{}, width{}, height{};

    constexpr float right() const {
        return x + width;
    }

    constexpr float bottom() const {
        return y + height;
    }
};
struct ParametersLayout {
    static constexpr std::size_t section_count = 5;
    static constexpr float inset = 6, column_gap = 12, row_pitch = 34, field_height = 26,
                           value_width = 64, raw_value_width = 100, slider_label_width = 76,
                           field_gap = 8, lower_wheel_y = 0;
    static constexpr float form_height = 4 * row_pitch + field_height;
    static constexpr float tyre_row_pitch = 64;
    ParameterRect panel, toggle, tabs, body, wheel;

    constexpr ParameterRect tab(std::size_t i) const {
        constexpr float x[] = {4, 79, 122, 162, 206}, w[] = {71, 39, 36, 40, 53};

        return {panel.x + x[i] + 2, tabs.y, w[i], 24};
    }

    constexpr bool compact50() const {
        return panel.width == 348 && panel.height == 250.5f;
    }

    constexpr bool scaled_form() const {
        return panel.height >= 250.5f;
    }

    constexpr float form_scale() const {
        return scaled_form() ? panel.height / 250.5f : 1.f;
    }

    // The one factor of the 100 percent design: the area height over its 360 points at 100 percent.
    constexpr float scale() const {
        return panel.height / 360.f;
    }

    // The gear radio and the bottom controls of the wheel column end this far above the bottom of
    // the area, and the cluster power glyph starts this far below its top: 40 physical pixels at
    // 100 percent.
    constexpr float edge_margin() const {
        return 20.f * scale();
    }

    constexpr float density() const {
        return compact50() ? 1920.f / 1336.f : 2.f;
    }

    constexpr float tab_height() const {
        return scaled_form() ? 32.f * form_scale() : 32.f;
    }

    constexpr float outer_inset() const {
        return inset * form_scale();
    }

    constexpr float content_inset() const {
        return scaled_form() ? 7.f * form_scale() : 0.f;
    }

    constexpr float group_margin_top() const {
        return scaled_form() ? 12.f / (1920.f / 1336.f) * form_scale() : 0.f;
    }

    constexpr float first_heading_offset() const {
        return scaled_form() ? 4.f / (1920.f / 1336.f) * form_scale() : 0.f;
    }

    constexpr float raw_input_width() const {
        return scaled_form() ? movement_value_width() : raw_value_width;
    }

    constexpr float tab_font_size() const {
        return compact50() ? 12.5f : 13.5f;
    }

    constexpr float label_font_size() const {
        return 20.f * form_scale();
    }

    // Scale the approved AP Hotspot capsule to at most its original80physical pixels.
    constexpr float input_height() const {
        return scaled_form() ? 40.f / (1920.f / 1336.f) * form_scale() : 30.f;
    }

    // The radio capsule is as high as the fields: 80 physical pixels at 100 percent.
    constexpr float radio_height() const {
        return input_height();
    }

    constexpr float input_font_size() const {
        return 18.f * form_scale();
    }

    constexpr float movement_value_width() const {
        return scaled_form() ? 71.f * form_scale() : value_width;
    }

    constexpr float label_gap() const {
        return field_gap * form_scale();
    }

    constexpr float input_padding() const {
        return scaled_form() ? (15.f - 2.f / (1920.f / 1336.f)) * form_scale() : 8.f;
    }

    // The fifth row is centred on the gear radio, which ends edge_margin above the panel bottom;
    // the rows above it spread evenly from the top of the body.
    constexpr float movement_row_pitch() const {
        return scaled_form() ? (panel.bottom() - edge_margin() -
                                (radio_height() + input_height()) / 2 - body.y) /
                                       4
                             : (body.height - input_height()) / 4;
    }

    constexpr float tyre_group_pitch() const {
        return tyre_group_pitch(label_font_size());
    }

    constexpr float tyre_group_pitch(float heading_size) const {
        return heading_size + 22 * form_scale() + input_height() + group_margin_top() -
               (scaled_form() ? form_scale() / (1920.f / 1336.f) : 0.f);
    }

    constexpr ParameterRect movement_row(std::size_t column_index, std::size_t index) const {
        const auto c = content_column(2, column_index);

        return {c.x, c.y + index * movement_row_pitch(), c.width, input_height()};
    }

    constexpr ParameterRect column(std::size_t count, std::size_t index) const {
        const float width = (body.width - column_gap * form_scale() * (count - 1)) / count;

        return {body.x + index * (width + column_gap * form_scale()), body.y, width, body.height};
    }

    constexpr ParameterRect content_column(std::size_t count, std::size_t index) const {
        auto c = column(count, index);
        c.x += content_inset();
        c.width -= content_inset();

        return c;
    }

    constexpr ParameterRect checkbox_column(std::size_t index) const {
        auto c = content_column(2, index);

        if (scaled_form() && index) {
            c.x -= 12.f / (1920.f / 1336.f) * form_scale();
        }

        return c;
    }

    constexpr ParameterRect tyre_column(std::size_t index) const {
        auto c = content_column(2, index);

        if (scaled_form() && index) {
            c.x -= 6.f / (1920.f / 1336.f) * form_scale();
        }

        return c;
    }

    constexpr ParameterRect row(ParameterRect column, std::size_t index) const {
        return {column.x, column.y + index * row_pitch, column.width, field_height};
    }

    constexpr ParameterRect value(ParameterRect row) const {
        return {row.x, row.y, value_width, field_height};
    }
};
// Measure labels in the active font before laying out locale-dependent hit regions.
struct ParameterTabs {
    std::array<ParameterRect, 5> items{};
    ParameterRect search{};
    float font_scale{1};
    ParameterRect content{};
};

inline ParameterTabs parameter_tabs(const ParametersLayout &layout,
                                    const std::array<float, 5> &widths, bool search) {
    ParameterTabs result;
    constexpr float minimum_gap = 8;
    const float left = layout.body.x + layout.content_inset();
    const float available = layout.body.width - 2 * layout.content_inset();
    float total = 0;

    for (const auto width : widths) {
        total += std::max(0.f, width);
    }

    result.font_scale = layout.compact50() ? 1.f
                        : total > 0 ? std::clamp((available - minimum_gap * 4) / total, 0.f, 1.f)
                                    : 1.f;
    const float gap = layout.scaled_form() ? (available - total * result.font_scale) / 4 -
                                                     2.f / (1920.f / 1336.f) * layout.form_scale()
                                           : minimum_gap;
    float x = left;

    for (std::size_t i = 0; i < widths.size(); ++i) {
        const float width = std::max(0.f, widths[i]) * result.font_scale;
        result.items[i] = {x, layout.tabs.y + 6 * layout.form_scale(), width, layout.tab_height()};
        x += width + gap;
    }

    result.search = {layout.body.x + layout.content_inset(), layout.body.y, available,
                     layout.input_height()};
    result.content = {layout.body.x + layout.content_inset(), layout.body.y, available,
                      layout.body.height};

    if (search) {
        result.content.y += result.search.height + 8 * layout.form_scale();
        result.content.height -= result.search.height + 8 * layout.form_scale();
    }

    return result;
}

struct ViewLayout {
    ParameterRect frame, wheel, parameters;
    float top_width, top_height, canvas_width, canvas_height;
};
constexpr unsigned canonical_ui_scale = 0;
constexpr float canonical_ui_width = 1336, canonical_ui_height = 501;

// Preserve OEM control pixels until the available quadrant is smaller than the measured form.
constexpr float ui_design_width(unsigned percent) {
    return std::max(canonical_ui_width, 1920.f * percent / 100);
}

constexpr float ui_design_height(unsigned percent) {
    return ui_design_width(percent) * .375f;
}

constexpr unsigned ui_layout_scale(unsigned percent) {
    return percent == 50 ? canonical_ui_scale : percent;
}

constexpr ParameterRect centered_wheel(float x, float width, float available_height) {
    const float height = 166.32f * width / 334.f;

    return {x, (available_height - height) / 2, width, height};
}

constexpr ViewLayout view_layout(unsigned screen_scale = 100) {
    if (screen_scale == canonical_ui_scale) {
        return {{0, 0, 668, 250.5f},
                centered_wheel(668, 320, 250.5f),
                {988, 0, 348, 250.5f},
                1336,
                250.5f,
                1336,
                501};
    }

    if (screen_scale == 50) {
        return {{0, 0, 480, 180},
                centered_wheel(480, 334, 202),
                {814, 0, 334, 202},
                1148,
                202,
                1148,
                382};
    }

    // The approved50percent wheel/form split scales with the screens, rounded to native pixels.
    if (screen_scale == 75) {
        return {{0, 0, 720, 270},
                centered_wheel(720, 345, 270),
                {1065, 0, 375, 270},
                1440,
                270,
                1440,
                540};
    }

    return {{0, 0, 960, 360},
            centered_wheel(960, 460, 360),
            {1420, 0, 500, 360},
            1920,
            360,
            1920,
            720};
}

constexpr ParametersLayout parameters_layout(float, float, float, unsigned screen_scale = 100) {
    const auto view = view_layout(screen_scale);
    const auto p = view.parameters;
    const float factor = p.height >= 250.5f ? p.height / 250.5f : 1.f;
    const float tab_height = 32 * factor, outer = 6 * factor,
                body_top = (46.f + (p.height >= 250.5f ? 8.f / (1920.f / 1336.f) : 0.f)) * factor;

    return {p,
            {},
            {p.x + outer, p.y, p.width - 2 * outer, tab_height},
            {p.x + outer, p.y + body_top, p.width - 2 * outer, p.height - body_top - outer},
            view.wheel};
}

constexpr unsigned ui_scale_percent(unsigned layout_scale) {
    return layout_scale == canonical_ui_scale ? 50 : layout_scale;
}

constexpr float guest_scale(unsigned scale) {
    return scale == canonical_ui_scale || scale == 50 ? .5f : scale / 100.f;
}

constexpr float physical_density(unsigned scale) {
    return scale == canonical_ui_scale ? 1920.f / canonical_ui_width : 2.f;
}

// Radio captions keep the OEM 28:70 proportion to the capsule height: 32 physical pixels at 100
// percent, scaled with the design.
constexpr float gear_font_size(unsigned scale) {
    return 16.f * view_layout(scale).top_height / 360.f;
}

// The P/R/N/D radio fills the fifth movement row with four segments of the shared radio cell.
constexpr ParameterRect gear_radio(const ParametersLayout &layout, float cell) {
    const auto row = layout.movement_row(0, 4);

    return {row.x, row.y, 4 * cell, layout.radio_height()};
}

// Visible wheel silhouette: per photo column, the first opaque row and one past the last.
struct WheelInk {
    int width{}, height{};
    std::vector<int> top, bottom;
};

inline WheelInk wheel_ink(const std::uint8_t *rgba, int width, int height, std::size_t stride) {
    WheelInk ink{width, height, std::vector<int>(width), std::vector<int>(width)};

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if (rgba[std::size_t(y) * stride + std::size_t(x) * 4 + 3] >= 32) {
                if (ink.bottom[x] == 0) {
                    ink.top[x] = y;
                }

                ink.bottom[x] = y + 1;
            }
        }
    }

    return ink;
}

constexpr float wheel_photo_width = 194.4f, wheel_photo_height = 166.32f;

constexpr ParameterRect wheel_photo(ParameterRect wheel, float joined_offset, bool right, int width,
                                    int height) {
    const float wheel_scale = wheel.width / 334.f;
    const float scale = std::min(wheel_photo_width / width, wheel_photo_height / height) *
                        wheel_scale;
    const float w = width * scale, h = height * scale;

    return {wheel.x +
                    (-.36444f + (130.f - joined_offset) * .54f +
                     (right ? joined_offset * 1.08f : 0)) *
                            wheel_scale +
                    (wheel_photo_width * wheel_scale - w) / 2,
            wheel.y + (wheel_photo_height * wheel_scale - h) / 2, w, h};
}

// Choice captions are measured at the gear caption size at 100 percent.
constexpr float wheel_choice_font = gear_font_size(100);
constexpr float wordmark_aspect = 800.f / 144, wordmark_body = 107.f / 144;
struct WheelColumn {
    float scale{};
    ParameterRect wordmark, scale_choice, language_choice, cluster_power, cluster_restart,
            media_power, media_restart;
    float choice_cell{}, choice_font{};
    // Hit circles around the glyph centres; neighbours meet halfway across their visible gap.
    float button_hit_radius{};
    // The screens window toggle in the bottom row, right of the multimedia power button.
    ParameterRect screens_toggle;
    // The USB drive toggle in the top row, right of the multimedia restart button.
    ParameterRect usb_toggle;
};

// The column around the wheel is laid out once at 100 percent and scaled by wheel width / 460.
// Horizontal insets follow the visible wheel silhouette; without it, the wheel rectangle is used.
// The scale and language choices share the gear row of the current form: top, height and caption
// size; their cell is the segment of every radio, the P/R/N/D radio included. The cluster power
// glyph starts, and the multimedia power glyph ends, the edge margin of the gear row from the area
// edges; the multimedia pair mirrors the cluster pair: power below, restart above, the screens
// window toggle right of power and the USB drive toggle right of restart on their centre lines.
// The wheel rectangle at a scale: horizontally where view_layout puts it; vertically the visible
// bottom of the wheel photos ends 40 physical pixels at 100 percent above the gear row, which ends
// as far above the bottom of the area. Without a silhouette the rectangle stays centred.
inline ParameterRect placed_wheel(unsigned screen_scale, const WheelInk &left,
                                  const WheelInk &right, float joined_offset) {
    const auto design = view_layout(100);
    const auto wheel = design.wheel;
    const auto form = parameters_layout(0, 0, 0, 100);
    float bottom = -1;

    for (const auto *ink : {&left, &right}) {
        if (ink->width <= 0 || ink->height <= 0 || ink->top.size() != std::size_t(ink->width) ||
            ink->bottom.size() != std::size_t(ink->width)) {
            continue;
        }

        const auto photo = wheel_photo(wheel, joined_offset, ink == &right, ink->width,
                                       ink->height);
        const float pitch = photo.width / ink->width, rise = photo.height / ink->height;

        for (int c = 0; c < ink->width; ++c) {
            const float x0 = photo.x + c * pitch;

            if (ink->bottom[c] > ink->top[c] && x0 + pitch > wheel.x && x0 < wheel.right()) {
                bottom = std::max(bottom,
                                  std::min(wheel.bottom(), photo.y + ink->bottom[c] * rise));
            }
        }
    }

    auto actual = view_layout(screen_scale).wheel;

    if (bottom >= 0) {
        actual.y += (design.top_height - 2 * form.edge_margin() - form.radio_height() - bottom) *
                    actual.width / wheel.width;
    }

    return actual;
}

inline WheelColumn wheel_column(unsigned screen_scale, const WheelInk &left, const WheelInk &right,
                                float joined_offset, float widest_choice_label) {
    const auto design = view_layout(100);
    const auto wheel = placed_wheel(100, left, right, joined_offset);
    struct Photo {
        const WheelInk &ink;
        ParameterRect rect;
    };
    const auto photo = [&](const WheelInk &ink, bool right_side) {
        const bool valid = ink.width > 0 && ink.height > 0 &&
                           ink.top.size() == std::size_t(ink.width) &&
                           ink.bottom.size() == std::size_t(ink.width);

        return Photo{ink,
                     valid ? wheel_photo(wheel, joined_offset, right_side, ink.width, ink.height)
                           : ParameterRect{}};
    };
    const std::array<Photo, 2> photos{photo(left, false), photo(right, true)};
    // Visits visible columns over [x0, x1] as clipped to the wheel rectangle.
    const auto columns = [&](float x0, float x1, auto visit) {
        for (const auto &p : photos) {
            if (p.rect.width <= 0) {
                continue;
            }

            const float pitch = p.rect.width / p.ink.width, rise = p.rect.height / p.ink.height;
            const int first = std::max(0,
                                       int(std::floor((std::max(x0, wheel.x) - p.rect.x) / pitch)));
            const int last =
                    std::min(p.ink.width - 1,
                             int(std::ceil((std::min(x1, wheel.right()) - p.rect.x) / pitch)) - 1);

            for (int c = first; c <= last; ++c) {
                if (p.ink.bottom[c] > p.ink.top[c]) {
                    visit(std::max(wheel.x, p.rect.x + c * pitch),
                          std::min(wheel.right(), p.rect.x + (c + 1) * pitch),
                          std::max(wheel.y, p.rect.y + p.ink.top[c] * rise),
                          std::min(wheel.bottom(), p.rect.y + p.ink.bottom[c] * rise));
                }
            }
        }
    };
    float visible_left = wheel.right(), visible_right = wheel.x;

    columns(wheel.x, wheel.right(), [&](float x0, float x1, float, float) {
        visible_left = std::min(visible_left, x0);
        visible_right = std::max(visible_right, x1);
    });

    if (visible_left > visible_right) {
        visible_left = wheel.x;
        visible_right = wheel.right();
    }

    const auto top_edge = [&](float x0, float x1) {
        float edge = wheel.bottom();
        columns(x0, x1, [&](float, float, float top, float) {
            edge = std::min(edge, top);
        });

        return edge == wheel.bottom() ? wheel.y : edge;
    };
    // Glyph insets inside a button box in units of box / 28: ring radius 6.5 plus half stroke,
    // power stem top and restart arrowhead.
    constexpr float side = 6.8f / 28, power_top = 6.f / 28, power_bottom = 6.8f / 28,
                    restart_top = 4.876f / 28, restart_bottom = 6.8f / 28;
    constexpr float button = 40, gap = 16, word_height = 32, choice_padding = .7f;
    const float visible_width = button * (1 - 2 * side), x = visible_left,
                box_x = x - button * side;
    const float pair_right = x + 2 * visible_width + gap;
    const float word_width = word_height * wordmark_aspect,
                word_x = wheel.x + (wheel.width - word_width) / 2;
    const float word_y = (top_edge(word_x, word_x + word_width) - word_height * wordmark_body) / 2;
    const float power_visible = button * (1 - power_top - power_bottom);
    const float margin = parameters_layout(0, 0, 0, 100).edge_margin();
    // Captions use the gear size; the row starts at the multimedia hit circle and, when short,
    // shrinks side padding and gaps first. Cells end on whole physical pixels at 100 percent.
    const float row_left = pair_right + gap / 2, row_right = visible_right, min_gap = 4,
                fit_padding = ((row_right - row_left - 2 * min_gap) / 6 - widest_choice_label) /
                              (2 * wheel_choice_font),
                padding = std::clamp(fit_padding, .25f, choice_padding),
                cell = std::ceil((widest_choice_label + 2 * padding * wheel_choice_font) * 2) / 2,
                choice_width = 3 * cell;
    const float language_x = row_right - choice_width,
                scale_x = (row_left + language_x - choice_width) / 2;
    const float media_y = design.top_height - margin - button * (1 - power_bottom),
                media_center = media_y + button * (power_top + 1 - power_bottom) / 2,
                restart_y = media_y + button * power_top - gap - button * (1 - restart_bottom);
    const auto view = view_layout(screen_scale);
    const auto actual = view.wheel;
    const float k = actual.width / wheel.width;
    const auto scaled = [&](ParameterRect r) {
        return ParameterRect{actual.x + (r.x - wheel.x) * k, r.y * k, r.width * k, r.height * k};
    };
    const auto gear = gear_radio(parameters_layout(0, 0, 0, screen_scale), cell * k);
    const auto row = [&](float x) {
        return ParameterRect{actual.x + (x - wheel.x) * k, gear.y, choice_width * k, gear.height};
    };

    return {k,
            scaled({word_x, word_y, word_width, word_height}),
            row(scale_x),
            row(language_x),
            scaled({box_x, margin - button * power_top, button, button}),
            scaled({box_x, margin + power_visible + gap - button * restart_top, button, button}),
            scaled({box_x, media_y, button, button}),
            scaled({box_x, restart_y, button, button}),
            cell * k,
            gear_font_size(screen_scale),
            (button * (.5f - side) + gap / 2) * k,
            scaled({x + visible_width + gap - button * side, media_center - button / 2, button,
                    button}),
            scaled({x + visible_width + gap - button * side, restart_y, button, button})};
}
} // namespace voyemu::cluster
