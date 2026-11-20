#include "passion_pointer_wake.hpp"
#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
using Gate = voyemu::media::PassionPointerWake;
using Proof = voyemu::media::PassionDisplayProof;

int main() try {
    unsigned cached = 0, wakes = 0, sdk = 0, rechecks = 0, last = 9;
    bool unknown = false, stale = false, bare = false, wrong_wake = false;
    int state[2] = {2, 2};
    auto value = [&](unsigned d, bool wake) {
        return Proof{"session",
                     (stale || (wake && wrong_wake)) ? "stale" : "boot",
                     "watch",
                     d == 0 ? "local:8141241408256768"
                            : "virtual:com.android.emulator.multidisplay:1234562",
                     wake ? "wake_observed" : "applied",
                     d,
                     state[d],
                     wake && !bare ? 2u : 1u,
                     wake && !bare ? 2u : 1u,
                     1000,
                     wake ? 2u : 1u,
                     1000};
    };
    auto cache = [&](unsigned d) -> std::optional<Proof> {
        ++cached;

        if (unknown) {
            return {};
        }

        return value(d, false);
    };
    auto wake = [&](unsigned d) {
        ++wakes;
        last = d;

        if (!bare) {
            state[d] = 2;
        }

        return value(d, true);
    };
    auto recheck = [&] {
        ++rechecks;
    };
    auto now = []() {
        return 100u;
    };
    Gate::Binding binding{true, "session", "boot", "watch"};
    Gate mouse(binding, cache, wake, recheck, now);
    auto input = [&](Gate::Phase phase, unsigned d) {
        auto r = mouse.before(phase, d);

        if (r == Gate::Result::pass) {
            ++sdk;
        }

        return r;
    };

    for (int i = 0; i < 100; i++) {
        assert(input(Gate::Phase::down, 0) == Gate::Result::pass);
        input(Gate::Phase::up, 0);
    }

    assert(wakes == 0 && rechecks == 0 && sdk == 200 && cached == 100);
    state[0] = 1;
    assert(input(Gate::Phase::down, 0) == Gate::Result::consumed);
    input(Gate::Phase::move, 0);
    input(Gate::Phase::up, 0);
    assert(wakes == 1 && last == 0 && sdk == 200 && state[1] == 2);
    unknown = true;
    auto start = std::chrono::steady_clock::now();
    bool failed = false;
    try {
        input(Gate::Phase::down, 0);
    } catch (const std::runtime_error &) {
        failed = true;
    }

    assert(failed && rechecks == 1 &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50));
    input(Gate::Phase::move, 0);
    input(Gate::Phase::up, 0);
    assert(sdk == 200 && wakes == 1);
    unknown = false;
    stale = true;
    failed = false;
    try {
        input(Gate::Phase::down, 0);
    } catch (const std::runtime_error &) {
        failed = true;
    }

    assert(failed && rechecks == 2);
    input(Gate::Phase::up, 0);
    stale = false;
    state[1] = 1;
    bare = true;
    failed = false;
    try {
        input(Gate::Phase::down, 1);
    } catch (const std::runtime_error &) {
        failed = true;
    }

    assert(failed);
    input(Gate::Phase::move, 1);
    input(Gate::Phase::up, 1);
    assert(sdk == 200);
    bare = false;
    state[0] = 1;
    wrong_wake = true;
    failed = false;
    try {
        input(Gate::Phase::down, 0);
    } catch (const std::runtime_error &) {
        failed = true;
    }

    assert(failed && last == 0);
    input(Gate::Phase::move, 0);
    input(Gate::Phase::up, 0);
    assert(sdk == 200);
    wrong_wake = false;
    Gate api(binding, cache, wake, recheck, now);
    assert(api.before(Gate::Phase::down, 1) == Gate::Result::consumed);

    for (int i = 0; i < 10; i++) {
        assert(api.before(Gate::Phase::move, 1) == Gate::Result::consumed);
    }

    api.before(Gate::Phase::up, 1);
    assert(last == 1 && state[0] == 2);
    assert(api.before(Gate::Phase::down, 1) == Gate::Result::pass);
    api.before(Gate::Phase::up, 1);
    Gate disabled({false, "free-09-chinese", "", "", ""}, cache, wake, recheck, now);
    assert(disabled.before(Gate::Phase::down, 0) == Gate::Result::pass);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
