#pragma once
#include "admission.hpp"
#include "api.hpp"
#include "can_model_observation.hpp"
#include "can_observation.hpp"
#include "can_restart_boundary.hpp"
#include "capture_age.hpp"
#include "capture_geometry.hpp"
#include "capture_png.hpp"
#include "capture_roi.hpp"
#include "check.hpp"
#include "conditions.hpp"
#include "frame_transport.hpp"
#include "full_restart.hpp"
#include "input_fence.hpp"
#include "logging.hpp"
#include "media_readiness.hpp"
#include "media_rpc.hpp"
#include "os/platform.hpp"
#include "phone_control.hpp"
#include "platform_file.hpp"
#include "pointer_drag.hpp"
#include "profile_catalog.hpp"
#include "profile_identity.hpp"
#include "profile_runtime.hpp"
#include "qinggan11_startup.hpp"
#include "qnx_cluster_ui_wire.hpp"
#include "qnx_runtime_layout.hpp"
#include "session.hpp"
#include "session_recovery.hpp"
#include "session_ui_response.hpp"
#include "system_observation.hpp"
#include "tap_repeat.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <future>
#include <latch>
#include <map>
#include <mutex>
#include <set>
#include <thread>

// Helpers shared by the parts of Api; each part includes this header only.
namespace voyemu::api::detail {
using Clock = std::chrono::steady_clock;
using Lock = FileLock;
std::uint64_t monotonic_us();
bool token(std::string_view value);

struct MachineIdentityError : VoyemuError {
    Json details = Json::object();

    MachineIdentityError(const Json &result, const Json &created)
        : VoyemuError("MACHINE_IDENTITY_UNKNOWN", "launched selection differs") {
        if (result.contains("session_id") && result.at("session_id").is_string() &&
            token(result.at("session_id").get<std::string>())) {
            details["session"] = {{"id", result.at("session_id")},
                                  {"ownership_id", created.at("ownership_id")}};
        }
    }
};

inline constexpr std::array<std::string_view, 3> observed_screens{"primary", "secondary", "rear"};

// The reply of a domain handler of Api::dispatch for an operation of another domain.
inline Json unhandled_operation() {
    return Json(Json::value_t::discarded);
}

std::string text(const Json &object, const char *key, std::size_t maximum = 128);
std::filesystem::path session_active_directory(const std::filesystem::path &root,
                                               const std::string &profile,
                                               const MachineIdentity &machine);
std::uint64_t number(const Json &object, const char *key, std::uint64_t fallback,
                     std::uint64_t minimum, std::uint64_t maximum);
bool boolean(const Json &object, const char *key, bool fallback);
std::string profile(const Json &request);
void validate_sources(const Json &request);
std::string terminal_outcome(const Json &response);
Json error(const std::exception &exception, bool contacted = false);
std::string admission_scope(const Json &request);
#ifndef _WIN32
bool owned_wheel_lease_held(const std::filesystem::path &path);
bool matching_wheel_writer(const std::filesystem::path &root,
                           const std::filesystem::path &directory, const Json &request);
#endif
Json operations();
Json requested_screens(const Json &request, bool android_only = false);
std::size_t observed_slot(std::string_view surface);
std::vector<Screen> status_screens(const Json &status);
media::GuestDisplay android_screen(const std::vector<Screen> &screens, std::string_view role);
void validate_ui_filter(const Json &request);
PixelRect requested_rect(const Json &request, std::size_t width = 1920, std::size_t height = 720);
Json rect_json(PixelRect rect);
Json normalize_request(Json request, bool suite_case = false);
bool writer(std::string_view op);
bool recorded(std::string_view op);
Json public_snapshot(Json snapshot);
std::filesystem::path artifacts(const std::filesystem::path &root, const Json &request);
void new_json(const std::filesystem::path &path, const Json &value);
Json observation(const Json &status, std::string_view predicate, const Json &request);
Json read_logs(const std::filesystem::path &path, const Json &request);
} // namespace voyemu::api::detail
