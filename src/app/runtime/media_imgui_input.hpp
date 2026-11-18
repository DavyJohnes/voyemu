#pragma once
#include "imgui_internal.h"
#include <cfloat>
#include <cmath>

namespace voyemu::media {
inline bool normalize_mouse_events(ImGuiContext &context, ImU32 &mapped_through, double scale_x,
                                   double scale_y) {
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0 || scale_y <= 0) {
        return false;
    }

    // ImGui may retain queued events across frames. Never transform a retained event twice.
    for (auto &event : context.InputEventsQueue) {
        if (event.EventId <= mapped_through || event.Type != ImGuiInputEventType_MousePos ||
            event.MousePos.PosX <= -FLT_MAX || event.MousePos.PosY <= -FLT_MAX) {
            continue;
        }

        if (std::isfinite(event.MousePos.PosX) && std::isfinite(event.MousePos.PosY)) {
            event.MousePos.PosX = float(event.MousePos.PosX * scale_x);
            event.MousePos.PosY = float(event.MousePos.PosY * scale_y);
        }
    }

    mapped_through = context.InputEventsNextEventId - 1;

    return true;
}
} // namespace voyemu::media
