#include "qinggan09_canbus_startup.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
void check(bool value, const char *why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

Json ready(const std::string &boot) {
    return {{"schema", 1},
            {"source", "android:qg.canbus"},
            {"boot_id", boot},
            {"status", "ready"},
            {"pid", 51},
            {"source_instance", "instance"},
            {"transport", {{"registration", "registered"}, {"binder_alive", true}}}};
}
} // namespace

int main() {
    try {
        check(qinggan09_canbus_service_component(Json::object()) ==
                      "com.qinggan.canbus.service/.RegistrationService",
              "legacy service entrypoint preserved");
        const auto component = qinggan09_canbus_service_component(
                Json{{"can_model_observer_sha256", std::string(64, 'a')}});
        const auto manifest =
                std::filesystem::path(__FILE__)
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .parent_path() /
                "profiles/free-09-chinese-20260106-6.0.3/components/canbus/AndroidManifest.xml";
        std::ifstream file(manifest);
        check(bool(file), "current model APK manifest required");
        const std::string bytes{std::istreambuf_iterator<char>(file), {}};
        const auto full = component.substr(0, component.find('/')) +
                          component.substr(component.find('/') + 1);
        check(bytes.find("<service android:name=\"" + full + "\"") != std::string::npos,
              "model startup and state dispatch target a declared service");
        Qinggan09CanBusStartup state;
        int installs = 0, starts = 0, polls = 0;
        auto run = [&](const std::string &boot, std::uint64_t generation) {
            return qinggan09_canbus_startup(
                    state, boot, generation,
                    [&] {
                        ++installs;
                    },
                    [&] {
                        ++starts;
                    },
                    [&] {
                        ++polls;

                        return ready(boot);
                    });
        };
        // No audio-completed or snapshot input can bypass this independent barrier.
        check(!run("boot-a", 1).is_null() && installs == 1 && starts == 1 && polls == 1,
              "fresh/restored delivery barrier");
        check(run("boot-a", 1).is_null() && installs == 1 && starts == 1, "same generation cached");
        run("boot-a", 2);
        check(installs == 2 && starts == 2 && polls == 2, "new generation service barrier");
        run("boot-b", 3);
        check(installs == 3 && starts == 3, "new verified boot delivery");
        Qinggan09CanBusStartup uncertain;
        int submitted = 0;

        for (int generation = 1; generation <= 2; ++generation) {
            bool rejected = false;
            try {
                qinggan09_canbus_startup(
                        uncertain, "boot-a", generation,
                        [&] {
                            ++submitted;
                            throw std::runtime_error("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN: pm");
                        },
                        [] {
                            throw std::runtime_error("must not start");
                        },
                        [] {
                            return ready("boot-a");
                        });
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected && submitted == 1 && uncertain.unknown,
                  "same boot quarantine survives generation");
        }

        qinggan09_canbus_startup(
                uncertain, "boot-b", 3,
                [&] {
                    ++submitted;
                },
                [] {},
                [] {
                    return ready("boot-b");
                });
        check(submitted == 2 && !uncertain.unknown, "verified new boot clears quarantine");
        Qinggan09CanBusStartup pending;
        int service_dispatch = 0;
        bool failed = false;
        try {
            qinggan09_canbus_startup(
                    pending, "b", 1, [] {},
                    [&] {
                        ++service_dispatch;
                    },
                    [] {
                        return ready("stale");
                    });
        } catch (const std::exception &) {
            failed = true;
        }

        check(failed && !pending.completed, "stale readiness rejected");
        qinggan09_canbus_startup(
                pending, "b", 1,
                [] {
                    throw std::runtime_error("must not reinstall");
                },
                [&] {
                    ++service_dispatch;
                },
                [] {
                    return ready("b");
                });
        check(service_dispatch == 1, "read-only readiness retry must not dispatch service again");

        for (const auto &key :
             {"schema", "source", "boot_id", "status", "pid", "source_instance", "transport"}) {
            auto invalid = ready("b");
            invalid.erase(key);
            check(!qinggan09_canbus_ready(invalid, "b"), "missing readiness evidence");
        }

        for (const auto &key : {"registration", "binder_alive"}) {
            auto invalid = ready("b");
            invalid["transport"].erase(key);
            check(!qinggan09_canbus_ready(invalid, "b"), "missing transport evidence");
        }

        for (const auto &key :
             {"schema", "source", "boot_id", "status", "pid", "source_instance", "transport"}) {
            auto invalid = ready("b");
            invalid[key] = nullptr;
            check(!qinggan09_canbus_ready(invalid, "b"), "null readiness evidence");
        }

        auto obsolete = ready("b");
        obsolete["schema"] = 2;
        check(!qinggan09_canbus_ready(obsolete, "b"), "obsolete schema rejected");
        auto fractional = ready("b");
        fractional["schema"] = 1.0;
        check(!qinggan09_canbus_ready(fractional, "b"), "integer schema required");
        auto dead = ready("b");
        dead["pid"] = 0;
        check(!qinggan09_canbus_ready(dead, "b"), "positive pid required");
        dead = ready("b");
        dead["transport"]["binder_alive"] = false;
        check(!qinggan09_canbus_ready(dead, "b"), "live binder required");
        const std::string navigation_dump =
                "ServiceRecord{abc u0 com.qinggan.QGBus/com.voyemu.oem.NavigationBusService}\n    app=ProcessRecord{def 235:com.qinggan.QGBus/1000}\n";
        auto navigation = qinggan09_navigation_bus_process(navigation_dump, "n");
        check(qinggan09_navigation_bus_ready(navigation, "n") && navigation.at("pid") == 235,
              "exact NavigationBus service process evidence");

        for (const auto &malformed :
             {std::string{},
              std::string(
                      "ServiceRecord{abc u0 com.qinggan.QGBus/com.voyemu.oem.NavigationBusService}\n app=null"),
              std::string(
                      "ServiceRecord{abc u0 foreign/Service}\n app=ProcessRecord{def 235:com.qinggan.QGBus/1000}")}) {
            check(qinggan09_navigation_bus_process(malformed, "n").is_null(),
                  "absent or foreign navigation process rejected");
        }

        check(!qinggan09_navigation_bus_ready(navigation, "stale"), "navigation boot fence");

        for (const auto *field : {"schema", "source", "boot_id", "service", "status", "pid"}) {
            auto invalid = navigation;
            invalid[field] = nullptr;
            check(!qinggan09_navigation_bus_ready(invalid, "n"), "invalid navigation evidence");
        }

        Qinggan09CanBusStartup navigation_state;
        int navigation_installs = 0, navigation_starts = 0;
        auto launch_navigation = [&](std::uint64_t generation) {
            return qinggan09_canbus_startup(
                    navigation_state, "n", generation,
                    [&] {
                        ++navigation_installs;
                    },
                    [&] {
                        ++navigation_starts;
                    },
                    [&] {
                        return navigation;
                    },
                    qinggan09_navigation_bus_ready, "ANDROID_NAVIGATION_BUS");
        };
        check(!launch_navigation(1).is_null() && launch_navigation(1).is_null() &&
                      navigation_installs == 1 && navigation_starts == 1,
              "navigation startup reuse and cache");
        launch_navigation(2);
        check(navigation_installs == 2 && navigation_starts == 2,
              "navigation generation independently rearms");
        Qinggan09CanBusStartup navigation_unknown;
        int navigation_submitted = 0;

        for (int generation = 1; generation <= 2; ++generation) {
            bool denied = false;
            try {
                qinggan09_canbus_startup(
                        navigation_unknown, "n", generation,
                        [&] {
                            ++navigation_submitted;
                            throw std::runtime_error("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN: pm");
                        },
                        [] {
                            throw std::runtime_error("must not dispatch");
                        },
                        [&] {
                            return navigation;
                        },
                        qinggan09_navigation_bus_ready, "ANDROID_NAVIGATION_BUS");
            } catch (const std::exception &) {
                denied = true;
            }

            check(denied && navigation_submitted == 1,
                  "unknown navigation replacement quarantines same boot");
        }

        std::cout << "a9 CanBus startup tests passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
