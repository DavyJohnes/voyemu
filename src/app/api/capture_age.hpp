#pragma once
#include "error.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace voyemu::api {
struct CaptureClock {
    std::uint64_t unix_us, steady_us;
};

inline std::uint64_t capture_age_upper_us(std::uint64_t source_us, CaptureClock before,
                                          CaptureClock after) {
    constexpr std::uint64_t tolerance_us = 2000;

    if (!source_us || source_us > after.unix_us || after.unix_us < before.unix_us ||
        after.steady_us < before.steady_us) {
        throw VoyemuError("TEST_FRAME_CLOCK", "invalid SDK timestamp or host clock interval");
    }

    const auto wall = after.unix_us - before.unix_us, steady = after.steady_us - before.steady_us;
    const auto drift = wall > steady ? wall - steady : steady - wall;

    if (drift > tolerance_us) {
        throw VoyemuError("TEST_FRAME_CLOCK", "host wall clock changed during capture");
    }

    const auto age = after.unix_us - source_us;

    if (age > std::numeric_limits<std::uint64_t>::max() - tolerance_us) {
        throw VoyemuError("TEST_FRAME_CLOCK", "frame age overflow");
    }

    return age + tolerance_us;
}
} // namespace voyemu::api
