#include "passion_session_power.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::media;

int main() try {
    unsigned fences = 0, wakes = 0, rechecks = 0;
    bool changed = false;
    PassionPointerWake::Binding b{true, "passion-11-russian-20260403-2.1.4.80", "session", "boot",
                                  "watch"};
    PassionSessionPower s(
            b,
            [&] {
                ++fences;

                if (changed) {
                    throw std::runtime_error("changed SDK");
                }
            },
            [&](unsigned d, const std::string &boot) {
                ++wakes;

                return PassionDisplayProof{"session",
                                           boot,
                                           "watch",
                                           "local:8141241408256768",
                                           "wake_observed",
                                           d,
                                           2,
                                           4,
                                           5,
                                           2000000,
                                           5,
                                           1001};
            },
            [&] {
                ++rechecks;
            },
            [] {
                return 250u;
            });
    Json e = {{"schema", 1},
              {"kind", "display_state_event"},
              {"session_id", "session"},
              {"watch_id", "watch"},
              {"boot_id", "boot"},
              {"sequence", 1},
              {"states", Json::array({{{"display", 0},
                                       {"unique_id", "local:8141241408256768"},
                                       {"state", 1},
                                       {"kind", "applied"},
                                       {"request", 1},
                                       {"transaction", 1},
                                       {"order", 1},
                                       {"observed_ms", 1000}}})}};

    assert(s.event(e, 100, 1000));
    assert(s.wake(0, "session", "boot", 200).state == 2 && wakes == 1 && fences == 3);

    for (int i = 0; i < 100; i++) {
        assert(s.cached(0, 300).value().state == 2);
        assert(!s.gesture(0, 300));
    }

    assert(fences == 3 && wakes == 1);
    bool rejected = false;
    try {
        s.wake(0, "foreign", "boot", 300);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && wakes == 1);
    s.lost();
    rejected = false;
    try {
        s.wake(0, "session", "boot", 300);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && rechecks == 1 && wakes == 1);
    changed = true;
    rejected = false;
    try {
        s.event(e, 400, 1000);
    } catch (...) {
        rejected = true;
    }

    assert(rejected);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
