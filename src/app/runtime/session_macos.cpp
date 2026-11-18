#include "error.hpp"
#include "session_posix.hpp"

// The parts of Session that only macOS has; session_posix.cpp holds those it shares with Linux.
namespace voyemu {
using namespace session_detail;

void Session::configure_graphics_environment(ProcessSpec &, const Json &) {}

bool Session::android_host_canvas_ready(const std::function<void()> &pending) {
    if (android_spec_.value("env", Json::object()).contains("DYLD_INSERT_LIBRARIES")) {
        const auto canvas_path = directory_ / "media-window.json";

        if (!std::filesystem::exists(canvas_path)) {
            pending();

            return false;
        }

        if (!multimedia_canvas_ready(read_private_json(canvas_path, 4096))) {
            if (android_displays_ready_) {
                android_display_activated_ = false;
                android_canvas_refreshed_ = false;
            }

            android_displays_ready_ = false;
            deliveries_["android"].erase("displays");
            deliveries_["android"].erase("host_canvas");

            if (!android_canvas_refreshed_) {
                // SDK36 may retain the primary-only viewport after a late secondary pipe opens.
                const auto nudge =
                        adb({"emu", "multidisplay", "add", "1", "1920", "721", "160", "0"}, 2500);
                const auto restore =
                        adb({"emu", "multidisplay", "add", "1", "1920", "720", "160", "0"}, 2500);
                android_canvas_refreshed_ = true;

                if (nudge.exit_code || nudge.timed_out || restore.exit_code || restore.timed_out ||
                    nudge.output.find("OK") == std::string::npos ||
                    restore.output.find("OK") == std::string::npos) {
                    throw VoyemuError("ANDROID_CANVAS",
                                      "SDK viewport refresh did not restore the profile geometry");
                }

                logging::write(logging::Level::info, "android", "canvas.refresh_submitted",
                               {{"width", 3840}, {"height", 720}, {"transient_guest_height", 721}});
            }

            return false;
        }

        deliveries_["android"]["host_canvas"] = {{"status", "measured_native_view"},
                                                 {"size", Json::array({3840, 720})},
                                                 {"source", "own SDK Metal view backing geometry"}};
    }

    return true;
}
} // namespace voyemu
