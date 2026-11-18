#include "source_registry.hpp"
#include "error.hpp"
#include <algorithm>
#include <stdexcept>
namespace voyemu::runtime {
SourceCoordinator::SourceCoordinator(WatchHub &hub) : hub_(hub) {}

SourceCoordinator::~SourceCoordinator() {
    stop();
}

std::int64_t SourceCoordinator::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

void SourceCoordinator::add(SourceDefinition source) {
    std::lock_guard lock(mutex_);

    if (started_ || stopped_ || slots_.size() >= 8 || source.id.empty() || source.id.size() > 64 ||
        !source.collect || source.interval.count() < 10 || source.interval.count() > 60000 ||
        source.budget.count() < 1 || source.budget.count() > 10000) {
        throw std::invalid_argument("invalid source registration");
    }

    if (source.id.front() < 97 || source.id.front() > 122 ||
        !std::all_of(source.id.begin(), source.id.end(), [](unsigned char c) {
            return (c >= 97 && c <= 122) || (c >= 48 && c <= 57) || c == 95 || c == 45;
        })) {
        throw std::invalid_argument("invalid source id");
    }

    for (const auto &slot : slots_) {
        if (slot->definition.id == source.id) {
            throw std::invalid_argument("duplicate source");
        }
    }

    auto slot = std::make_unique<Slot>();
    slot->definition = std::move(source);
    slots_.push_back(std::move(slot));
}

void SourceCoordinator::publish_to(WatchHub &hub, std::vector<std::string> sources) {
    std::lock_guard lock(mutex_);

    if (stopped_ || sources.empty() || destinations_.size() >= 8 || &hub == &hub_) {
        throw std::invalid_argument("invalid watch projection");
    }

    std::sort(sources.begin(), sources.end());

    if (std::adjacent_find(sources.begin(), sources.end()) != sources.end()) {
        throw std::invalid_argument("duplicate projected source");
    }

    for (const auto &id : sources) {
        if (std::none_of(slots_.begin(), slots_.end(), [&](const auto &slot) {
                return slot->definition.id == id;
            })) {
            throw std::invalid_argument("unknown projected source");
        }
    }

    for (const auto &d : destinations_) {
        if (d.hub == &hub) {
            throw std::invalid_argument("duplicate projected hub");
        }
    }

    destinations_.push_back({&hub, std::move(sources), 1});
}

void SourceCoordinator::remove_projection(WatchHub &hub) {
    std::lock_guard lock(mutex_);

    std::erase_if(destinations_, [&](const auto &destination) {
        return destination.hub == &hub;
    });
}

std::vector<std::string> SourceCoordinator::select(const Json &sources) const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> selected;

    if (sources.is_null()) {
        for (const auto &slot : slots_) {
            selected.push_back(slot->definition.id);
        }
    } else {
        if (!sources.is_array() || sources.empty() || sources.size() > slots_.size()) {
            throw VoyemuError("TEST_ARGUMENT",
                              "sources must be a nonempty array of registered IDs");
        }

        for (const auto &id : sources) {
            if (!id.is_string()) {
                throw VoyemuError("TEST_ARGUMENT", "source ID must be a string");
            }

            const auto value = id.get<std::string>();

            if (std::none_of(slots_.begin(), slots_.end(), [&](const auto &slot) {
                    return slot->definition.id == value;
                })) {
                throw VoyemuError("TEST_ARGUMENT", "unknown source ID");
            }

            selected.push_back(value);
        }
    }

    std::sort(selected.begin(), selected.end());

    if (std::adjacent_find(selected.begin(), selected.end()) != selected.end()) {
        throw VoyemuError("TEST_ARGUMENT", "duplicate source ID");
    }

    return selected;
}

bool SourceCoordinator::demanded(const std::string &source) {
    std::lock_guard lock(mutex_);
    const auto now = now_ms();

    if (stopped_) {
        return false;
    }

    for (const auto &slot : slots_) {
        if (slot->definition.id == source && slot->demand_until >= now) {
            return true;
        }
    }

    for (const auto &destination : destinations_) {
        if (std::find(destination.sources.begin(), destination.sources.end(), source) !=
                    destination.sources.end() &&
            destination.hub->active(now)) {
            return true;
        }
    }

    return false;
}

