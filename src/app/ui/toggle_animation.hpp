#pragma once
#include "music_tab_animation.hpp"
#include <algorithm>
namespace voyemu::cluster {
class ToggleAnimation {
public:
    static constexpr double duration = .4;

    bool active(double now) const {
        return initialized_ && from_ != target_ && now < start_ + duration;
    }

    float value(bool selected, double now) {
        const float next = selected ? 1.f : 0.f;

        if (!initialized_) {
            from_ = target_ = next;
            start_ = now;
            initialized_ = true;
        }

        const auto current = position(now);

        if (next != target_) {
            from_ = current;
            target_ = next;
            start_ = now;
        }

        return position(now);
    }

private:
    float position(double now) const {
        const float t = static_cast<float>(std::clamp((now - start_) / duration, 0., 1.));

        return from_ + (target_ - from_) * static_cast<float>(MusicTabIndicator::interpolate(t));
    }

    float from_ = 0, target_ = 0;
    double start_ = 0;
    bool initialized_ = false;
};
} // namespace voyemu::cluster
