#include "startup_video.hpp"
#include "error.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace voyemu::window {
unsigned startup_video_workers(unsigned videos) {
    const unsigned cores = std::thread::hardware_concurrency();

    return std::clamp((cores > 1 ? cores - 1 : 1U) / std::max(1U, videos), 1U, 3U);
}

StartupVideoPlayer::StartupVideoPlayer(StartupAnimation animation, unsigned workers)
    : animation_(std::move(animation)), capacity_(std::size_t(std::max(1U, workers)) + 2) {
    const auto &timeline = animation_.timeline;

    for (std::size_t index = 0; index < timeline.size(); ++index) {
        if (index == 0 || timeline[index] != timeline[index - 1]) {
            frames_.push_back(index);
        }
    }

    for (unsigned i = 0; i < std::max(1U, workers); ++i) {
        workers_.emplace_back([this] {
            run();
        });
    }
}

StartupVideoPlayer::~StartupVideoPlayer() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }

    wake_.notify_all();

    for (auto &worker : workers_) {
        worker.join();
    }
}

void StartupVideoPlayer::run() {
    std::unique_lock lock(mutex_);

    while (true) {
        wake_.wait(lock, [this] {
            return stop_ || !error_.empty() || next_ >= frames_.size() ||
                   busy_ + ready_.size() < capacity_;
        });

        if (stop_ || !error_.empty() || next_ >= frames_.size()) {
            return;
        }

        // A frame whose successor is already due would only be dropped.
        while (next_ + 1 < frames_.size() && frames_[next_ + 1] <= due_) {
            ++next_;
        }

        const auto frame = next_++;
        ++busy_;
        lock.unlock();

        try {
            auto image = decode_png_bytes(
                    read_startup_frame(animation_, animation_.timeline[frames_[frame]]));

            if (unsigned(image.width) != animation_.width ||
                unsigned(image.height) != animation_.height) {
                throw VoyemuError("WINDOW_STARTUP", "frame differs from desc.txt size");
            }

            lock.lock();

            if (!taken_ || frame > *taken_) {
                ready_.emplace(frame, std::move(image));
            }
        } catch (const std::exception &error) {
            lock.lock();
            error_ = error.what();
        }

        --busy_;
        wake_.notify_all();
    }
}

std::optional<StartupVideoFrame> StartupVideoPlayer::take(double seconds) {
    const double due = std::isfinite(seconds) && seconds > 0 ? seconds * animation_.fps : 0;
    std::optional<StartupVideoFrame> result;
    {
        std::lock_guard lock(mutex_);
        due_ = std::max(due_, std::size_t(due));

        while (!ready_.empty() && double(frames_[ready_.begin()->first]) <= due) {
            taken_ = ready_.begin()->first;
            result = StartupVideoFrame{frames_[*taken_], std::move(ready_.begin()->second)};
            ready_.erase(ready_.begin());
        }
    }

    wake_.notify_all();

    return result;
}

bool StartupVideoPlayer::finished(double seconds) const {
    std::lock_guard lock(mutex_);

    return !error_.empty() ||
           (next_ >= frames_.size() && busy_ == 0 && ready_.empty() && seconds >= this->seconds());
}

std::string StartupVideoPlayer::error() const {
    std::lock_guard lock(mutex_);

    return error_;
}

std::optional<double> StartupVideoTrigger::update(bool starting, double now) {
    if (!starting) {
        started_.reset();
    } else if (!started_) {
        started_ = now;
    }

    return started_ ? std::optional<double>(now - *started_) : std::nullopt;
}
} // namespace voyemu::window
