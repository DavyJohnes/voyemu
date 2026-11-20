#include "passion_power_owner.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::media;

int main() try {
    PassionPointerWake::Binding b{true, "session", "boot", "watch"};
    Json e = {{"schema", 1},
              {"kind", "display_state_event"},
              {"session_id", "session"},
              {"watch_id", "watch"},
              {"boot_id", "boot"},
              {"sequence", 1},
              {"guest_time_ms", 1000},
              {"states", Json::array({{{"display", 0},
                                       {"unique_id", "local:8141241408256768"},
                                       {"state", 2},
                                       {"kind", "observed"},
                                       {"request", 0},
                                       {"transaction", 0},
                                       {"order", 1},
                                       {"observed_ms", 1000}}})}};
    PassionDisplayCache c(b);

    assert(c.accept(e, 100, 1000));
    assert(c.diagnostic()["reason"] == "admitted" && c.get(0, 100).value().state == 2);
    auto empty = e;
    empty["sequence"] = 2;
    empty["states"] = Json::array();
    assert(c.accept(empty, 200, 1000));
    assert(!c.get(0, 200) && c.diagnostic()["reason"] == "empty_snapshot");
    auto stale = e;
    stale["sequence"] = 3;
    stale["states"][0]["observed_ms"] = 4000;
    assert(c.accept(stale, 300, 1000));
    assert(!c.get(0, 300) && c.diagnostic()["reason"] == "no_fresh_samples");
    auto wrong = e;
    wrong["boot_id"] = "foreign";
    assert(!c.accept(wrong, 400, 1000));
    assert(c.diagnostic()["reason"] == "envelope");
    auto invalid = e;
    invalid["sequence"] = 4;
    invalid["states"][0]["unique_id"] = "local:replacement";
    assert(!c.accept(invalid, 500, 1000) && c.diagnostic()["reason"] == "invalid_state");
    std::string raw = "  " + e.dump();
    bool alive = true;
    unsigned reads = 0, commands = 0;
    auto launch = [&](const std::vector<std::string> &) {
        return PassionWatchStream::Child{[&] {
                                             return alive;
                                         },
                                         [&](std::size_t) {
                                             return reads++ ? std::string{} : raw + "\n";
                                         },
                                         [&] {
                                             alive = false;
                                         },
                                         [] {}};
    };
    PassionPowerOwner owner(
            b, [] {}, launch,
            [&](const std::vector<std::string> &, int) {
                ++commands;

                return Json{};
            },
            [] {
                return 100ULL;
            },
            [] {
                return 699LL;
            });
    PassionDisplayCache old_clock(b);
    assert(old_clock.accept(e, 100, 699));
    assert(!old_clock.get(0, 100));
    owner.poll();
    auto record = owner.record();
    assert(record["last_raw_state_event"] == raw);
    assert(record["watch_diagnostic"]["reason"] == "admitted");
    assert(record["states"].size() == 1);
    assert(!owner.gesture(0) && commands == 0);
    owner.close();
    assert(record["last_raw_state_maximum_bytes"] == 4096);

    for (bool stale_reply : {false, true}) {
        std::uint64_t now = 100;
        bool live = true;
        unsigned reads = 0;
        auto off = e;
        off["states"][0]["state"] = 1;
        PassionPowerOwner waking(
                b, [] {},
                [&](const std::vector<std::string> &) {
                    return PassionWatchStream::Child{[&] {
                                                         return live;
                                                     },
                                                     [&](std::size_t) {
                                                         return reads++ ? std::string{}
                                                                        : off.dump() + "\n";
                                                     },
                                                     [&] {
                                                         live = false;
                                                     },
                                                     [] {}};
                },
                [&](const std::vector<std::string> &, int) {
                    now += 100000;

                    return Json{{"ok", true},
                                {"kind", "wake_observed"},
                                {"boot_id", "boot"},
                                {"display", 0},
                                {"unique_id", "local:8141241408256768"},
                                {"state", 2},
                                {"request", 1},
                                {"transaction", 2},
                                {"order", 2},
                                {"observed_ms", stale_reply ? 1000 : 2100}};
                },
                [&] {
                    return now;
                },
                [] {
                    return 699LL;
                });
        waking.poll();
        now += 1000000;
        bool failed = false;
        try {
            auto result = waking.gesture(0);
            assert(result && result->at("display_wake_completed") == true);
        } catch (...) {
            failed = true;
        }

        assert(failed == stale_reply);
        waking.close();
    }

    bool future_alive = true;
    unsigned future_reads = 0;
    auto future = e;
    future["states"][0]["observed_ms"] = 1251;
    PassionPowerOwner future_owner(
            b, [] {},
            [&](const std::vector<std::string> &) {
                return PassionWatchStream::Child{[&] {
                                                     return future_alive;
                                                 },
                                                 [&](std::size_t) {
                                                     return future_reads++ ? std::string{}
                                                                           : future.dump() + "\n";
                                                 },
                                                 [&] {
                                                     future_alive = false;
                                                 },
                                                 [] {}};
            },
            [](const std::vector<std::string> &, int) {
                return Json{};
            },
            [] {
                return 100ULL;
            },
            [] {
                return 699LL;
            });
    bool future_rejected = false;
    try {
        future_owner.poll();
    } catch (...) {
        future_rejected = true;
    }

    assert(future_rejected && !future_alive && future_owner.record()["states"].empty());
    bool bad_alive = true, preserved = false;
    std::string captured;
    unsigned bad_reads = 0;
    PassionWatchStream bounded(
            b,
            [&](const std::vector<std::string> &) {
                return PassionWatchStream::Child{[&] {
                                                     return bad_alive;
                                                 },
                                                 [&](std::size_t) {
                                                     return bad_reads++
                                                                    ? std::string{}
                                                                    : std::string(4097, ' ') + "\n";
                                                 },
                                                 [&] {
                                                     bad_alive = false;
                                                 },
                                                 [&] {
                                                     preserved = true;
                                                 }};
            },
            [] {}, [](const Json &) {}, [] {},
            [&](const std::string &x) {
                captured = x;
            });
    bool rejected = false;
    try {
        bounded.poll(0);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && preserved && captured.empty() && !bad_alive);
    std::cout
            << "cache reasons, exact bounded last raw event, no fake state/ACK, ON zero command PASS\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
