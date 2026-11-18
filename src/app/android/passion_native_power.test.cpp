#include "passion_native_power.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::media;
void presentation_off_regression();

int main() try {
    presentation_off_regression();
    unsigned wakes = 0, rechecks = 0, sdk = 0;
    Json reply;
    PassionNativePower p(
            "session",
            [&](unsigned, const std::string &) {
                ++wakes;

                return reply;
            },
            [&] {
                ++rechecks;
            },
            [] {
                return 100u;
            });
    using Phase = PassionPointerWake::Phase;
    using Result = PassionPointerWake::Result;
    Json normal = {{"session_id", "session"}, {"deliveries", {{"android", Json::object()}}}};
    p.observe(normal);
    assert(p.before(Phase::down, 0) == Result::pass);
    p.before(Phase::up, 0);
    Json state = {{"session_id", "session"},
                  {"boot_id", "boot"},
                  {"watch_id", "watch"},
                  {"display", 0},
                  {"unique_id", "local:8141241408256768"},
                  {"kind", "applied"},
                  {"state", 2},
                  {"request", 1},
                  {"transaction", 1},
                  {"order", 1},
                  {"observed_ms", 1000},
                  {"expires_monotonic_us", 2000000}};
    Json value = {{"schema", 1},
                  {"protocol", "passion-display-power-v1"},
                  {"session_id", "session"},
                  {"boot_id", "boot"},
                  {"watch_id", "watch"},
                  {"capability", PassionPowerOwner::capability()},
                  {"states", Json::array({state})}};
    auto runtime = normal;
    runtime["deliveries"]["android"]["display_power"] = value;
    p.observe(runtime);

    for (int i = 0; i < 100; i++) {
        if (p.before(Phase::down, 0) == Result::pass) {
            ++sdk;
        }

        p.before(Phase::up, 0);
    }

    assert(sdk == 100 && !wakes && !rechecks);
    state["state"] = 1;
    state["order"] = 2;
    runtime["deliveries"]["android"]["display_power"]["states"] = Json::array({state});
    p.observe(runtime);
    reply = state;
    reply["ok"] = true;
    reply.erase("display");
    reply["display_id"] = 0;
    reply["state"] = 2;
    reply["kind"] = "wake_observed";
    reply["request"] = 3;
    reply["transaction"] = 4;
    reply["order"] = 4;
    assert(p.before(Phase::down, 0) == Result::consumed);
    assert(p.before(Phase::move, 0) == Result::consumed);
    p.before(Phase::up, 0);
    assert(wakes == 1);
    p.observe(runtime);
    assert(p.before(Phase::down, 0) == Result::pass);
    p.before(Phase::up, 0);
    assert(wakes == 1);
    state["state"] = 1;
    state["order"] = 5;
    state["request"] = 5;
    state["transaction"] = 5;
    runtime["deliveries"]["android"]["display_power"]["states"] = Json::array({state});
    p.observe(runtime);
    reply["boot_id"] = "foreign";
    reply["request"] = 7;
    reply["transaction"] = 7;
    reply["order"] = 7;
    bool wrong = false;
    try {
        p.before(Phase::down, 0);
    } catch (...) {
        wrong = true;
    }

    assert(wrong);
    assert(p.before(Phase::move, 0) == Result::consumed);
    p.before(Phase::up, 0);
    reply["boot_id"] = "boot";
    assert(p.before(Phase::down, 0) == Result::consumed);
    p.before(Phase::up, 0);
    assert(wakes == 3);
    runtime["deliveries"]["android"].erase("display_power");
    p.observe(runtime);
    bool failed = false;
    try {
        p.before(Phase::down, 0);
    } catch (...) {
        failed = true;
    }

    assert(failed && rechecks == 1);
    assert(p.before(Phase::move, 0) == Result::consumed);
    p.before(Phase::up, 0);

    // Windows monitor emits explicit cancellation before any synthetic point enters the gate.
    unsigned releases = 0;
    auto cancel = [&] {
        p.cancel([&] {
            ++releases;
        });
    };
    // Secondary unknown DOWN -> readiness lost -> cancel -> primary known-ON DOWN.
    p.observe(runtime);
    bool secondary_unknown = false;
    try {
        p.before(Phase::down, 1);
    } catch (...) {
        secondary_unknown = true;
    }

    assert(secondary_unknown);
    cancel();
    runtime["deliveries"]["android"]["display_power"] = value;
    p.observe(runtime);
    assert(p.before(Phase::down, 0) == Result::pass);
    p.before(Phase::up, 0);
    assert(releases == 1);
    // Secondary confirmed OFF wake DOWN -> cancel -> primary ON; no synthetic display0 UP.
    auto second = state;
    second["display"] = 1;
    second["unique_id"] = "virtual:com.android.emulator.multidisplay:1234562";
    second["state"] = 1;
    second["order"] = 10;
    second["request"] = 10;
    second["transaction"] = 10;
    runtime["deliveries"]["android"]["display_power"]["states"] =
            Json::array({value["states"][0], second});
    p.observe(runtime);
    reply = second;
    reply.erase("display");
    reply["display_id"] = 1;
    reply["ok"] = true;
    reply["kind"] = "wake_observed";
    reply["state"] = 2;
    reply["order"] = 11;
    reply["request"] = 11;
    reply["transaction"] = 11;
    assert(p.before(Phase::down, 1) == Result::consumed);
    cancel();
    assert(p.before(Phase::down, 0) == Result::pass);
    p.before(Phase::up, 0);
    assert(releases == 2);
    // A failed SDK release cannot retain a suppressed power gesture.
    try {
        p.cancel([] {
            throw std::runtime_error("release fixture");
        });
        assert(false);
    } catch (const std::exception &) { /* Ignored: the expected rejection is asserted afterwards. */
    }

    assert(p.before(Phase::down, 0) == Result::pass);
    p.before(Phase::up, 0);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}

