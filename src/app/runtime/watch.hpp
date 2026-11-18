#pragma once
#include "conditions.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
namespace voyemu::runtime {
struct WatchReady {
    std::string status, watch_id, epoch, cursor;
    ConditionResult result;
};
struct WatchEvent {
    std::string cursor;
    std::shared_ptr<const ConditionObservation> observation;
};
struct WatchRead {
    std::string status, epoch, cursor;
    bool gap = false;
    ConditionResult result;
    std::vector<WatchEvent> events;
};
// Supervisor lifetime; publish is called by its source coordinator. No SDK calls.
// Hub epoch must accompany decimal cursors across transport boundaries.
class WatchHub {
public:
    explicit WatchHub(std::size_t capacity = 4096);
    WatchReady start(Json condition, std::int64_t lease_ms, std::int64_t now_ms);
    void publish(ConditionObservation observation);
    WatchRead read(const std::string &id, const std::string &cursor,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds{0},
                   std::size_t limit = 256, std::int64_t now_ms = 0);
    void stop(const std::string &id);
    void tick(std::int64_t now_ms);
    void close();

    const std::string &epoch() const {
        return epoch_;
    }

    std::size_t active(std::int64_t now_ms);

private:
    struct Entry {
        std::uint64_t sequence;
        std::size_t bytes;
        std::shared_ptr<const ConditionObservation> observation;
    };
    struct Watch {
        Json condition;
        std::shared_ptr<const ConditionObservation> baseline;
        std::uint64_t sequence;
        std::int64_t expires;
        std::size_t bytes;
    };
    const std::size_t capacity_;
    const std::string epoch_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Entry> entries_;
    std::map<std::string, Watch> watches_;
    std::shared_ptr<const ConditionObservation> latest_;
    std::size_t latest_bytes_ = 0, replay_bytes_ = 0, baseline_bytes_ = 0;
    std::uint64_t sequence_ = 0, next_id_ = 0;
    bool closed_ = false;
};
} // namespace voyemu::runtime
