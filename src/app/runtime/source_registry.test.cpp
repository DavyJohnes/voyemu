#include "source_registry.hpp"
#include "error.hpp"
#include <cassert>
#include <exception>
#include <future>
#include <iostream>
using namespace voyemu;
using namespace voyemu::runtime;

int main() try {
    WatchHub hub;
    SourceCoordinator coordinator(hub);
    std::mutex mutex;
    std::condition_variable changed;
    int entered = 0;
    bool release = false;
    auto collector = [&](const SourceContext &context) {
        std::unique_lock lock(mutex);
        ++entered;
        changed.notify_all();
        changed.wait_until(lock, context.deadline, [&] {
            return release || context.cancelled.load();
        });

        return SourceSample{"ready", "instance", "boot", "1", Json{{"enabled", true}},
                            {},      false,      false};
    };

    coordinator.add(
            {"ui", collector, std::chrono::milliseconds(60000), std::chrono::milliseconds(1000)});
    coordinator.add(
            {"can", collector, std::chrono::milliseconds(60000), std::chrono::milliseconds(1000)});
    bool duplicate = false;
    try {
        coordinator.add({"ui", collector});
    } catch (const VoyemuError &error) {
        duplicate = error.code() == "PROVIDER_REGISTRATION";
    }

    assert(duplicate);
    coordinator.start();
    {
        std::unique_lock lock(mutex);
        assert(changed.wait_for(lock, std::chrono::seconds(1), [&] {
            return entered == 2;
        }));
    }
    auto partial = coordinator.snapshot(std::chrono::milliseconds(0));
    assert(partial["status"] == "timeout" && !partial["complete"].get<bool>());
    {
        std::lock_guard lock(mutex);
        release = true;
        changed.notify_all();
    }
    auto snapshot = coordinator.snapshot(std::chrono::milliseconds(1500));
    assert(snapshot["status"] == "ready" && snapshot["complete"].get<bool>() &&
           !snapshot["atomic"].get<bool>());
    assert(snapshot["sources"]["ui"]["value"]["enabled"] == true);
    auto watch =
            hub.start(Json{{"op", "eq"}, {"path", "/sources/ui/value/enabled"}, {"value", true}},
                      1000, SourceCoordinator::now_ms());
    assert(watch.status == "ready" && watch.result.status == "passed");
    auto negative = hub.start(
            Json{{"op", "never"},
                 {"condition",
                  {{"op", "eq"}, {"path", "/sources/ui/value/enabled"}, {"value", false}}},
                 {"duration_ms", 1}},
            1000, SourceCoordinator::now_ms());
    assert(negative.result.status == "unavailable");
    coordinator.stop();
    assert(coordinator.snapshot(std::chrono::milliseconds(0))["status"] == "unavailable");
    WatchHub unavailable_hub;
    SourceCoordinator unavailable(unavailable_hub);
    unavailable.add({"system", [](const SourceContext &) -> SourceSample {
                         throw std::runtime_error("failure");
                     }});
    unavailable.start();
    auto failed = unavailable.snapshot(std::chrono::milliseconds(1000));
    assert(!failed["complete"].get<bool>() &&
           failed["sources"]["system"]["status"] == "unavailable");
    unavailable.stop();
    WatchHub events_hub;
    SourceCoordinator events(events_hub);
    std::atomic<bool> replacement = false;
    events.add({"can",
                [&](const SourceContext &) {
                    return SourceSample{
                            "ready",          replacement ? "next" : "first", "boot", "2",
                            Json{{"raw", 5}}, {Json{{"kind", "write"}}},      true,   false};
                },
                std::chrono::milliseconds(60000)});
    events.start();
    assert(events.snapshot(std::chrono::milliseconds(1000))["complete"] == true);
    auto event_watch = events_hub.start(
            Json{{"op", "ordered"},
                 {"args",
                  Json::array({Json{{"op", "eq"}, {"path", "/event/kind"}, {"value", "write"}}})}},
            1000, SourceCoordinator::now_ms());
    assert(event_watch.status == "ready" && event_watch.result.status != "passed");
    events.snapshot(std::chrono::milliseconds(1000));
    auto event_read = events_hub.read(event_watch.watch_id, event_watch.cursor, {}, 32,
                                      SourceCoordinator::now_ms());
    assert(event_read.result.status == "passed");
    replacement = true;
    events.snapshot(std::chrono::milliseconds(1000));
    auto boundary = events_hub.read(event_watch.watch_id, event_read.cursor, {}, 32,
                                    SourceCoordinator::now_ms());
    assert(boundary.result.status == "unavailable");
    events.stop();
    WatchHub combined, can_only;
    SourceCoordinator projected(combined);
    std::atomic<bool> journal_gap = false, sampled = false;
    std::atomic<std::int64_t> after = 0;
    projected.add({"can",
                   [&](const SourceContext &context) {
                       std::mutex wait_mutex;
                       std::condition_variable wait_cv;
                       std::unique_lock wait_lock(wait_mutex);
                       wait_cv.wait_until(wait_lock,
                                          std::chrono::steady_clock::time_point(
                                                  std::chrono::milliseconds(after.load())));
                       SourceSample sample{"ready",          "can-service",
                                           "boot",           "3",
                                           Json{{"raw", 5}}, {},
                                           !sampled.load(),  journal_gap.load()};
                       sample.covered_through_ms = SourceCoordinator::now_ms();

                       return sample;
                   },
                   std::chrono::milliseconds(60000)});
    projected.add({"system",
                   [](const SourceContext &) {
                       return SourceSample{"ready",        "system", "boot", "1",
                                           Json::object(), {},       false,  false};
                   },
                   std::chrono::milliseconds(60000)});
    projected.publish_to(can_only, {"can"});
    projected.start();
    assert(projected.snapshot(std::chrono::milliseconds(1000))["complete"] == true);
    Json no_write = {{"op", "never"},
                     {"condition", {{"op", "eq"}, {"path", "/event/kind"}, {"value", "write"}}},
                     {"duration_ms", 1}};
    auto no_write_watch = can_only.start(no_write, 1000, SourceCoordinator::now_ms());
    assert(no_write_watch.status == "ready");
    auto mixed_watch = combined.start(no_write, 1000, SourceCoordinator::now_ms());
    assert(mixed_watch.result.status == "unavailable");
    after = SourceCoordinator::now_ms() + 3;
    projected.snapshot(std::chrono::milliseconds(1000));
    auto covered = can_only.read(no_write_watch.watch_id, no_write_watch.cursor, {}, 32,
                                 SourceCoordinator::now_ms());
    assert(covered.result.status == "passed");
    sampled = true;
    projected.snapshot(std::chrono::milliseconds(1000));
    auto unproven = can_only.start(no_write, 1000, SourceCoordinator::now_ms());
    assert(unproven.result.status == "unavailable");
    sampled = false;
    journal_gap = true;
    projected.snapshot(std::chrono::milliseconds(1000));
    assert(can_only.read(no_write_watch.watch_id, covered.cursor, {}, 32,
                         SourceCoordinator::now_ms())
                   .result.status == "unavailable");
    projected.stop();

    // An unavailable unrelated source does not make a requested subset incomplete.
    WatchHub subset_hub, provider_hub;
    SourceCoordinator subset(subset_hub);
    std::atomic<unsigned> can_reads = 0, provider_reads = 0;
    subset.add({"can",
                [&](const SourceContext &) {
                    ++can_reads;

                    return SourceSample{"ready",          "can", "boot", "1",
                                        Json{{"raw", 5}}, {},    false,  false};
                },
                std::chrono::milliseconds(60000)});
    subset.add({"project",
                [&](const SourceContext &) {
                    ++provider_reads;

                    return SourceSample{};
                },
                std::chrono::milliseconds(60000)});
    subset.start();
    assert(subset.snapshot(std::chrono::milliseconds(1000))["complete"] == false);
    const auto provider_before = provider_reads.load();
    const auto only_can = subset.snapshot(std::chrono::milliseconds(1000), {"can"});
    assert(only_can["complete"] == true && only_can["sources"].size() == 1 &&
           provider_reads == provider_before);
    assert(subset.select(Json::array({"project", "can"})) ==
           std::vector<std::string>({"can", "project"}));

    for (const auto &selection :
         std::vector<Json>{Json::array(), Json::array({"can", "can"}), Json::array({"unknown"}),
                           Json::array({3}), Json("can")}) {
        bool rejected = false;
        try {
            subset.select(selection);
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        assert(rejected);
    }

    // A projection can be registered after workers start without adding a collector.
    subset.publish_to(provider_hub, {"project"});
    const auto only_provider = subset.snapshot(std::chrono::milliseconds(1000), {"project"});
    assert(only_provider["sources"].size() == 1 && only_provider["complete"] == false);
    auto missing_provider =
            provider_hub.start(Json{{"op", "present"}, {"path", "/sources/project/value"}}, 1000,
                               SourceCoordinator::now_ms());
    assert(missing_provider.result.status == "unavailable");
    provider_hub.close();
    subset.remove_projection(provider_hub);
    // The removed destination is no longer dereferenced by later publications.
    assert(subset.snapshot(std::chrono::milliseconds(1000), {"can"})["complete"] == true);
    subset.stop();
    assert(!subset.demanded("can"));

    WatchHub partial_system_hub;
    SourceCoordinator partial_system(partial_system_hub);
    const Json partial_system_value = {
            {"processes", Json::array({{{"pid", 17}, {"name", "system_server"}}})},
            {"activities",
             {{"status", "ready"},
              {"complete", true},
              {"items", Json::array({{{"user", 0}, {"component", "oem/.Settings"}}})}}},
            {"services",
             {{"status", "partial"},
              {"complete", false},
              {"malformed_records", 2},
              {"items", Json::array({{{"user", 0}, {"component", "oem/.Service"}}})}}},
            {"complete", false}};
    partial_system.add({"system",
                        [&](const SourceContext &) {
                            return SourceSample{
                                    "partial", "system", "boot", "1", partial_system_value,
                                    {},        true,     false};
                        },
                        std::chrono::milliseconds(60000)});
    partial_system.start();
    const auto partial_state = partial_system.snapshot(std::chrono::milliseconds(1000), {"system"});
    assert(partial_state["status"] == "ready" && partial_state["fresh"] == true &&
           partial_state["complete"] == false);
    assert(partial_state["sources"]["system"]["status"] == "partial" &&
           partial_state["sources"]["system"]["value"] == partial_system_value);
    assert(partial_state["sources"]["system"]["coverage"]["continuous"] == false);
    auto partial_negative = partial_system_hub.start(
            Json{{"op", "absent"}, {"path", "/sources/system/value/services/items/1"}}, 1000,
            SourceCoordinator::now_ms());
    assert(partial_negative.result.status == "unavailable");
    partial_system.stop();

    WatchHub ids_hub;
    SourceCoordinator ids(ids_hub);
    auto empty = [](const SourceContext &) {
        return SourceSample{};
    };
    ids.add({"project-provider_2", empty});
    ids.add({std::string(64, 97), empty});

    for (const auto &id :
         std::vector<std::string>{"", "Upper", "0start", "_start", "-start", "a/b", "a~b", "a.b",
                                  std::string(65, 97), std::string(1, char(0xff))}) {
        bool rejected = false;
        try {
            ids.add({id, empty});
        } catch (const VoyemuError &error) {
            rejected = error.code() == "PROVIDER_REGISTRATION";
        }

        assert(rejected);
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
