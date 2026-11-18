#include "event_journal.hpp"
#include <charconv>
#include <limits>
#include <stdexcept>
namespace voyemu::runtime {
EventJournal::EventJournal(std::size_t capacity) : capacity_(capacity) {
    if (!capacity || capacity > 4096) {
        throw std::invalid_argument("journal capacity must be 1..4096");
    }
}

void EventJournal::baseline(ConditionObservation observation) {
    std::lock_guard lock(mutex_);

    if (closed_) {
        throw std::logic_error("journal closed");
    }

    if (armed_) {
        if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("journal sequence exhausted");
        }

        reset_at_ = ++sequence_;
        entries_.clear();
    }

    latest_ = std::move(observation);
    armed_ = true;
    changed_.notify_all();
}

std::string EventJournal::publish(ConditionObservation observation, Json event) {
    std::lock_guard lock(mutex_);

    if (!armed_ || closed_) {
        throw std::logic_error("journal not armed or closed");
    }

    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("journal sequence exhausted");
    }

    if (observation.monotonic_ms < latest_.monotonic_ms) {
        throw std::invalid_argument("journal clock moved backwards");
    }

    if (observation.epoch != latest_.epoch) {
        observation.gap = true;
        reset_at_ = sequence_ + 1;
    }

    latest_ = observation;
    auto cursor = std::to_string(++sequence_);
    entries_.push_back({cursor, std::move(observation), std::move(event)});

    if (entries_.size() > capacity_) {
        entries_.pop_front();
    }

    changed_.notify_all();

    return cursor;
}

JournalReady EventJournal::ready() const {
    std::lock_guard lock(mutex_);

    if (!armed_ || closed_) {
        throw std::logic_error("journal not armed or closed");
    }

    return {latest_, std::to_string(sequence_)};
}

JournalRead EventJournal::replay(std::uint64_t cursor) const {
    JournalRead r;

    r.cursor = std::to_string(sequence_);
    r.reset = cursor < reset_at_;
    r.gap = r.reset || closed_;

    if (!entries_.empty()) {
        auto oldest = std::stoull(entries_.front().cursor);
        r.gap |= cursor < oldest - 1;
    }

    for (const auto &entry : entries_) {
        if (std::stoull(entry.cursor) > cursor) {
            r.events.push_back(entry);
            r.gap |= entry.observation.gap;
        }
    }

    return r;
}

JournalRead EventJournal::read(const std::string &text, std::chrono::milliseconds wait) {
    std::uint64_t cursor = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), cursor);

    if (text.empty() || (text.size() > 1 && text[0] == '0') || parsed.ec != std::errc() ||
        parsed.ptr != text.data() + text.size() || wait.count() < 0 ||
        wait > std::chrono::seconds(60)) {
        throw std::invalid_argument("invalid journal cursor or wait");
    }

    std::unique_lock lock(mutex_);

    if (!armed_ || cursor > sequence_) {
        throw std::invalid_argument("journal cursor outside subscription");
    }

    changed_.wait_for(lock, wait, [&] {
        return sequence_ > cursor || closed_;
    });

    return replay(cursor);
}

void EventJournal::close() {
    std::lock_guard lock(mutex_);

    closed_ = true;
    changed_.notify_all();
}
} // namespace voyemu::runtime
