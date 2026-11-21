#pragma once
#include "model.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace voyemu {
// Own power policy of the vehicle: one state OFF/ACC/ON/READY that sets ign, ready_lamp and the
// 12 V battery voltage of the model for both guests and the ACC of the multimedia CAN model. Not a
// recovered OEM BCM protocol.
class PowerEcu {
public:
    enum class State {
        off,
        acc,
        on,
        ready
    };

    struct Outcome {
        Json patch = Json::object();
        Json receipt = Json::object();
    };

    static std::optional<State> parse(const Json &value) {
        if (!value.is_string()) {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < names.size(); ++i) {
            if (value == names[i]) {
                return static_cast<State>(i);
            }
        }

        return std::nullopt;
    }

    static std::string_view name(State state) {
        return names[static_cast<std::size_t>(state)];
    }

    // The ACC that the multimedia CAN model reports: OFF=0, ON=2.
    static unsigned android_acc(State state) {
        return state == State::off ? 0 : 2;
    }

    // The state of a model whose power fields a writer set directly. With ign off a vehicle that
    // was not switched off keeps the multimedia on.
    void observe(const Json &state) {
        const auto &raw = state.at("raw_oem");

        if (raw.at("ign") == 1) {
            state_ = raw.at("ready_lamp") == 1 ? State::ready : State::on;
        } else if (state_ != State::off) {
            state_ = State::acc;
        }
    }

    // READY needs the gear in P or N; a moving vehicle stays at ON or above.
    Outcome request(const Json &state, State target) {
        const auto &raw = state.at("raw_oem");
        const auto previous = state_;
        Outcome result;
        result.receipt = {{"consumer", "voyemu.power-ecu"},
                          {"policy_id", "voyemu-own-power-v1"},
                          {"requested", name(target)},
                          {"previous", name(previous)}};
        std::string reason;

        if (target == State::ready &&
            (raw.at("gear_valid") != 1 || (raw.at("gear") != gear_p && raw.at("gear") != gear_n))) {
            reason = "gear-not-park-or-neutral";
        } else if (target < State::on && raw.at("spd_valid") == 1 && raw.at("speed") != 0) {
            reason = "vehicle-moving";
        }

        if (!reason.empty()) {
            result.receipt.update({{"outcome", "rejected"}, {"reason", reason}});
        } else {
            state_ = target;
            held_.reset();
            result.patch = fields(raw);
            result.receipt["outcome"] = "applied";
        }

        result.receipt.update({{"state", name(state_)}, {"changed_fields", result.patch}});

        return result;
    }

    // The cold start of a cluster holds ON or READY at ACC until the startup releases it.
    Outcome startup(const Json &state, bool ignition, const std::string &reason) {
        const auto &raw = state.at("raw_oem");
        const auto previous = state_;

        if (!ignition && state_ >= State::on) {
            held_ = state_;
            state_ = State::acc;
        } else if (ignition && held_) {
            state_ = *held_;
            held_.reset();
        }

        Outcome result;
        result.patch = fields(raw);
        result.receipt = {{"consumer", "voyemu.power-ecu"},
                          {"policy_id", "voyemu-own-power-v1"},
                          {"requested", ignition ? "startup-release" : "startup-hold"},
                          {"reason", reason},
                          {"previous", name(previous)},
                          {"state", name(state_)},
                          {"outcome", "applied"},
                          {"changed_fields", result.patch}};

        return result;
    }

    State state() const noexcept {
        return state_;
    }

private:
    static constexpr std::array<std::string_view, 4> names{"off", "acc", "on", "ready"};
    // cluster_ui_controls gear_labels: P, R, N, D.
    static constexpr unsigned gear_p = 0, gear_n = 2;
    // ic_bat_voltage in 0.01 V, the unit of the OEM powermgr voltage levels, whose normal band is
    // 9.00-16.00 V: a resting battery, and the DC-DC converter charging it at READY.
    static constexpr unsigned battery_rest = 1240, battery_charging = 1380;

    Json fields(const Json &raw) const {
        const unsigned ign = state_ >= State::on ? 1 : 0, ready = state_ == State::ready ? 1 : 0;
        Json patch = Json::object();

        if (raw.at("ign") != ign) {
            patch["ign"] = ign;
        }

        if (raw.at("ready_lamp") != ready) {
            patch["ready_lamp"] = ready;
        }

        if (const auto voltage = ready ? battery_charging : battery_rest;
            raw.at("ic_bat_voltage") != voltage) {
            patch["ic_bat_voltage"] = voltage;
        }

        return patch;
    }

    State state_ = State::acc;
    std::optional<State> held_;
};
} // namespace voyemu
