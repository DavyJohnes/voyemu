#pragma once
#include "model.hpp"
#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace voyemu {
struct AdasOutcome {
    bool handled{};
    Json patch = Json::object();
    Json receipt = Json::object();
};
// An explicitly owned display/control model. It does not implement a physical OEM ECU protocol.
class AdasEcu {
public:
    using Clock = std::chrono::steady_clock;
    static bool handles(std::string_view id);
    static Json policy();
    AdasOutcome input(const Json &state, std::uint32_t sequence, std::string_view id,
                      std::string_view phase, std::string_view owner, Clock::time_point when);
    AdasOutcome reconcile(const Json &state) const;

    void cancel_all() noexcept {
        held_.clear();
    }

private:
    struct Held {
        std::string owner;
        Clock::time_point down;
    };
    std::map<std::string, Held, std::less<>> held_;
    std::uint32_t last_sequence_{};
};
} // namespace voyemu
