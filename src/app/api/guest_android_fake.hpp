#pragma once
#include "error.hpp"
#include "guest_android.hpp"
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace voyemu::api {
// In-memory Android guest: power, boot identity, launchable packages, UI tree, frames, system
// state, synthetic phone and logs, without an emulator.
class FakeGuestAndroid : public GuestAndroid {
public:
    bool powered = true;
    std::uint64_t generation = 1;
    std::int64_t pid = 4242, observer_pid = 777;
    std::string boot_id = "00000000-0000-4000-8000-000000000001";
    std::set<std::string> packages{"com.android.settings"};
    // Primary display nodes; the secondary display reports an unsupported tree.
    Json nodes = Json::array({{{"id", "1"},
                               {"parent", nullptr},
                               {"class", "android.widget.TextView"},
                               {"text", "Settings"},
                               {"bounds", {0, 0, 100, 40}},
                               {"clipped_bounds", {0, 0, 100, 40}},
                               {"visible", true}}});
    // SDK frame dimensions; anything but 1920x720 is malformed guest data.
    unsigned frame_width = 1920, frame_height = 720;
    std::uint32_t frame_sequence = 1;
    Json system_state = {{"source", "system"}, {"android_pid", 4242}};
    Json phone_state = {{"connected", false}, {"call", "idle"}};
    std::string logs_source;
    // A non-empty failure is thrown by the next call, as the supervisor reports it.
    std::string failure;
    std::vector<std::string> calls;

    Json reboot(const Json &request, const std::string &mode, int) override {
        calls.push_back("reboot_" + mode);
        fail();

        if (!powered) {
            throw VoyemuError("ANDROID_REBOOT", "owned Android guest is powered off");
        }

        ++generation;
        boot_id.back() = boot_id.back() == '9' ? '0' : static_cast<char>(boot_id.back() + 1);

        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"mode", mode},
                {"android_generation", generation},
                {"boot_id", boot_id}};
    }

    Json power(const Json &request, bool enabled, int) override {
        calls.push_back(enabled ? "power_on" : "power_off");
        fail();
        const bool changed = powered != enabled;

        if (changed && enabled) {
            ++generation;
            ++pid;
        }

        powered = enabled;
        Json receipt{{"ok", true},
                     {"session_id", request.at("session").at("id")},
                     {"enabled", enabled},
                     {"process_alive", enabled},
                     {"verified", true},
                     {"qnx_unchanged", true},
                     {"source", "supervisor.android_lifecycle"},
                     {"visible_effect_verified", false},
                     {"changed", changed},
                     {"android_generation", generation},
                     {"android_pid", enabled ? Json(pid) : Json(nullptr)}};

        if (changed && !enabled) {
            receipt["shutdown"] = {{"wrapper_terminal", true}, {"ports_released", true}};
        }

        return receipt;
    }

    Json launch(const Json &request, const Json &app) override {
        calls.push_back("launch");
        fail();

        if (!powered) {
            throw VoyemuError("APP_UNAVAILABLE", "owned Android guest is not ready");
        }

        if (app.at("boot_id") != boot_id) {
            throw VoyemuError("APP_STALE", "guest boot differs before package resolution");
        }

        if (!packages.contains(app.at("package").get<std::string>())) {
            throw VoyemuError("APP_RESOLVE", "launcher activity resolution failed");
        }

        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"package", app.at("package")},
                {"screen", app.at("screen")},
                {"android_pid", pid}};
    }

    Json ui_observer(const Json &request, bool recover, int) override {
        calls.push_back(recover ? "ui_observer_recover" : "ui_observer");
        fail();
        ready("TREE_STALE: Android observation is unavailable");

        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"android_pid", pid},
                {"connection",
                 {{"boot_id", boot_id}, {"observer_pid", observer_pid}, {"api_level", 28}}}};
    }

    Json ui_exchange(const Json &, const Json &connection, const Json &message, int) override {
        calls.push_back("ui_" + message.at("op").get<std::string>());
        fail();
        ready("TREE_TRANSPORT_UNKNOWN: observer control unavailable");

        if (connection.at("boot_id") != boot_id) {
            return {{"schema", 1}, {"ok", false}, {"reason", "boot_changed"}};
        }

        if (message.at("op") == "hello") {
            return {{"ok", true},
                    {"boot_id", boot_id},
                    {"observer_pid", observer_pid},
                    {"parallel_surfaces", true},
                    {"observer_epoch", "fake-observer"},
                    {"ui_scopes", {"all_attached"}}};
        }

        const bool parallel = message.value("parallel_surfaces", false);
        Json surfaces = Json::array();

        for (const auto &name : message.at("surfaces")) {
            Json surface = name == "media_primary"
                                   ? Json{{"surface", name},          {"display_id", 0},
                                          {"status", "ok"},           {"complete", true},
                                          {"windows", Json::array()}, {"nodes", nodes}}
                                   : Json{{"surface", name},
                                          {"display_id", nullptr},
                                          {"status", "unsupported"}};

            if (parallel) {
                surface.update({{"scope", "all_attached"},
                                {"clock_domain", "android_uptime_ms"},
                                {"started_uptime_ms", 101},
                                {"completed_uptime_ms", 109}});
            }

            surfaces.push_back(std::move(surface));
        }

        Json observation{{"schema", 1},
                         {"ok", true},
                         {"observation_id", "fake-tree"},
                         {"observer_epoch", "fake-observer"},
                         {"boot_id", boot_id},
                         {"observer_pid", observer_pid},
                         {"started_uptime_ms", 100},
                         {"completed_uptime_ms", 110},
                         {"surfaces", surfaces}};

        if (parallel) {
            observation["parallel_surfaces"] = true;
        }

        return observation;
    }

    Json boot_identity(const Json &, int) override {
        calls.push_back("boot_identity");
        fail();
        ready("TEST_BOOT_UNAVAILABLE: owned Android identity is not ready");

        return {{"ok", true}, {"boot_id", boot_id}, {"android_pid", pid}};
    }

    media::Image frame(const std::filesystem::path &, const std::string &,
                       media::GuestDisplay display, int) override {
        calls.push_back("frame");
        fail();
        ready("MEDIA_RPC_UNKNOWN: SDK frame unavailable");
        media::Image image;
        image.display = display.id;
        image.width = frame_width;
        image.height = frame_height;
        image.sequence = frame_sequence++;
        image.storage.assign(std::size_t(frame_width) * frame_height * 4, 0x40);

        return image;
    }

    Json system(const Json &request, int) override {
        calls.push_back("system");
        fail();

        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"state", system_state}};
    }

    Json phone(const Json &request, const Json &command) override {
        const auto method = command.at("method").get<std::string>();

        calls.push_back("phone_" + method);
        fail();
        ready("PHONE_PROVIDER_UNAVAILABLE: owned Android guest is powered off");

        if (method == "connect") {
            phone_state["connected"] = true;
        }

        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"state", phone_state}};
    }

    Json logs(const Json &request, const Json &query, int) override {
        calls.push_back("logs");
        fail();
        ready("TEST_LOGS_UNAVAILABLE: owned Android guest required");

        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"logs",
                 {{"source", logs_source.empty() ? query.at("source") : Json(logs_source)},
                  {"entries", Json::array()}}}};
    }