void SourceCoordinator::start() {
    std::lock_guard lock(mutex_);

    if (started_ || stopped_ || slots_.empty()) {
        throw std::invalid_argument("invalid coordinator start");
    }

    started_ = true;

    for (auto &slot : slots_) {
        slot->worker = std::thread([this, p = slot.get()] {
            pump(*p);
        });
    }
}

Json SourceCoordinator::state_locked() const {
    Json sources = Json::object();
    bool complete = true;

    for (const auto &slot : slots_) {
        const auto &s = slot->sample;
        bool ready = slot->initialized && s.status == "ready";
        bool readable = slot->initialized && (s.status == "ready" || s.status == "partial");
        complete &= ready;
        sources[slot->definition.id] = {
                {"status", readable ? s.status : "unavailable"},
                {"source_instance", s.instance},
                {"boot_id", s.boot_id},
                {"revision", s.revision},
                {"value", readable ? s.value : Json()},
                {"coverage", {{"continuous", ready && s.continuous}, {"gap", s.gap}}},
                {"host_interval",
                 {{"clock", "host.steady"},
                  {"start_ms", std::to_string(slot->began)},
                  {"end_ms", std::to_string(slot->ended)}}}};
    }

    return {{"sources", std::move(sources)},
            {"complete", complete},
            {"atomic", false},
            {"epoch", std::to_string(epoch_)}};
}

void SourceCoordinator::publish_locked(const std::string &source, const SourceSample &fresh,
                                       bool boundary) {
    auto publish = [&](WatchHub &hub, const std::vector<std::string> &selected,
                       std::uint64_t epoch) {
        if (!selected.empty() &&
            std::find(selected.begin(), selected.end(), source) == selected.end()) {
            return;
        }

        std::vector<const Slot *> included;

        for (const auto &slot : slots_) {
            if (selected.empty() || std::find(selected.begin(), selected.end(),
                                              slot->definition.id) != selected.end()) {
                if (!slot->initialized) {
                    return;
                }

                included.push_back(slot.get());
            }
        }

        ConditionObservation observation;
        observation.state = state_locked();

        // The projection reports only selected source facts and their completeness.
        if (!selected.empty()) {
            Json sources = Json::object();
            bool complete = true;

            for (const auto *slot : included) {
                sources[slot->definition.id] = observation.state["sources"][slot->definition.id];
                complete &= slot->sample.status == "ready";
            }

            observation.state["sources"] = std::move(sources);
            observation.state["complete"] = complete;
        }

        observation.state["epoch"] = std::to_string(epoch);
        observation.monotonic_ms = now_ms();
        observation.epoch = std::to_string(epoch);
        observation.continuous = included.size() == 1;
        observation.gap = false;

        for (const auto *slot : included) {
            const auto &s = slot->sample;

            if (s.status == "ready") {
                observation.available.push_back("/sources/" + slot->definition.id);
            }

            bool covered = s.continuous && s.covered_through_ms > 0 &&
                           s.covered_through_ms <= slot->ended;
            observation.continuous &= s.status == "ready" && covered && !s.gap;
            observation.gap |= s.gap;
        }

        if (observation.continuous) {
            observation.monotonic_ms = included.front()->sample.covered_through_ms;
        }

        for (const auto &event : fresh.events) {
            observation.state["event"] = event;
            observation.available.push_back("/event");
            observation.event_watermark = false;
            hub.publish(observation);
            observation.available.pop_back();
        }

        observation.state.erase("event");
        observation.event_watermark = observation.continuous;
        hub.publish(std::move(observation));
    };

    publish(hub_, {}, epoch_);

    for (auto &destination : destinations_) {
        bool selected = std::find(destination.sources.begin(), destination.sources.end(), source) !=
                        destination.sources.end();

        if (selected && boundary) {
            ++destination.epoch;
        }

        publish(*destination.hub, destination.sources, destination.epoch);
    }
}

