#pragma once
#include "guest_qnx.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace voyemu::api {
// In-memory QNX cluster: power state, generation, Gauge scene and exported frame, without QEMU or
// OEM images.
class FakeGuestQnx final : public GuestQnx {
public:
    bool supported = true, running = true;
    std::uint64_t generation = 1, heartbeat = 0;
    // Gauge Node2D scene; an empty scene is malformed guest data.
    Json nodes = Json::array({{{"id", 1u}, {"parent", nullptr}, {"name", "Gauge"}}});
    // Latest exported frame; nullopt until the cluster exports one.
    std::optional<frames::Frame> exported;
    // A non-empty failure is thrown by the next call, as the supervisor reports it.
    std::string failure;
    std::vector<std::string> calls;

    bool lifecycle_supported(const Json &) override {
        calls.push_back("lifecycle_supported");

        return supported;
    }

    Json power(const Json &request, bool enabled) override {
        calls.push_back(enabled ? "power_on" : "power_off");
        fail();

        if (enabled && !running) {
            ++generation;
        }

        running = enabled;

        return receipt(request);
    }

    Json restart(const Json &request) override {
        calls.push_back("restart");
        fail();
        running = true;
        ++generation;

        return receipt(request);
    }

    Json ui(const Json &, int) override {
        calls.push_back("ui");
        fail();

        if (!running) {
            return {{"generation", generation}, {"status", "off"}};
        }

        return {{"generation", generation},
                {"status", "sample"},
                {"owner", 7u},
                {"sequence", 2u},
                {"heartbeat_ns", ++heartbeat},
                {"guest_monotonic_ns", heartbeat},
                {"presented_ns", heartbeat},
                {"window", {{"width", 1920u}, {"height", 720u}}},
                {"complete", true},
                {"nodes", nodes}};
    }

    Json system(const Json &, int) override {
        calls.push_back("system");
        fail();

        if (!running) {
            return {{"generation", generation},
                    {"status", "unavailable"},
                    {"reason", "cluster_off"}};
        }

        return {{"generation", generation},
                {"status", "sample"},
                {"owner", 7u},
                {"sequence", 2u},
                {"guest_pid", 4097u},
                {"producer_incarnation", 1u},
                {"guest_monotonic_ns", ++heartbeat},
                {"heartbeat_ns", heartbeat},
                {"node_count", nodes.size()},
                {"complete", true}};
    }

    std::optional<frames::Frame> frame(const std::filesystem::path &, std::uint64_t after_sequence,
                                       int) override {
        calls.push_back("frame");
        fail();

        if (!running || !exported || exported->sequence <= after_sequence) {
            return std::nullopt;
        }

        return exported;
    }

private:
    void fail() {
        if (!failure.empty()) {
            throw std::runtime_error(std::exchange(failure, {}));
        }
    }

    Json receipt(const Json &request) const {
        return {{"ok", true},
                {"session_id", request.at("session").at("id")},
                {"deliveries",
                 {{"qnx",
                   {{"status", running ? "running" : "off"},
                    {"generation", generation},
                    {"lifecycle_supported", supported}}}}}};
    }
};
} // namespace voyemu::api
