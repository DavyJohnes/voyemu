#pragma once
#include "model.hpp"
#include <functional>
#include <stdexcept>
namespace voyemu::api {
struct TapRepeat {
    unsigned count = 1, interval_ms = 100;
};

inline TapRepeat tap_repeat(const Json &request) {
    auto bounded = [&](const char *key, unsigned fallback, unsigned minimum, unsigned maximum) {
        if (!request.contains(key)) {
            return fallback;
        }

        const auto &value = request.at(key);

        if (!value.is_number_integer() ||
            (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
            value.get<std::uint64_t>() < minimum || value.get<std::uint64_t>() > maximum) {
            throw VoyemuError("TEST_ARGUMENT", "tap count must be 1..8 and interval_ms 0..300");
        }

        return value.get<unsigned>();
    };

    return {bounded("count", 1, 1, 8), bounded("interval_ms", 100, 0, 300)};
}

inline Json repeat_taps(TapRepeat repeat, const std::function<Json()> &dispatch,
                        const std::function<void(unsigned)> &delay) {
    Json result;
    unsigned completed = 0;

    try {
        for (unsigned i = 0; i < repeat.count; ++i) {
            if (i && repeat.interval_ms) {
                delay(repeat.interval_ms);
            }

            result = dispatch();
            ++completed;
        }
    } catch (const std::exception &) {
        if (completed) {
            throw VoyemuError("TEST_INPUT_PARTIAL_UNKNOWN",
                              "dispatched_count=" + std::to_string(completed) +
                                      "; requested_count=" + std::to_string(repeat.count) +
                                      "; subsequent tap outcome unknown; do not retry batch");
        }

        throw;
    }

    result["requested_count"] = repeat.count;
    result["dispatched_count"] = completed;
    result["interval_ms"] = repeat.interval_ms;

    return result;
}
} // namespace voyemu::api
