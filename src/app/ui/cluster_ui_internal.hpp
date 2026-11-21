#pragma once
#include "cluster_font.hpp"
#include "cluster_ui.hpp"
#include "control_drawing.hpp"
#include "error.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "licenses.hpp"
#include "logging.hpp"
#include "parameters_layout.hpp"
#include "qinggan11_startup.hpp"
#include "ui_localization.hpp"
#include "vector_artwork.hpp"
#include "wheel_geometry.hpp"
#include "wheel_hover.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

// Helpers shared by the parts of the cluster UI; each part includes this header.
namespace voyemu::cluster::cluster_ui_detail {
ImU32 control_ink(ImU32 color);
ImU32 alpha_ink(ImU32 color, unsigned alpha);
// Recolours the vertices added from `first` on by their height in a vertical gradient. Shapes are
// drawn opaque white, so the anti-aliased fringe keeps its transparency.
template <typename Color>
void shade_vertical(ImDrawList *draw, int first, float top, float height, Color color) {
    for (int i = first; i < draw->VtxBuffer.Size; ++i) {
        auto &vertex = draw->VtxBuffer[i];
        const ImU32 ink = control_ink(color((vertex.pos.y - top) / height));
        vertex.col = alpha_ink(
                ink, ((ink >> IM_COL32_A_SHIFT) * (vertex.col >> IM_COL32_A_SHIFT) + 127) / 255);
    }
}
bool event_name(std::string_view name);
std::string lower(std::string value);
float sample_ink_height(ImFont *font, float size, float density, const char *sample);
float sample_ink_bottom(ImFont *font, float size, float density, const char *sample);
float reference_font_size(ImFont *font, const char *sample, float guest_ink, unsigned scale);
struct ControlPalette {
    ImU32 background, surface, hover, active, border, text, secondary, muted, accent, error,
            inactive, input, divider;
};
class ControlStyle {
    struct Color {
        ImGuiCol slot;
        ImU32 color;
    };

public:
    explicit ControlStyle(const ControlPalette &p) {
        const Color colors[] = {{ImGuiCol_WindowBg, p.background},
                                {ImGuiCol_ChildBg, p.background},
                                {ImGuiCol_PopupBg, p.surface},
                                {ImGuiCol_InputTextCursor, p.accent},
                                {ImGuiCol_Text, p.text},
                                {ImGuiCol_TextDisabled, p.muted},
                                {ImGuiCol_Border, p.border},
                                {ImGuiCol_BorderShadow, 0},
                                {ImGuiCol_FrameBg, p.surface},
                                {ImGuiCol_FrameBgHovered, p.hover},
                                {ImGuiCol_FrameBgActive, p.active},
                                {ImGuiCol_Button, p.surface},
                                {ImGuiCol_ButtonHovered, p.hover},
                                {ImGuiCol_ButtonActive, p.active},
                                {ImGuiCol_Header, alpha_ink(p.accent, 28)},
                                {ImGuiCol_HeaderHovered, p.hover},
                                {ImGuiCol_HeaderActive, p.active},
                                {ImGuiCol_CheckboxSelectedBg, alpha_ink(p.accent, 25)},
                                {ImGuiCol_CheckMark, IM_COL32(255, 255, 255, 255)},
                                {ImGuiCol_NavCursor, p.accent},
                                {ImGuiCol_TextSelectedBg, alpha_ink(p.accent, 65)},
                                {ImGuiCol_ScrollbarBg, p.background},
                                {ImGuiCol_ScrollbarGrab, p.border},
                                {ImGuiCol_ScrollbarGrabHovered, p.muted},
                                {ImGuiCol_ScrollbarGrabActive, p.secondary},
                                {ImGuiCol_TableRowBg, 0},
                                {ImGuiCol_TableRowBgAlt, 0}};

        count_ = static_cast<int>(std::size(colors));

        for (const auto &entry : colors) {
            ImGui::PushStyleColor(entry.slot, ImGui::ColorConvertU32ToFloat4(entry.color));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 6.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 3);
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, .3f);
    }

    ~ControlStyle() {
        ImGui::PopStyleVar(11);
        ImGui::PopStyleColor(count_);
    }

private:
    int count_{};
};
struct ThreeDigitEdit {
    std::string before;

    static int callback(ImGuiInputTextCallbackData *data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
            return (data->EventChar >= '0' && data->EventChar <= '9') || data->EventChar == '-' ||
                                   data->EventChar == '+' || data->EventChar == '.'
                           ? 0
                           : 1;
        }

        auto &self = *static_cast<ThreeDigitEdit *>(data->UserData);
        const std::string after(data->Buf, static_cast<std::size_t>(data->BufTextLen));
        std::size_t prefix = 0, suffix = 0;

        while (prefix < self.before.size() && prefix < after.size() &&
               self.before[prefix] == after[prefix]) {
            ++prefix;
        }

        while (suffix < self.before.size() - prefix && suffix < after.size() - prefix &&
               self.before[self.before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) {
            ++suffix;
        }

        const auto digit = [](char c) {
            return c >= '0' && c <= '9';
        };
        int remaining =
                3 - static_cast<int>(std::count_if(after.begin(), after.begin() + prefix, digit) +
                                     std::count_if(after.end() - suffix, after.end(), digit));

        for (int i = static_cast<int>(prefix); i < data->BufTextLen - static_cast<int>(suffix);) {
            if (digit(data->Buf[i]) && remaining <= 0) {
                data->DeleteChars(i, 1);
            } else {
                if (digit(data->Buf[i])) {
                    --remaining;
                }

                ++i;
            }
        }

        return 0;
    }
};
const ControlPalette &control_palette(bool light);
} // namespace voyemu::cluster::cluster_ui_detail