private:
    void ready(const char *unavailable) const {
        if (!powered) {
            throw std::runtime_error(unavailable);
        }
    }

    void fail() {
        if (!failure.empty()) {
            throw std::runtime_error(std::exchange(failure, {}));
        }
    }
};
// Fake whose UI observer, boot identity and SDK frames follow per-test scripts; an unset script
// keeps the in-memory behavior.
class ScriptedGuestAndroid final : public FakeGuestAndroid {
public:
    std::function<Json(const Json &request, bool recover, int timeout_ms)> owner;
    std::function<Json(const Json &connection, const Json &message, int timeout_ms)> exchange;
    std::function<Json(int timeout_ms)> identity;
    std::function<media::Image(const std::string &session_id, unsigned display, int timeout_ms)>
            capture;

    Json ui_observer(const Json &request, bool recover, int timeout_ms) override {
        return owner ? owner(request, recover, timeout_ms)
                     : FakeGuestAndroid::ui_observer(request, recover, timeout_ms);
    }

    Json ui_exchange(const Json &request, const Json &connection, const Json &message,
                     int timeout_ms) override {
        return exchange ? exchange(connection, message, timeout_ms)
                        : FakeGuestAndroid::ui_exchange(request, connection, message, timeout_ms);
    }

    Json boot_identity(const Json &request, int timeout_ms) override {
        return identity ? identity(timeout_ms)
                        : FakeGuestAndroid::boot_identity(request, timeout_ms);
    }

    media::Image frame(const std::filesystem::path &session_directory,
                       const std::string &session_id, media::GuestDisplay display,
                       int timeout_ms) override {
        return capture ? capture(session_id, display.id, timeout_ms)
                       : FakeGuestAndroid::frame(session_directory, session_id, display,
                                                 timeout_ms);
    }
};
} // namespace voyemu::api
