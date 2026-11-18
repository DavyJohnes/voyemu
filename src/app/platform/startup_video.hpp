#pragma once
#include "startup_animation.hpp"
#include "window_image.hpp"
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace voyemu::window {
// One decoded frame of a startup video: its first position in the animation timeline.
struct StartupVideoFrame {
    std::size_t index{};
    Image image;
};
// Decoding threads of each of `videos` startup videos that play at once: the cores but one, left
// for the window, split evenly, one to three per video; three keep 30 fps at 3840x1440 while a
// frame decodes in up to 100 ms.
unsigned startup_video_workers(unsigned videos);
// Plays an OEM startup video that preparation stored as a boot animation archive: `workers`
// threads decode its PNG frames in parallel ahead of the window clock. At most workers + 2
// decoded or decoding frames exist at once, about 22 MB each at 3840x1440.
class StartupVideoPlayer {
public:
    StartupVideoPlayer(StartupAnimation animation, unsigned workers);
    ~StartupVideoPlayer();
    StartupVideoPlayer(const StartupVideoPlayer &) = delete;
    StartupVideoPlayer &operator=(const StartupVideoPlayer &) = delete;
    // The newest decoded frame due `seconds` after playback started; frames are handed in order,
    // and frames that became due late or finish after a newer frame was handed are dropped.
    std::optional<StartupVideoFrame> take(double seconds);
    // The video has played to its end, or decoding failed.
    bool finished(double seconds) const;
    // Why decoding stopped early; empty while it succeeds.
    std::string error() const;

    // Length of the whole timeline, held frames included.
    double seconds() const {
        return double(animation_.timeline.size()) / animation_.fps;
    }

private:
    void run();

    StartupAnimation animation_;
    // Timeline position of each distinct frame; a held frame repeats the previous entry.
    std::vector<std::size_t> frames_;
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    // Decoded frames by their number in frames_.
    std::map<std::size_t, Image> ready_;
    std::size_t next_{}, due_{}, busy_{};
    std::optional<std::size_t> taken_;
    bool stop_{};
    std::string error_;
    std::vector<std::thread> workers_;
};
// When the startup videos play: from each entry into the multimedia startup phase until it ends;
// the OEM cluster service starts its players at power-on and stops them when Android reports
// readiness.
class StartupVideoTrigger {
public:
    // Seconds since the entry into the startup phase at `now`, or none outside it.
    std::optional<double> update(bool starting, double now);

private:
    std::optional<double> started_;
};
} // namespace voyemu::window
