#pragma once
#include "watch.hpp"
#include <atomic>
#include <functional>
#include <thread>
namespace voyemu::runtime {
struct SourceSample {
    std::string status = "unavailable", instance, boot_id, revision;
    Json value = Json::object();
    std::vector<Json> events;
    bool continuous = false, gap = false;
    // Host steady watermark proven by the journal adapter; zero means unknown.
    std::int64_t covered_through_ms = 0;
};
struct SourceContext {
    std::chrono::steady_clock::time_point deadline;
    const std::atomic<bool> &cancelled;
};
struct SourceDefinition {
    std::string id;
    std::function<SourceSample(const SourceContext &)> collect;
    std::chrono::milliseconds interval{100}, budget{1000};
};
// Collectors MUST honor cancellation/deadline. No detached threads or Session locks.
// Construct after Hub; stop before destroying Hub. Register before start.
class SourceCoordinator {
public:
    explicit SourceCoordinator(WatchHub &hub);
    ~SourceCoordinator();
    void add(SourceDefinition source);
    void publish_to(WatchHub &hub, std::vector<std::string> sources);
    void remove_projection(WatchHub &hub);
    void start();
    Json snapshot(std::chrono::milliseconds timeout, std::vector<std::string> sources = {});
    std::vector<std::string> select(const Json &sources) const;
    bool demanded(const std::string &source);
    void stop();
    static std::int64_t now_ms();

private:
    struct Slot {
        SourceDefinition definition;
        SourceSample sample;
        std::uint64_t generation = 0, requested = 0;
        bool initialized = false;
        std::int64_t demand_until = 0;
        std::int64_t began = 0, ended = 0;
        std::thread worker;
    };
    void pump(Slot &slot);
    void publish_locked(const std::string &source, const SourceSample &fresh, bool boundary);
    struct Destination {
        WatchHub *hub;
        std::vector<std::string> sources;
        std::uint64_t epoch = 1;
    };
    std::vector<Destination> destinations_;
    Json state_locked() const;
    WatchHub &hub_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::atomic<bool> cancelled_{false};
    bool started_ = false, stopped_ = false;
    std::uint64_t epoch_ = 1;
};
} // namespace voyemu::runtime
