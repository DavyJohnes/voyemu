#pragma once
#include "model.hpp"
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
namespace voyemu {
// New model-backed APK declares CanBusService; legacy payloads retain their original entrypoint.
inline std::string qinggan09_canbus_service_component(const Json &specification) {
    return specification.contains("can_model_observer_sha256")
                   ? "com.qinggan.canbus.service/.CanBusService"
                   : "com.qinggan.canbus.service/.RegistrationService";
}

struct Qinggan09CanBusStartup {
    std::string boot;
    std::uint64_t generation = 0;
    bool unknown = false, installed = false, started = false, completed = false;
};

inline bool qinggan09_canbus_ready(const Json &reply, const std::string &boot) {
    auto text = [&](const Json &value, const char *key, const std::string &expected) {
        return value.is_object() && value.contains(key) && value.at(key).is_string() &&
               value.at(key).get<std::string>() == expected;
    };

    return reply.is_object() && reply.contains("schema") &&
           reply.at("schema").is_number_integer() && reply.at("schema") == 1 &&
           text(reply, "source", "android:qg.canbus") && text(reply, "boot_id", boot) &&
           text(reply, "status", "ready") && reply.contains("pid") &&
           reply.at("pid").is_number_integer() && reply.at("pid").get<std::int64_t>() > 0 &&
           reply.contains("source_instance") && reply.at("source_instance").is_string() &&
           !reply.at("source_instance").get<std::string>().empty() && reply.contains("transport") &&
           text(reply.at("transport"), "registration", "registered") &&
           reply.at("transport").contains("binder_alive") &&
           reply.at("transport").at("binder_alive").is_boolean() &&
           reply.at("transport").at("binder_alive") == true;
}

inline Json qinggan09_navigation_bus_process(const std::string &output, const std::string &boot) {
    std::smatch process;

    if (output.size() > 65536 || output.find("ServiceRecord{") == std::string::npos ||
        output.find("com.qinggan.QGBus/com.voyemu.oem.NavigationBusService") == std::string::npos ||
        !std::regex_search(
                output, process,
                std::regex(
                        "app=ProcessRecord\\{[^\r\n}]* ([1-9][0-9]{0,8}):com\\.qinggan\\.QGBus/1000\\}"))) {
        return nullptr;
    }

    return {{"schema", 1},
            {"source", "android:com.qinggan.QGBus"},
            {"boot_id", boot},
            {"service", "com.qinggan.QGBus/com.voyemu.oem.NavigationBusService"},
            {"status", "service_process_running"},
            {"pid", std::stoll(process[1].str())}};
}

inline bool qinggan09_navigation_bus_ready(const Json &reply, const std::string &boot) {
    auto text = [&](const char *key, const std::string &expected) {
        return reply.contains(key) && reply.at(key).is_string() && reply.at(key) == expected;
    };

    return reply.is_object() && reply.contains("schema") &&
           reply.at("schema").is_number_integer() && reply.at("schema") == 1 &&
           text("source", "android:com.qinggan.QGBus") && text("boot_id", boot) &&
           text("service", "com.qinggan.QGBus/com.voyemu.oem.NavigationBusService") &&
           text("status", "service_process_running") && reply.contains("pid") &&
           reply.at("pid").is_number_integer() && reply.at("pid").get<std::int64_t>() > 0;
}

template <class Install, class Start, class Observe,
          class Ready = decltype(&qinggan09_canbus_ready)>
inline Json qinggan09_canbus_startup(Qinggan09CanBusStartup &state, const std::string &boot,
                                     std::uint64_t generation, Install install, Start start,
                                     Observe observe, Ready ready = qinggan09_canbus_ready,
                                     const std::string &role = "ANDROID_CANBUS") {
    if (state.boot != boot) {
        state = {boot, generation};
    }

    if (state.unknown) {
        throw std::runtime_error(
                role + "_DELIVERY_UNKNOWN: replacement outcome requires a new verified boot");
    }

    if (state.generation != generation) {
        state.generation = generation;
        state.installed = false;
        state.started = false;
        state.completed = false;
    }

    if (state.completed) {
        return nullptr;
    }

    if (!state.installed) {
        try {
            install();
            state.installed = true;
        } catch (const std::exception &error) {
            if (std::string_view(error.what())
                        .starts_with("ANDROID_OWNED_PACKAGE_OUTCOME_UNKNOWN:")) {
                state.unknown = true;
            }

            throw;
        }
    }

    if (!state.started) {
        state.started = true;
        start();
    }

    auto reply = observe();

    if (!ready(reply, boot)) {
        throw std::runtime_error(role + (role == "ANDROID_CANBUS"
                                                 ? "_STARTUP: current registered service required"
                                                 : "_STARTUP: current service readiness required"));
    }

    state.completed = true;

    return reply;
}
} // namespace voyemu