// Presentation uses the sealed parser cache, independently for each display and at draw time.
void presentation_off_regression() {
    std::uint64_t now = 100;
    Json reply;
    PassionNativePower p(
            "session",
            [&](unsigned, const std::string &) {
                return reply;
            },
            [] {},
            [&] {
                return now;
            });
    const std::array<bool, 2> on{false, false}, primary{true, false}, secondary{false, true},
            both{true, true};
    auto state = [](unsigned display, int power) {
        return Json{{"session_id", "session"},
                    {"boot_id", "boot"},
                    {"watch_id", "watch"},
                    {"display", display},
                    {"unique_id", display ? "virtual:com.android.emulator.multidisplay:1234562"
                                          : "local:8141241408256768"},
                    {"kind", "applied"},
                    {"state", power},
                    {"request", 1},
                    {"transaction", 1},
                    {"order", 1},
                    {"observed_ms", 1000},
                    {"expires_monotonic_us", 2000000}};
    };
    auto runtime = [&](int a, int b) {
        return Json{{"session_id", "session"},
                    {"deliveries",
                     {{"android",
                       {{"display_power",
                         {{"schema", 1},
                          {"protocol", "passion-display-power-v1"},
                          {"session_id", "session"},
                          {"boot_id", "boot"},
                          {"watch_id", "watch"},
                          {"capability", PassionPowerOwner::capability()},
                          {"states", Json::array({state(0, a), state(1, b)})}}}}}}}};
    };

    assert(p.off_displays() == on);

    for (auto [a, b, expected] : {std::tuple{2, 2, on}, std::tuple{1, 2, primary},
                                  std::tuple{2, 1, secondary}, std::tuple{1, 1, both}}) {
        p.observe(runtime(a, b));
        assert(p.off_displays() == expected);
    }

    now = 2000000;
    assert(p.off_displays() == on);
    now = 100;

    for (const auto &field : {"session_id", "boot_id", "watch_id", "unique_id"}) {
        auto invalid = runtime(1, 1);
        invalid["deliveries"]["android"]["display_power"]["states"][0][field] = "foreign";
        bool rejected = false;
        try {
            p.observe(invalid);
        } catch (...) {
            rejected = true;
        }

        assert(rejected && p.off_displays() == on);
    }

    auto invalid = runtime(1, 1);
    invalid["deliveries"]["android"]["display_power"]["states"][1]["expires_monotonic_us"] = now;
    try {
        p.observe(invalid);
        assert(false);
    } catch (...) { /* Ignored: the expected rejection is asserted afterwards. */
    }

    assert(p.off_displays() == on);
    p.observe(runtime(1, 1));
    reply = state(0, 2);
    reply.erase("display");
    reply["display_id"] = 0;
    reply["ok"] = true;
    reply["kind"] = "wake_observed";
    reply["request"] = 2;
    reply["transaction"] = 2;
    reply["order"] = 2;
    assert(p.before(PassionPointerWake::Phase::down, 0) == PassionPointerWake::Result::consumed);
    assert(p.off_displays() == secondary);
    p.before(PassionPointerWake::Phase::up, 0);
    // A replayed pre-wake record cannot hide the retained active display again.
    p.observe(runtime(1, 1));
    assert(p.off_displays() == secondary);
    p.observe(Json{{"session_id", "session"}, {"deliveries", {{"android", Json::object()}}}});
    assert(p.off_displays() == on);
}
