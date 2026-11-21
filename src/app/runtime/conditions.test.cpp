#include "conditions.hpp"
#include "error.hpp"
#include <cassert>
#include <iostream>
using namespace voyemu;
using namespace voyemu::runtime;

int main() try {
    ConditionObservation b{Json{{"can", {{"x", 0}, {"list", Json::array({1, 2})}}}},
                           10,
                           "boot1",
                           {"/can"},
                           true,
                           false};
    auto eq = Json{{"op", "eq"}, {"path", "/can/x"}, {"value", 0}};
    auto check = [&](Json e, std::vector<ConditionObservation> events = {}, std::int64_t now = 10,
                     bool end = false) {
        return evaluate_condition(e, b, events, now, end).status;
    };

    assert(check(eq) == "passed");
    assert(check({{"op", "absent"}, {"path", "/can/missing"}}) == "passed");
    assert(check({{"op", "absent"}, {"path", "/ui/missing"}}) == "unavailable");
    assert(check({{"op", "eq"}, {"path", "/can/missing"}, {"value", false}}) == "unavailable");
    assert(check({{"op", "count"}, {"path", "/can/list"}, {"value", 2}}) == "passed");
    assert(check({{"op", "all"}, {"args", Json::array({eq, eq})}}) == "passed");
    auto one = eq;
    one["value"] = 1;
    assert(check(one, {}, 10, true) == "failed");
    auto event = b;
    event.monotonic_ms = 20;
    event.state["can"]["x"] = 1;
    assert(check({{"op", "changed"}, {"path", "/can/x"}}, {event}, 20) == "passed");
    auto restored = b;
    restored.monotonic_ms = 25;
    assert(check({{"op", "changed"}, {"path", "/can/x"}}, {event, restored}, 25) == "passed");
    auto ordered = Json{{"op", "ordered"}, {"args", Json::array({eq, one})}};
    assert(check(ordered, {event}, 20) == "pending");
    auto zero = b;
    zero.monotonic_ms = 15;
    assert(check(ordered, {zero, event}, 20) == "passed");
    auto first = Json{{"op", "first"}, {"args", Json::array({eq, one})}};
    assert(check(first, {event}, 20) == "failed");
    auto stable = Json{{"op", "stable"}, {"condition", eq}, {"duration_ms", 10}};
    auto watermark = b;
    watermark.monotonic_ms = 20;
    assert(check(stable, {watermark}, 20) == "passed");
    assert(check(stable, {}, 20) == "pending");
    assert(check(stable, {}, 20, true) == "unavailable");
    assert(check(stable, {event}, 20) == "failed");
    watermark.gap = true;
    assert(check(stable, {watermark}, 20) == "unavailable");
    watermark.gap = false;
    watermark.continuous = false;
    assert(check(stable, {watermark}, 20) == "unavailable");
    auto never = stable;
    never["op"] = "never";
    never["condition"] = one;
    assert(check(never, {event}, 20) == "failed");
    watermark.continuous = true;
    assert(check(never, {watermark}, 20) == "passed");
    // A baseline matching an event predicate is not an occurrence.
    never["condition"] = eq;
    auto later = event;
    later.monotonic_ms = 20;
    assert(check(never, {later}, 20) == "passed");
    // A journal watermark proves no event occurrence, not missing event data.
    auto event_base = b;
    event_base.available.push_back("/event");
    auto event_never =
            Json{{"op", "never"},
                 {"condition", {{"op", "eq"}, {"path", "/event/kind"}, {"value", "write"}}},
                 {"duration_ms", 10}};
    auto coverage = event_base;
    coverage.monotonic_ms = 20;
    coverage.event_watermark = true;
    assert(evaluate_condition(event_never, event_base, {coverage}, 20).status == "passed");
    auto uncovered = coverage;
    uncovered.event_watermark = false;
    assert(evaluate_condition(event_never, event_base, {uncovered}, 20).status == "unavailable");
    auto write = event_base;
    write.monotonic_ms = 15;
    write.state["event"] = {{"kind", "write"}};
    assert(evaluate_condition(event_never, event_base, {write, coverage}, 20).status == "failed");
    coverage.monotonic_ms = 25;
    assert(evaluate_condition(event_never, event_base, {coverage}, 25).status == "passed");
    auto absent_event = event_never;
    absent_event["condition"] = {{"op", "absent"}, {"path", "/event/kind"}};
    assert(evaluate_condition(absent_event, event_base, {coverage}, 25).status == "passed");
    coverage.gap = true;
    assert(evaluate_condition(event_never, event_base, {coverage}, 25).status == "unavailable");
    coverage.gap = false;
    coverage.epoch = "boot2";
    assert(evaluate_condition(event_never, event_base, {coverage}, 25).status == "unavailable");
    coverage.epoch = event_base.epoch;
    coverage.continuous = false;
    assert(evaluate_condition(event_never, event_base, {coverage}, 25).status == "unavailable");
    coverage.continuous = true;
    assert(evaluate_condition(event_never, event_base, {}, 20, true).status == "unavailable");
    assert(evaluate_condition(event_never, event_base, {coverage}, 25, true).status == "passed");
    coverage.monotonic_ms = 20;
    auto mixed = event_never;
    mixed["condition"] = {{"op", "any"}, {"args", Json::array({event_never["condition"], eq})}};
    assert(evaluate_condition(mixed, event_base, {coverage}, 25).status == "failed");
    mixed["condition"]["op"] = "all";
    assert(evaluate_condition(mixed, event_base, {coverage}, 25).status == "passed");
    auto reset = event;
    reset.epoch = "boot2";
    assert(check(one, {reset}, 20) == "unavailable");
    auto sampled = b;
    sampled.continuous = false;
    assert(evaluate_condition(never, sampled, {later}, 20).status == "unavailable");
    bool invalid = false;
    try {
        validate_condition({{"op", "eq"}, {"path", "/can/x"}, {"value", 0}, {"typo", 1}});
    } catch (const VoyemuError &error) {
        invalid = error.code() == "TEST_ARGUMENT";
    }

    assert(invalid);
    auto nested = eq;

    for (int i = 0; i < 18; ++i) {
        nested = {{"op", "all"}, {"args", Json::array({nested})}};
    }

    invalid = false;
    try {
        validate_condition(nested);
    } catch (const VoyemuError &error) {
        invalid = error.code() == "TEST_ARGUMENT";
    }

    assert(invalid);
    invalid = false;
    try {
        validate_condition({{"op", "count"}, {"path", "/can/x"}, {"value", -1}});
    } catch (const VoyemuError &error) {
        invalid = error.code() == "TEST_ARGUMENT";
    }

    assert(invalid);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
