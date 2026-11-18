#include "passion_power_owner.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::media;

int main() try {
    PassionPointerWake::Binding b{true, "passion-11-russian-20260403-2.1.4.80", "session", "boot",
                                  "watch"};
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
                return 1000LL;
            });
    owner.poll();
    auto record = owner.record();
    assert(record["last_raw_state_event"] == raw);
    assert(record["watch_diagnostic"]["reason"] == "admitted");
    assert(record["states"].size() == 1);
    assert(!owner.gesture(0) && commands == 0);
    owner.close();
    assert(record["last_raw_state_maximum_bytes"] == 4096);
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
