#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace voyemu::media {
// Caller holds the presentation mutex. Frames received during boot are never presented. One frame
// per Android screen; the first `required` ones, the multimedia screens, complete the presentation
// and the others never delay it.
template <class Frame> class PresentationFrames {
public:
    explicit PresentationFrames(std::size_t count = 2, std::size_t required = 2)
        : frames_(count), required_(std::min(required, count)) {}

    bool set_ready(bool value) {
        if (ready_ == value) {
            return false;
        }

        ready_ = value;
        frames_.assign(frames_.size(), Frame{});

        return true;
    }

    void put(unsigned display, Frame frame) {
        if (ready_) {
            frames_.at(display) = std::move(frame);
        }
    }

    bool complete() const {
        return ready_ &&
               std::all_of(frames_.begin(), frames_.begin() + required_, [](const Frame &frame) {
                   return bool(frame);
               });
    }

    std::vector<Frame> snapshot() const {
        return complete() ? frames_ : std::vector<Frame>(frames_.size());
    }

private:
    bool ready_{};
    std::vector<Frame> frames_;
    std::size_t required_;
};

// Each stream attempt owns a cancellation token. Resume never clears a cancelled token.
class CaptureDemand {
public:
    using Cancellation = std::shared_ptr<std::atomic<bool>>;

    Cancellation wait() {
        std::unique_lock lock(mutex_);

        wake_.wait(lock, [&] {
            return stopped_ || wanted_;
        });

        if (stopped_) {
            return {};
        }

        std::erase_if(active_, [](const auto &token) {
            return token.expired();
        });
        auto token = std::make_shared<std::atomic<bool>>(false);
        active_.push_back(token);

        return token;
    }

    bool set_wanted(bool value) {
        std::lock_guard lock(mutex_);

        if (stopped_ || wanted_ == value) {
            return false;
        }

        wanted_ = value;

        if (!wanted_) {
            cancel();
        }

        wake_.notify_all();

        return true;
    }

    void refresh() {
        std::lock_guard lock(mutex_);

        if (stopped_) {
            return;
        }

        cancel();
        wake_.notify_all();
    }

    void stop() {
        std::lock_guard lock(mutex_);

        stopped_ = true;
        cancel();
        wake_.notify_all();
    }

private:
    void cancel() {
        for (const auto &weak : active_) {
            if (auto token = weak.lock()) {
                token->store(true);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    bool wanted_{true}, stopped_{};
    std::vector<std::weak_ptr<std::atomic<bool>>> active_;
};

// Caller holds the presentation mutex: invalidate old callbacks before admitting fresh frames.
template <class Frame>
bool set_presentation_ready(PresentationFrames<Frame> &frames, CaptureDemand &demand, bool ready) {
    const bool changed = frames.set_ready(ready);

    if (changed) {
        demand.refresh();
    }

    return changed;
}
} // namespace voyemu::media
