#include "startup_ignition.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace voyemu;
namespace {
void check(bool value, const char *why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

unsigned ign(const VehicleModel &model) {
    return model.state().at("raw_oem").at("ign").get<unsigned>();
}

void apply(VehicleModel &model, const std::optional<StartupIgnition::Change> &change) {
    if (change) {
        model.patch(change->values);
    }
}

// The reason of a change, empty without one.
std::string reason(const std::optional<StartupIgnition::Change> &change) {
    return change ? change->reason : std::string();
}
} // namespace

int main() {
    try {
        const auto directory =
                std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("startup-ignition-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::remove_all(directory);
        ensure_private_directory(directory);
        VehicleModel model(directory / "vehicle-state.json");
        const auto start = StartupIgnition::Clock::now();
        StartupIgnition ignition;

        check(StartupIgnition::applies("sa8155-qinggan09") &&
                      StartupIgnition::applies("sa8155-qinggan11-611") &&
                      StartupIgnition::applies("sa8155-qinggan11-731") &&
                      !StartupIgnition::applies("sa8155-passion-21480") &&
                      !StartupIgnition::applies(""),
              "Free A9 and A11 clusters only");

        // The model, which every vehicle reading returns, shows ign 0 during the cold start and 1
        // after the runtime request; each change is a new revision.
        check(ign(model) == 1, "default vehicle has ignition on");
        const auto off = ignition.begin(model.state(), start);
        check(off && off->values == Json{{"ign", 0}} && off->reason == "cluster_cold_start" &&
                      ignition.pending(),
              "cold start switches ignition off");
        const auto revision = model.state().at("sequence").get<std::uint32_t>();
        apply(model, off);
        check(ign(model) == 0 && model.state().at("sequence") == revision + 1,
              "ignition off visible in the model");
        check(!ignition.marker("QMC BEGIN raw-OEM-QNX6-readonly", start) &&
                      !ignition.graphics(start) && ignition.pending(),
              "other guest markers and frames keep the sequence");
        check(!ignition.expire(start + StartupIgnition::frames_limit - std::chrono::seconds(1)),
              "no timeout before the guest timeline");
        const auto on = ignition.marker("QIP STARTUP ignition reason=gauge-check", start);
        check(on && on->values == Json{{"ign", 1}} && on->reason == "runtime_gauge-check" &&
                      !ignition.pending(),
              "runtime request switches ignition on");
        apply(model, on);
        check(ign(model) == 1 && model.state().at("sequence") == revision + 2,
              "ignition on visible in the model");
        check(!ignition.marker("QIP STARTUP ignition reason=gauge-check", start) &&
                      !ignition.expire(start + StartupIgnition::boot_limit) && !ignition.finish(),
              "one request per cold start");

        // Without a request the guest timeline switches ignition on: a lost request costs seconds,
        // inside the 6000 ms animation state of the OEM powermgr. Each bound runs from the guest
        // marker it follows and, except the animation bound, scales with the engine.
        const auto second = std::chrono::seconds(1);

        for (const auto engine : {QnxEngine::hvf, QnxEngine::tcg_file}) {
            StartupIgnition timeline(engine);
            const auto scaled = [&](std::chrono::seconds hvf) {
                return hvf * qnx_source_acceptance_seconds(engine) /
                       qnx_source_acceptance_seconds(QnxEngine::hvf);
            };
            const auto boot = scaled(StartupIgnition::boot_limit),
                       frames_limit = scaled(StartupIgnition::frames_limit),
                       gauge_limit = scaled(StartupIgnition::gauge_limit);
            check(engine == QnxEngine::tcg_file || (boot == std::chrono::seconds(300) &&
                                                    frames_limit == std::chrono::seconds(90) &&
                                                    gauge_limit == std::chrono::seconds(25)),
                  "HVF keeps its bounds");
            check(timeline.begin(model.state(), start) && !timeline.expire(start + boot - second) &&
                          reason(timeline.expire(start + boot)) == "boot_timeout",
                  "cold boot without frames");
            // The live TCG run of free-09-chinese-20260106-6.0.3 on Linux showed its first producer
            // about 4900 s after QEMU start, with new startup markers on the way.
            const auto progress = start + boot - second;
            check(timeline.begin(model.state(), start) &&
                          !timeline.marker("QMC BEGIN raw-OEM-QNX6-readonly", progress) &&
                          !timeline.expire(start + boot) &&
                          !timeline.expire(progress + boot - second) &&
                          reason(timeline.expire(progress + boot)) == "boot_timeout",
                  "startup markers extend the boot bound");
            check(engine == QnxEngine::hvf ||
                          (timeline.begin(model.state(), start) &&
                           !timeline.expire(start + std::chrono::seconds(4900))),
                  "TCG boot bound covers the measured first frames");
            check(timeline.begin(model.state(), start) &&
                          reason(timeline.graphics(start)) == "no_runtime_markers",
                  "frames without runtime markers switch ignition on at once");
            const auto frames = start + scaled(std::chrono::seconds(10)),
                       gauge = frames + scaled(std::chrono::seconds(20)),
                       ready = gauge + scaled(std::chrono::seconds(3));
            check(timeline.begin(model.state(), start) &&
                          !timeline.marker("QMC BEGIN raw-OEM-QNX6-readonly", start) &&
                          !timeline.graphics(frames) &&
                          !timeline.expire(frames + frames_limit - second) &&
                          reason(timeline.expire(frames + frames_limit)) == "frames_timeout",
                  "frames bound the wait");
            check(timeline.begin(model.state(), start) && !timeline.marker("QMC BEGIN", start) &&
                          !timeline.graphics(frames) &&
                          !timeline.marker("QIP GAUGE window-ready=/dev/screen/7/win-1", gauge) &&
                          !timeline.expire(gauge + gauge_limit - second) &&
                          reason(timeline.expire(gauge + gauge_limit)) == "gauge_window_timeout",
                  "gauge window bounds the wait");
            check(timeline.begin(model.state(), start) && !timeline.marker("QMC BEGIN", start) &&
                          !timeline.graphics(frames) &&
                          !timeline.marker("QIP GAUGE window-ready=/dev/screen/7/win-1", gauge) &&
                          !timeline.marker("QMC CHILD begin=animation-ready", ready) &&
                          !timeline.graphics(ready) &&
                          !timeline.expire(ready + StartupIgnition::animation_limit - second) &&
                          reason(timeline.expire(ready + StartupIgnition::animation_limit)) ==
                                  "animation_ready_timeout",
                  "animation-ready bounds the wait below the OEM animation timer");
        }

        // A cluster closed during the sequence restores ignition.
        check(ignition.begin(model.state(), start) &&
                      reason(ignition.finish()) == "cluster_closed" && !ignition.finish(),
              "closed cluster restores ignition");

        // A client value of ign wins over the sequence.
        check(ignition.begin(model.state(), start).has_value(), "sequence before client value");
        ignition.observe_patch(Json{{"speed", 40}});
        check(ignition.pending(), "unrelated client values keep the sequence");
        ignition.observe_patch(Json{{"ign", 0}});
        check(!ignition.pending() &&
                      !ignition.marker("QIP STARTUP ignition reason=gauge-check", start) &&
                      !ignition.finish(),
              "client ignition value ends the sequence");

        // A vehicle with ignition already off stays off and starts no sequence.
        apply(model, StartupIgnition::Change{{{"ign", 0}}, "client"});
        check(!ignition.begin(model.state(), start) && !ignition.pending() &&
                      !ignition.marker("QIP STARTUP ignition reason=gauge-check", start) &&
                      !ignition.expire(start + StartupIgnition::boot_limit),
              "ignition off is kept");
        std::filesystem::remove_all(directory);
        std::cout
                << "startup ignition: model sequence, request, guest timeline per engine, close, client PASS\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
