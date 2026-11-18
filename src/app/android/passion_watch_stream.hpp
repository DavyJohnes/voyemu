#pragma once
#include "error.hpp"
#include "passion_display_cache.hpp"
#include <deque>
namespace voyemu::media {
// Capability callbacks close over retained owned ChildProcess handles, never numeric PIDs.
class PassionWatchStream {
public:
    struct Child {
        std::function<bool()> alive;
        std::function<std::string(std::size_t)> read;
        std::function<void()> stop;
        std::function<void()> preserve;
    };
    using Launch = std::function<Child(const std::vector<std::string> &)>;
    using Fence = std::function<void()>;
    using Accept = std::function<void(const Json &)>;
    using Lost = std::function<void()>;
    using Raw = std::function<void(const std::string &)>;

    PassionWatchStream(PassionPointerWake::Binding binding, Launch launch, Fence fence,
                       Accept accept, Lost lost, Raw raw = {})
        : binding_(std::move(binding)), launch_(std::move(launch)), fence_(std::move(fence)),
          accept_(std::move(accept)), lost_(std::move(lost)), raw_(std::move(raw)) {}

    ~PassionWatchStream() {
        try {
            close();
        } catch (...) { /* Ignored: destructors must not throw. */
        }
    }

    void close() {
        std::exception_ptr failure;

        for (auto it = slots_.begin(); it != slots_.end();) {
            try {
                it->child.stop();

                if (it->child.alive()) {
                    throw VoyemuError("PASSION_WATCH", "Owned watch still alive");
                }

                it = slots_.erase(it);
            } catch (...) {
                failure = std::current_exception();
                ++it;
            }
        }

        lost_();

        if (failure) {
            std::rethrow_exception(failure);
        }
    }

    void recheck() {
        refresh_ = true;
    }

    // Owner tick only. Two retained children maximum; protocol/EOF failures back off two seconds.
    void poll(std::uint64_t now_us) {
        if (now_us < retry_at_) {
            return;
        }

        try {
            fence_();

            for (auto &slot : slots_) {
                drain(slot);
            }

            for (auto it = slots_.begin(); it != slots_.end();) {
                bool alive = it->child.alive();

                if (!alive || now_us - it->started >= 24000000) {
                    if (!it->pending.empty() || (!alive && !it->superseded && !it->ended)) {
                        if (it->child.preserve) {
                            it->child.preserve();
                        }

                        throw VoyemuError("PASSION_WATCH",
                                          "Owned watch ended without complete terminal frame");
                    }

                    it->child.stop();

                    if (it->child.alive()) {
                        throw VoyemuError("PASSION_WATCH", "Expired owned watcher still alive");
                    }

                    it = slots_.erase(it);
                } else {
                    ++it;
                }
            }

            bool due = refresh_ || slots_.empty() || slots_.back().ended ||
                       now_us - slots_.back().started >= 18000000;

            if (due && slots_.size() < 2) {
                auto child = launch_({"shell", "cmd", "display", "voyemu-passion-screen", "watch",
                                      binding_.session, binding_.epoch, binding_.boot});

                if (!child.alive || !child.read || !child.stop) {
                    if (child.stop) {
                        child.stop();
                    }

                    throw VoyemuError("PASSION_WATCH", "Owned watcher capability incomplete");
                }

                for (auto &slot : slots_) {
                    slot.superseded = true;
                }

                slots_.push_back({std::move(child), now_us, {}});
                refresh_ = false;
                drain(slots_.back());
            }
        } catch (...) {
            auto failure = std::current_exception();
            retry_at_ = now_us + 2000000;
            try {
                close();
            } catch (...) {
                throw VoyemuError("PASSION_WATCH",
                                  "Owned watch cleanup unverified; no state admitted");
            }

            std::rethrow_exception(failure);
        }
    }

    std::size_t retained() const {
        return slots_.size();
    }

private:
    struct Slot {
        Child child;
        std::uint64_t started;
        std::string pending;
        bool superseded{}, ended{};
    };

    void drain(Slot &slot) {
        try {
            auto bytes = slot.child.read(65536);

            if (bytes.size() > 65536 || slot.pending.size() + bytes.size() > 65536) {
                throw VoyemuError("PASSION_WATCH", "Bounded watch output exceeded");
            }

            slot.pending += bytes;
            std::size_t pos;

            while ((pos = slot.pending.find('\n')) != std::string::npos) {
                if (pos > 4096) {
                    throw VoyemuError("PASSION_WATCH", "Bounded watch line exceeded");
                }

                auto line = slot.pending.substr(0, pos);
                slot.pending.erase(0, pos + 1);

                if (line.empty()) {
                    continue;
                }

                auto event = Json::parse(line);

                if (event.value("session_id", std::string{}) != binding_.session ||
                    event.value("watch_id", std::string{}) != binding_.epoch ||
                    event.value("boot_id", std::string{}) != binding_.boot) {
                    throw VoyemuError("PASSION_WATCH", "Watch incarnation changed");
                }

                if (event.value("kind", std::string{}) == "display_state_end") {
                    if (event.at("schema") != 1 ||
                        (event.at("reason") != "lease" && event.at("reason") != "byte_budget")) {
                        throw VoyemuError("PASSION_WATCH", "Watch terminal frame invalid");
                    }

                    slot.ended = true;
                    continue;
                }

                if (slot.ended || event.value("kind", std::string{}) != "display_state_event") {
                    throw VoyemuError("PASSION_WATCH", "Late or malformed watch event");
                }

                if (raw_) {
                    raw_(line);
                }

                accept_(event);
            }

            if (slot.pending.size() > 4096) {
                throw VoyemuError("PASSION_WATCH", "Bounded partial watch line exceeded");
            }
        } catch (...) {
            if (slot.child.preserve) {
                slot.child.preserve();
            }

            throw;
        }
    }

    PassionPointerWake::Binding binding_;
    Launch launch_;
    Fence fence_;
    Accept accept_;
    Lost lost_;
    Raw raw_;
    std::deque<Slot> slots_;
    bool refresh_{};
    std::uint64_t retry_at_{};
};
} // namespace voyemu::media
