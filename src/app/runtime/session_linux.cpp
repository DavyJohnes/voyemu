#include "error.hpp"
#include "session_posix.hpp"

// The parts of Session that only Linux has; session_posix.cpp holds those it shares with macOS.
namespace voyemu {
using namespace session_detail;

// ANGLE renders through Vulkan on the system Vulkan driver, with or without a display server; its
// default platform on Linux would be desktop OpenGL on X11.
void Session::configure_graphics_environment(ProcessSpec &spec, const Json &) {
    spec.environment["ANGLE_DEFAULT_PLATFORM"] = "vulkan";
}

bool Session::android_host_canvas_ready(const std::function<void()> &) {
    return true;
}
} // namespace voyemu
