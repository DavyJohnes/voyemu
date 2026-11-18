#pragma once
#include "conditions.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
namespace voyemu::runtime {
struct JournalEntry {
    std::string cursor;
    ConditionObservation observation;
    Json event;
};
struct JournalRead {
    std::string cursor;
    bool gap = false, reset = false;
    std::vector<JournalEntry> events;
};
struct JournalReady {
    ConditionObservation baseline;
    std::string cursor;
};
// One journal per source subscription. Source pump publishes full state atomically
// with each event. ready() arms a baseline/cursor under the publication lock.
class EventJournal {
public:
    explicit EventJournal(std::size_t capacity = 1024);
    void baseline(ConditionObservation observation);
    std::string publish(ConditionObservation observation, Json event);
    JournalReady ready() const;
    JournalRead read(const std::string &cursor,
                     std::chrono::milliseconds wait = std::chrono::milliseconds{0});
    void close();

private:
    JournalRead replay(std::uint64_t cursor) const;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<JournalEntry> entries_;
    ConditionObservation latest_;
    std::uint64_t sequence_ = 0, reset_at_ = 0;
    bool armed_ = false, closed_ = false;
};
} // namespace voyemu::runtime
