#pragma once
#include "model.hpp"
#include <algorithm>
#include <chrono>
#include <optional>

namespace voyemu {
// Own body-ECU policy: 500 ms lit / 500 ms dark, not a recovered OEM timing.
// The persistent input is the requested turn; only the ephemeral MCU projection blinks.
class BodyEcu {
public:
    using Clock = std::chrono::steady_clock;

    bool update(const Json &logical, Clock::time_point now) {
        const auto source = logical.at("sequence").get<std::uint32_t>();
        const auto &raw = logical.at("raw_oem");
        const unsigned requested = raw.at("turn_left").get<unsigned>() |
                                   (raw.at("turn_right").get<unsigned>() << 1);

        if (requested != requested_) {
            requested_ = requested;
            started_ = now;
        }

        const bool lit =
                !requested ||
                (std::chrono::duration_cast<std::chrono::milliseconds>(now - started_).count() /
                 500) % 2 ==
                        0;
        const unsigned lamps = lit ? requested : 0;

        if (!state_.is_null() && source == source_ && lamps == lamps_) {
            return false;
        }

        validate_state(logical);
        const auto previous = state_.is_null() ? 0 : state_.at("sequence").get<std::uint64_t>();
        const auto sequence = std::max<std::uint64_t>(source, previous + 1);

        if (sequence > UINT32_MAX) {
            throw VoyemuError("BODY_SEQUENCE", "MCU sequence exhausted");
        }

        state_ = logical;
        state_["sequence"] = sequence;
        state_["raw_oem"]["turn_left"] = lamps & 1;
        state_["raw_oem"]["turn_right"] = (lamps >> 1) & 1;
        source_ = source;
        lamps_ = lamps;

        return true;
    }

    const Json &state() const {
        return state_;
    }

    std::uint32_t source_revision() const {
        return source_;
    }

private:
    Json state_;
    std::uint32_t source_ = 0;
    unsigned requested_ = 0, lamps_ = 0;
    Clock::time_point started_{};
};
} // namespace voyemu
