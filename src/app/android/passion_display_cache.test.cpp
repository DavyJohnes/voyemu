#include "passion_display_cache.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::media;

int main() try {
    PassionPointerWake::Binding binding{true, "session", "boot", "watch"};
    PassionDisplayCache cache(binding);
    Json event = {{"schema", 1},
                  {"kind", "display_state_event"},
                  {"session_id", "session"},
                  {"watch_id", "watch"},
                  {"boot_id", "boot"},
                  {"sequence", 1},
                  {"states",
                   Json::array({{{"display", 0},
                                 {"unique_id", "local:8141241408256768"},
                                 {"state", 2},
                                 {"kind", "observed"},
                                 {"request", 0},
                                 {"transaction", 0},
                                 {"observed_ms", 1000},
                                 {"order", 1}},
                                {{"display", 1},
                                 {"unique_id", "virtual:com.android.emulator.multidisplay:1234562"},
                                 {"state", 1},
                                 {"kind", "applied"},
                                 {"request", 2},
                                 {"transaction", 3},
                                 {"observed_ms", 1000},
                                 {"order", 1}}})}};

    assert(cache.accept(event, 100, 1000));
    assert(cache.get(0, 100).value().state == 2 && cache.get(1, 100).value().state == 1);
    PassionDisplayProof wake{
            "session", "boot", "watch", "local:8141241408256768", "wake_observed", 0, 2, 4, 5,
            2500100,   5,      1001};
    assert(cache.confirmWake(wake, 100));
    auto delayed = event;
    delayed["sequence"] = 2;
    delayed["states"][0]["state"] = 1;
    assert(cache.accept(delayed, 150, 1001) && cache.get(0, 150).value().state == 2 &&
           cache.get(1, 150).value().state == 1);
    auto foreign = wake;
    foreign.boot = "other";
    assert(!cache.confirmWake(foreign, 150));
    auto old = event;
    old["states"][0]["state"] = 1;
    assert(!cache.accept(old, 200, 1000) && cache.get(0, 200).value().state == 2);
    auto wrong = event;
    wrong["sequence"] = 2;
    wrong["boot_id"] = "foreign";
    assert(!cache.accept(wrong, 300, 1000) && !cache.get(0, 300));
    event["sequence"] = 3;
    assert(cache.accept(event, 400, 1000));
    assert(!cache.get(0, 2500500));
    event["sequence"] = 4;
    event["states"] = Json::array();
    assert(cache.accept(event, 500, 1000) && !cache.get(0, 500));
    event["sequence"] = 5;
    event["states"] = old["states"];
    event["states"][0]["observed_ms"] = -1;
    assert(cache.accept(event, 600, 1000) && !cache.get(0, 600) && cache.get(1, 600));
    event["sequence"] = 6;
    event["states"][1]["transaction"] = 0;
    assert(!cache.accept(event, 700, 1000) && !cache.get(1, 700));
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
