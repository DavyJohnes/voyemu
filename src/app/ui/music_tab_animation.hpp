#pragma once
#include <algorithm>
#include <cmath>

namespace voyemu::cluster {
// OEM Music QGRadioGroup uses OvershootInterpolator(1) over 400 ms.
// Pure host math: no guest, IPC, sleeping or frame-rate dependent integration.
class MusicTabIndicator {
public:
    static constexpr double duration = .4;

    // Overshoot(1) peaks at 1+1/27. Repeated interrupted reversals bound
    // the excursion by span/26; reserve an extra point for raster rounding.
    static float animation_padding(float span) {
        return float(std::ceil(std::max(0.f, span) / 26.f) + 1);
    }

    static double interpolate(double progress) {
        const double t = std::clamp(progress, 0.0, 1.0) - 1.0;

        return t * t * (2.0 * t + 1.0) + 1.0;
    }

    double position(double now) const {
        if (!initialized_) {
            return 0;
        }

        return from_ + (target_ - from_) * interpolate((now - start_) / duration);
    }

    void snap(double target, double now) {
        initialized_ = true;
        from_ = target_ = target;
        start_ = now - duration;
    }

    void select(double target, double now) {
        if (!initialized_) {
            snap(target, now);

            return;
        }

        if (target == target_) {
            return;
        }

        from_ = position(now);
        target_ = target;
        start_ = now;
    }

    bool active(double now) const {
        return initialized_ && from_ != target_ && now - start_ < duration;
    }

private:
    double from_{}, target_{}, start_{};
    bool initialized_{};
};
} // namespace voyemu::cluster
