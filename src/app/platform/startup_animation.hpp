#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace voyemu::window {
// One OEM Android boot animation (bootanimation.zip), or an OEM startup video that preparation
// stored in the same format: stored PNG frames in part directories, timed by desc.txt. Only the
// index is read up front; frames are read one at a time.
struct StartupAnimation {
    struct Frame {
        std::uint64_t offset{}, bytes{};
    };
    std::filesystem::path file;
    // Frame size and rate from desc.txt; the window scales frames to the screen.
    unsigned width{}, height{}, fps{};
    std::vector<std::vector<Frame>> parts;
    // Frames in display order: each part `count` times (at least once), with `pause` frames of its
    // last frame after each repeat; the final entry holds until Android is ready.
    std::vector<std::pair<std::size_t, std::size_t>> timeline;
};
StartupAnimation read_startup_animation(const std::filesystem::path &file);
// Part and frame shown `seconds` after the window started.
std::pair<std::size_t, std::size_t> startup_frame(const StartupAnimation &animation,
                                                  double seconds);
// The stored PNG bytes of one frame.
std::string read_startup_frame(const StartupAnimation &animation,
                               std::pair<std::size_t, std::size_t> frame);
} // namespace voyemu::window
