#include "watch.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <stdexcept>
namespace voyemu::runtime {
namespace {
constexpr std::size_t observation_budget = 8 * 1024 * 1024, replay_budget = 16 * 1024 * 1024;
std::atomic<std::uint64_t> epochs{0};

std::uint64_t decimal(const std::string &value) {
    std::uint64_t result = 0;
    auto p = std::from_chars(value.data(), value.data() + value.size(), result);

    if (value.empty() || (value.size() > 1 && value[0] == '0') || p.ec != std::errc() ||
        p.ptr != value.data() + value.size()) {
        throw std::invalid_argument("invalid watch cursor");
    }

    return result;
}

ConditionResult unavailable(const char *reason) {
    return {"unavailable", reason};
}
} // namespace

WatchHub::WatchHub(std::size_t capacity) : capacity_(capacity), epoch_(std::to_string(++epochs)) {
    if (!capacity || capacity > 4096) {
        throw std::invalid_argument("watch capacity must be 1..4096");
    }
}

WatchReady WatchHub::start(Json condition, std::int64_t lease, std::int64_t now) {
    validate_condition(condition);

    if (lease <= 0 || lease > 86400000 || now < 0 ||
        now > std::numeric_limits<std::int64_t>::max() - lease) {
        throw std::invalid_argument("invalid watch lease or clock");
    }

    std::lock_guard lock(mutex_);
    WatchReady r{"unavailable", "", epoch_, std::to_string(sequence_),
                 unavailable("baseline unavailable")};

    if (closed_) {
        r.result = unavailable("watch hub closed");

        return r;
    }

    if (!latest_ || latest_->epoch.empty() || latest_->gap || latest_->available.empty()) {
        return r;
    }

    if (watches_.size() >= 64 || baseline_bytes_ + latest_bytes_ > observation_budget) {
        r.result = unavailable("watch resource limit");

        return r;
    }

    r.watch_id = std::to_string(++next_id_);
    r.status = "ready";
    r.result = evaluate_condition(condition, *latest_, {}, now);
    watches_.emplace(r.watch_id,
                     Watch{std::move(condition), latest_, sequence_, now + lease, latest_bytes_});
    baseline_bytes_ += latest_bytes_;

    return r;
}

void WatchHub::publish(ConditionObservation observation) {
    // Count serialized facts and conservative per-node storage; do not truncate facts.
    auto bytes = observation.state.dump().size() * 16 + sizeof(ConditionObservation) +
                 observation.epoch.size();

    for (const auto &path : observation.available) {
        bytes += path.size() + sizeof(std::string);
    }

    if (bytes > observation_budget) {
        observation.state = Json();
        observation.available.clear();
        observation.epoch.clear();
        observation.gap = true;
        bytes = sizeof(ConditionObservation);
    }

    std::lock_guard lock(mutex_);

    if (closed_) {
        return;
    }

    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("watch sequence exhausted");
    }

    if (latest_ &&
        (observation.epoch != latest_->epoch || observation.monotonic_ms < latest_->monotonic_ms)) {
        observation.gap = true;
    }

    latest_ = std::make_shared<const ConditionObservation>(std::move(observation));
    latest_bytes_ = bytes;
    entries_.push_back({++sequence_, bytes, latest_});
    replay_bytes_ += bytes;

    while (entries_.size() > capacity_ || replay_bytes_ > replay_budget) {
        replay_bytes_ -= entries_.front().bytes;
        entries_.pop_front();
    }

    changed_.notify_all();
}

WatchRead WatchHub::read(const std::string &id, const std::string &cursor,
                         std::chrono::milliseconds timeout, std::size_t limit, std::int64_t now) {
    auto after = decimal(cursor);

    if (timeout.count() < 0 || timeout > std::chrono::seconds(60) || !limit || limit > 4096 ||
        now < 0) {
        throw std::invalid_argument("invalid watch read bounds");
    }

    std::unique_lock lock(mutex_);
    auto found = watches_.find(id);

    if (found != watches_.end() && (after < found->second.sequence || after > sequence_)) {
        throw std::invalid_argument("cursor outside watch");
    }

    const auto wall_start = std::chrono::steady_clock::now();
    auto bounded_timeout = timeout;

    if (found != watches_.end()) {
        bounded_timeout = std::min(timeout, std::chrono::milliseconds(std::max<std::int64_t>(
                                                    0, found->second.expires - now)));
    }

    changed_.wait_for(lock, bounded_timeout, [&] {
        return closed_ || !watches_.count(id) || sequence_ > after;
    });
    WatchRead r;
    r.epoch = epoch_;
    r.cursor = cursor;
    found = watches_.find(id);

    if (closed_ || found == watches_.end()) {
        r.status = "ended";
        r.result = unavailable("watch stopped, expired or closed");

        return r;
    }

    auto watch = found->second;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - wall_start)
                           .count();
    auto evaluation_now = now + elapsed;

    if (evaluation_now >= watch.expires) {
        baseline_bytes_ -= watch.bytes;
        watches_.erase(found);
        changed_.notify_all();
        r.status = "ended";
        r.result = unavailable("watch lease expired");

        return r;
    }

    r.gap = !entries_.empty() && watch.sequence < entries_.front().sequence - 1;
    std::vector<std::shared_ptr<const ConditionObservation>> history;

    for (const auto &e : entries_) {
        if (e.sequence > watch.sequence) {
            history.push_back(e.observation);
        }

        if (e.sequence > after && r.events.size() < limit) {
            r.events.push_back({std::to_string(e.sequence), e.observation});
            r.cursor = std::to_string(e.sequence);
        }
    }

    r.gap |= !entries_.empty() && after < entries_.front().sequence - 1;
    r.status = r.events.empty() ? "timeout" : "events";
    lock.unlock();

    if (r.gap) {
        r.result = unavailable("watch replay retention gap");

        return r;
    }

    std::vector<ConditionObservation> observations;
    observations.reserve(history.size());

    for (const auto &o : history) {
        observations.push_back(*o);
    }

    r.result = evaluate_condition(watch.condition, *watch.baseline, observations, evaluation_now);

    return r;
}

void WatchHub::stop(const std::string &id) {
    std::lock_guard lock(mutex_);
    auto w = watches_.find(id);

    if (w != watches_.end()) {
        baseline_bytes_ -= w->second.bytes;
        watches_.erase(w);
    }

    changed_.notify_all();
}

void WatchHub::tick(std::int64_t now) {
    std::lock_guard lock(mutex_);

    for (auto w = watches_.begin(); w != watches_.end();) {
        if (now >= w->second.expires) {
            baseline_bytes_ -= w->second.bytes;
            w = watches_.erase(w);
        } else {
            ++w;
        }
    }

    changed_.notify_all();
}

std::size_t WatchHub::active(std::int64_t now) {
    std::lock_guard lock(mutex_);

    for (auto w = watches_.begin(); w != watches_.end();) {
        if (now >= w->second.expires) {
            baseline_bytes_ -= w->second.bytes;
            w = watches_.erase(w);
        } else {
            ++w;
        }
    }

    changed_.notify_all();

    return watches_.size();
}

void WatchHub::close() {
    std::lock_guard lock(mutex_);

    closed_ = true;
    watches_.clear();
    entries_.clear();
    latest_.reset();
    baseline_bytes_ = replay_bytes_ = latest_bytes_ = 0;
    changed_.notify_all();
}
} // namespace voyemu::runtime