void SourceCoordinator::pump(Slot &slot) {
    std::unique_lock lock(mutex_);

    while (!cancelled_) {
        auto target = std::max(slot.requested, slot.generation + 1);
        auto began = now_ms();
        auto deadline = std::chrono::steady_clock::now() + slot.definition.budget;
        lock.unlock();
        SourceSample sample;
        try {
            sample = slot.definition.collect(SourceContext{deadline, cancelled_});
        } catch (...) {
            sample.gap = true;
        }

        auto ended = now_ms();
        bool valid = std::chrono::steady_clock::now() <= deadline &&
                     (sample.status == "ready" || sample.status == "partial") &&
                     !sample.instance.empty() && !sample.boot_id.empty() &&
                     sample.instance.size() <= 256 && sample.boot_id.size() <= 256 &&
                     sample.revision.size() <= 256 && sample.events.size() <= 256;
        try {
            std::size_t bytes = sample.value.dump().size();

            for (const auto &event : sample.events) {
                bytes += event.dump().size();
            }

            valid &= bytes <= 512 * 1024;
        } catch (...) {
            valid = false;
        }

        if (!valid) {
            sample = SourceSample{};
            sample.gap = true;
        }

        lock.lock();

        if (cancelled_) {
            break;
        }

        bool boundary = slot.initialized && (slot.sample.instance != sample.instance ||
                                             slot.sample.boot_id != sample.boot_id);

        if (boundary) {
            ++epoch_;
        }

        slot.sample = std::move(sample);
        slot.initialized = true;
        slot.began = began;
        slot.ended = ended;
        slot.generation = target;
        publish_locked(slot.definition.id, slot.sample, boundary);
        changed_.notify_all();
        changed_.wait_for(lock, slot.definition.interval, [&] {
            return cancelled_ || slot.requested > slot.generation;
        });
    }
}

Json SourceCoordinator::snapshot(std::chrono::milliseconds timeout,
                                 std::vector<std::string> selected) {
    if (timeout.count() < 0 || timeout.count() > 60000) {
        throw std::invalid_argument("invalid snapshot timeout");
    }

    selected = select(selected.empty() ? Json() : Json(selected));
    std::unique_lock lock(mutex_);

    if (!started_ || stopped_) {
        return {{"status", "unavailable"}, {"atomic", false}};
    }

    std::vector<std::pair<Slot *, std::uint64_t>> targets;

    for (auto &slot : slots_) {
        if (std::find(selected.begin(), selected.end(), slot->definition.id) != selected.end()) {
            slot->demand_until = std::max<std::int64_t>(slot->demand_until,
                                                        now_ms() + timeout.count() + 1000);
            slot->requested = slot->generation + 1;
            targets.emplace_back(slot.get(), slot->requested);
        }
    }

    changed_.notify_all();
    bool fresh = changed_.wait_for(lock, timeout, [&] {
        if (stopped_) {
            return true;
        }

        for (const auto &[slot, target] : targets) {
            if (slot->generation < target) {
                return false;
            }
        }

        return true;
    });
    auto result = state_locked();
    Json values = Json::object();
    bool complete = fresh && !stopped_;

    for (const auto &[slot, target] : targets) {
        auto value = result["sources"][slot->definition.id];

        if (slot->generation < target) {
            value["status"] = "unavailable";
            value["value"] = nullptr;
            value["coverage"]["continuous"] = false;
        }

        complete &= value["status"] == "ready";
        values[slot->definition.id] = std::move(value);
    }

    result["sources"] = std::move(values);
    result["complete"] = complete;
    result["status"] = stopped_ ? "unavailable" : fresh ? "ready" : "timeout";
    result["fresh"] = fresh && !stopped_;

    return result;
}

void SourceCoordinator::stop() {
    {
        std::lock_guard lock(mutex_);

        if (stopped_) {
            return;
        }

        stopped_ = true;
        cancelled_ = true;
        changed_.notify_all();
    }

    for (auto &slot : slots_) {
        if (slot->worker.joinable()) {
            slot->worker.join();
        }
    }
}
} // namespace voyemu::runtime
