#include "event_journal.hpp"
#include <cassert>
#include <exception>
#include <future>
#include <iostream>
using namespace voyemu;
using namespace voyemu::runtime;

int main() try {
    EventJournal journal(2);
    ConditionObservation b{Json{{"x", 0}}, 0, "a", {""}, true, false};

    journal.baseline(b);
    const auto ready = journal.ready();
    assert(ready.cursor == "0");
    auto reader = std::async(std::launch::async, [&] {
        return journal.read(ready.cursor, std::chrono::seconds(5));
    });
    b.monotonic_ms = 1;
    journal.publish(b, {{"kind", "write"}});
    auto replay = reader.get();
    assert(!replay.gap && replay.events.size() == 1 && replay.cursor == "1");
    b.monotonic_ms = 2;
    journal.publish(b, {});
    b.monotonic_ms = 3;
    journal.publish(b, {});
    replay = journal.read("0");
    assert(replay.gap && replay.events.size() == 2);
    journal.baseline(b);
    assert(journal.read("3").reset);
    auto armed = journal.ready();
    assert(journal.read(armed.cursor).events.empty());
    b.epoch = "b";
    journal.publish(b, {});
    assert(journal.read(armed.cursor).reset);
    auto cursor = journal.ready().cursor;
    journal.close();
    assert(journal.read(cursor).gap);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
