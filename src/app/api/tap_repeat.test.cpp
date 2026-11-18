#include "tap_repeat.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::api;

int main() try {
    assert(tap_repeat(Json::object()).count == 1 && tap_repeat(Json::object()).interval_ms == 100);
    unsigned calls = 0, delays = 0, total_delay = 0;
    auto dispatch = [&] {
        ++calls;

        return Json{{"sdk_completed", true}};
    };
    auto delay = [&](unsigned ms) {
        ++delays;
        total_delay += ms;
    };
    auto result = repeat_taps(tap_repeat({{"count", 5}, {"interval_ms", 100}}), dispatch, delay);
    assert(calls == 5 && delays == 4 && total_delay == 400 && result["dispatched_count"] == 5 &&
           result["requested_count"] == 5);
    calls = delays = 0;
    result = repeat_taps(tap_repeat({{"count", 8}, {"interval_ms", 0}}), dispatch, delay);
    assert(calls == 8 && delays == 0);

    for (const Json &bad :
         {Json(0), Json(9), Json(-1), Json(true), Json("5"), Json(1.0), Json(nullptr)}) {
        bool rejected = false;
        try {
            (void)tap_repeat({{"count", bad}});
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    }

    for (const Json &bad :
         {Json(-1), Json(301), Json(true), Json("100"), Json(1.0), Json(nullptr)}) {
        bool rejected = false;
        try {
            (void)tap_repeat({{"interval_ms", bad}});
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    }

    calls = delays = 0;
    bool partial = false;
    try {
        repeat_taps(
                {5, 100},
                [&]() -> Json {
                    if (++calls == 3) {
                        throw std::runtime_error("MEDIA_RPC_UNKNOWN: failed");
                    }

                    return Json::object();
                },
                delay);
    } catch (const std::exception &e) {
        partial = std::string(e.what()).starts_with(
                "TEST_INPUT_PARTIAL_UNKNOWN: dispatched_count=2;");
    }

    assert(partial && calls == 3 && delays == 2);
    calls = 0;
    bool unchanged = false;
    try {
        repeat_taps(
                {5, 100},
                [&]() -> Json {
                    ++calls;
                    throw std::runtime_error("MEDIA_RPC_UNKNOWN: first failure");
                },
                delay);
    } catch (const std::exception &e) {
        unchanged = std::string(e.what()) == "MEDIA_RPC_UNKNOWN: first failure";
    }

    assert(unchanged && calls == 1);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
