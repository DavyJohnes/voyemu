#pragma once
#include <chrono>
#include <cstdint>
#include <limits>
namespace voyemu {
enum class WindowsWheelDispatch {
    none,
    single,
    wait,
    pair
};
struct WindowsWheelDispatchPlan {
    WindowsWheelDispatch kind;
    int hold_ms = 0;
};

template <class Events>
WindowsWheelDispatchPlan windows_wheel_dispatch(const Events &events,
                                                std::chrono::steady_clock::time_point now) {
    if (events.empty()) {
        return {WindowsWheelDispatch::none};
    }

    const auto &down = events.front();

    if (down.phase != "down" || (down.id != "volume_up" && down.id != "volume_down")) {
        return {WindowsWheelDispatch::single};
    }

    if (events.size() > 1) {
        const auto &up = events[1];
        const auto hold = std::chrono::duration_cast<std::chrono::milliseconds>(up.accepted_at -
                                                                                down.accepted_at)
                                  .count();

        if (up.phase == "up" && up.id == down.id && up.owner == down.owner &&
            down.sequence != std::numeric_limits<std::uint32_t>::max() &&
            up.sequence == down.sequence + 1 && up.accepted_at >= down.accepted_at &&
            up.accepted_at <= now && hold <= 200) {
            return {WindowsWheelDispatch::pair, static_cast<int>(hold > 0 ? hold : 1)};
        }

        return {WindowsWheelDispatch::single};
    }

    return {now >= down.accepted_at && now - down.accepted_at < std::chrono::milliseconds(200)
                    ? WindowsWheelDispatch::wait
                    : WindowsWheelDispatch::single};
}
} // namespace voyemu
