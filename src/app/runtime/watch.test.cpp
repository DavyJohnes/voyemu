#include "watch.hpp"
#include "error.hpp"
#include <cassert>
#include <exception>
#include <future>
#include <iostream>
using namespace voyemu;
using namespace voyemu::runtime;

int main() try {
    const Json changed = {{"op", "changed"}, {"path", "/x"}};
    ConditionObservation observation{Json{{"x", 0}}, 0, "boot", {""}, true, false};
    WatchHub hub(2);

    assert(hub.start(changed, 100, 0).status == "unavailable");
    hub.publish(observation);
    auto ready = hub.start(changed, 100, 0);
    assert(ready.status == "ready" && ready.result.status == "pending");
    observation.state["x"] = 1;
    observation.monotonic_ms = 1;
    hub.publish(observation);
    observation.state["x"] = 0;
    observation.monotonic_ms = 2;
    hub.publish(observation);
    auto read = hub.read(ready.watch_id, ready.cursor, std::chrono::milliseconds(0), 1, 2);
    assert(read.result.status == "passed" && read.events.size() == 1 && read.cursor == "2");
    read = hub.read(ready.watch_id, read.cursor, std::chrono::milliseconds(0), 1, 2);
    assert(read.events.size() == 1);
    observation.monotonic_ms = 3;
    hub.publish(observation);
    assert(hub.read(ready.watch_id, ready.cursor, {}, 10, 3).gap);
    auto stopped = hub.start(changed, 100, 3);
    auto reader = std::async(std::launch::async, [&] {
        return hub.read(stopped.watch_id, stopped.cursor, std::chrono::seconds(5), 10, 3);
    });
    hub.stop(stopped.watch_id);
    assert(reader.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(reader.get().status == "ended");
    auto leased = hub.start(changed, 10, 3);
    auto expiring = std::async(std::launch::async, [&] {
        return hub.read(leased.watch_id, leased.cursor, std::chrono::seconds(5), 10, 3);
    });
    hub.tick(13);
    assert(expiring.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(expiring.get().status == "ended");
    auto epoch = hub.start(changed, 100, 3);
    observation.epoch = "new";
    observation.monotonic_ms = 4;
    hub.publish(observation);
    assert(hub.read(epoch.watch_id, epoch.cursor, {}, 10, 4).result.status == "unavailable");
    WatchHub unknown;
    observation.epoch = "boot";
    observation.gap = false;
    observation.available.clear();
    unknown.publish(observation);
    assert(unknown.start(changed, 100, 4).status == "unavailable");
    observation.available = {""};
    observation.continuous = false;
    unknown.publish(observation);
    auto negative = unknown.start(Json{{"op", "never"},
                                       {"condition", {{"op", "eq"}, {"path", "/x"}, {"value", 9}}},
                                       {"duration_ms", 1}},
                                  100, 4);
    assert(negative.result.status == "unavailable");
    unknown.close();
    assert(unknown.read(negative.watch_id, negative.cursor, {}, 1, 4).status == "ended");
    WatchHub leases;
    observation.available = {""};
    observation.gap = false;
    leases.publish(observation);
    auto scoped_lease = leases.start(changed, 10, 100);
    assert(!leases.epoch().empty());
    assert(leases.active(109) == 1);
    assert(leases.active(110) == 0);
    assert(leases.read(scoped_lease.watch_id, scoped_lease.cursor, {}, 1, 110).status == "ended");
    // A malformed client condition, cursor or lease is the client's argument error.
    const auto client_error = [](const auto &call) {
        try {
            call();
        } catch (const VoyemuError &error) {
            return error.code() == "TEST_ARGUMENT";
        }

        return false;
    };
    assert(client_error([&] {
        leases.start(Json{{"op", "not-a-condition"}}, 10, 110);
    }));
    assert(client_error([&] {
        leases.start(changed, 0, 110);
    }));
    const auto live = leases.start(changed, 1000, 110);
    assert(live.status == "ready" && client_error([&] {
               leases.read(live.watch_id, "01", {}, 1, 110);
           }));
    assert(client_error([&] {
        leases.read(live.watch_id, "999", {}, 1, 110);
    }));
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
