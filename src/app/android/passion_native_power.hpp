#pragma once
#include "error.hpp"
#include "passion_power_owner.hpp"
#include <memory>
#include <mutex>
namespace voyemu::media {
// Runtime observer updates this view; only the existing input worker calls before().
class PassionNativePower {
public:
    using ClosedWake = std::function<Json(unsigned, const std::string &)>;

    PassionNativePower(std::string session, ClosedWake wake, PassionPointerWake::Recheck recheck,
                       PassionPointerWake::Clock clock)
        : session_(std::move(session)), wake_(std::move(wake)), recheck_(std::move(recheck)),
          clock_(std::move(clock)) {}

    // Input-worker cancellation has no display argument: it closes whichever gesture was retained.
    void cancel(const std::function<void()> &release) {
        unknown_gesture_.reset();
        gate_.reset();
        gate_boot_.clear();
        gate_epoch_.clear();
        release();
    }

    // Only fresh, parser-validated OFF proof may suppress a presentation quad.
    std::array<bool, 2> off_displays() const {
        std::lock_guard lock(mutex_);
        const auto now = clock_();
        std::array<bool, 2> off{};

        for (unsigned d = 0; d < 2; ++d) {
            off[d] = states_[d] && states_[d].value().state == 1 &&
                     states_[d].value().expires_us > now;
        }

        return off;
    }

    std::string boot() const {
        std::lock_guard lock(mutex_);

        return binding_ ? binding_->boot : std::string{};
    }

    void observe(const Json &runtime) {
        std::lock_guard lock(mutex_);

        try {
            if (runtime.at("session_id") != session_) {
                throw VoyemuError("OEM_DISPLAY_POWER", "foreign runtime");
            }

            const auto &android = runtime.at("deliveries").at("android");

            if (!android.contains("display_power")) {
                clear();

                return;
            }

            const auto &value = android.at("display_power");

            if (value.at("capability") != PassionPowerOwner::capability()) {
                throw VoyemuError("OEM_DISPLAY_POWER", "unreviewed native capability");
            }

            enabled_ = true;

            if (value.at("schema") != 1 || value.at("protocol") != "passion-display-power-v1" ||
                value.at("session_id") != session_) {
                throw VoyemuError("OEM_DISPLAY_POWER", "native binding mismatch");
            }

            const auto boot = value.at("boot_id").get<std::string>(),
                       epoch = value.at("watch_id").get<std::string>();

            if (boot.empty() || epoch.empty()) {
                clear();

                return;
            }

            bool changed = !binding_ || binding_->boot != boot || binding_->epoch != epoch;

            if (changed) {
                clear();
                binding_ = PassionPointerWake::Binding{true, "passion-11-russian-20260403-2.1.4.80",
                                                       session_, boot, epoch};
            }

            const auto &items = value.at("states");

            if (!items.is_array() || items.size() > 2) {
                throw VoyemuError("OEM_DISPLAY_POWER", "native pair invalid");
            }

            std::array<std::optional<PassionDisplayProof>, 2> next;
            std::array<bool, 2> seen{};

            for (const auto &item : items) {
                auto p = parse(item, false);

                if (seen[p.display]) {
                    throw VoyemuError("OEM_DISPLAY_POWER", "duplicate native display");
                }

                seen[p.display] = true;

                if (states_[p.display] && states_[p.display].value().order > p.order) {
                    next[p.display] = states_[p.display];
                } else {
                    next[p.display] = std::move(p);
                }
            }

            states_ = std::move(next);
        } catch (...) {
            clear();
            throw;
        }
    }

    PassionPointerWake::Result before(PassionPointerWake::Phase phase, unsigned display) {
        using Phase = PassionPointerWake::Phase;
        using Result = PassionPointerWake::Result;

        if (display > 1) {
            throw VoyemuError("OEM_DISPLAY_POWER", "canonical native display required");
        }

        if (unknown_gesture_) {
            if (*unknown_gesture_ != display) {
                throw VoyemuError("OEM_DISPLAY_POWER", "unknown gesture crossed displays");
            }

            if (phase == Phase::up) {
                unknown_gesture_.reset();
            }

            return Result::consumed;
        }

        std::optional<PassionPointerWake::Binding> binding;
        bool enabled;
        {
            std::lock_guard lock(mutex_);
            enabled = enabled_;
            binding = binding_;
        }

        if (!enabled) {
            return Result::pass;
        }

        if (phase == Phase::down) {
            if (!binding) {
                unknown_gesture_ = display;
                recheck_();
                throw VoyemuError("OEM_DISPLAY_POWER", "asynchronous initial state required");
            }

            if (!gate_ || gate_boot_ != binding->boot || gate_epoch_ != binding->epoch) {
                gate_boot_ = binding->boot;
                gate_epoch_ = binding->epoch;
                gate_ = std::make_unique<PassionPointerWake>(
                        *binding,
                        [this](unsigned d) {
                            std::lock_guard lock(mutex_);

                            if (d > 1) {
                                return std::optional<PassionDisplayProof>{};
                            }

                            return states_[d];
                        },
                        [this, expected_boot = binding->boot,
                         expected_epoch = binding->epoch](unsigned d) {
                            {
                                std::lock_guard lock(mutex_);

                                if (!binding_ || binding_->boot != expected_boot ||
                                    binding_->epoch != expected_epoch) {
                                    throw VoyemuError("OEM_DISPLAY_POWER",
                                                      "native boot changed before wake");
                                }
                            }
                            auto result = wake_(d, expected_boot);
                            std::lock_guard lock(mutex_);

                            if (!binding_ || binding_->boot != expected_boot ||
                                binding_->epoch != expected_epoch) {
                                throw VoyemuError("OEM_DISPLAY_POWER",
                                                  "native boot changed after wake");
                            }

                            auto p = parse(result, true);

                            if (!states_[d] || states_[d]->state != 1 ||
                                p.order <= states_[d]->order || p.request <= states_[d]->request ||
                                p.transaction <= states_[d]->transaction) {
                                throw VoyemuError("OEM_DISPLAY_POWER", "stale native wake result");
                            }

                            auto stored = p;
                            stored.kind = "applied";
                            states_[d] = std::move(stored);

                            return p;
                        },
                        recheck_, clock_);
            }
        }

        return gate_ ? gate_->before(phase, display) : Result::pass;
    }

private:
    void clear() {
        for (auto &state : states_) {
            state.reset();
        }
    }

    PassionDisplayProof parse(const Json &j, bool wake) const {
        if (wake && !j.value("ok", false)) {
            throw VoyemuError("OEM_DISPLAY_POWER", "native wake not completed");
        }

        auto nonnegative = [](const Json &v) {
            if (!v.is_number_integer() || (!v.is_number_unsigned() && v.get<std::int64_t>() < 0)) {
                throw VoyemuError("OEM_DISPLAY_POWER", "positive native field required");
            }

            return v.get<std::uint64_t>();
        };
        const auto display = nonnegative(j.at(wake ? "display_id" : "display"));

        if (display > 1 || !binding_) {
            throw VoyemuError("OEM_DISPLAY_POWER", "canonical native binding absent");
        }

        auto state = nonnegative(j.at("state"));
        auto expires = nonnegative(j.at("expires_monotonic_us"));
        auto now = clock_();
        auto kind = j.at("kind").get<std::string>(), unique = j.at("unique_id").get<std::string>();
        auto request = nonnegative(j.at("request")), transaction = nonnegative(j.at("transaction")),
             order = nonnegative(j.at("order"));
        auto sampled = j.at("observed_ms").get<std::int64_t>();

        if (j.at("session_id") != session_ || j.at("boot_id") != binding_->boot ||
            j.at("watch_id") != binding_->epoch ||
            unique != (display == 0 ? "local:8141241408256768"
                                    : "virtual:com.android.emulator.multidisplay:1234562") ||
            (state != 1 && state != 2) || !order || sampled <= 0 || expires <= now ||
            expires - now > 2500000 || (!wake && kind != "observed" && kind != "applied") ||
            (!wake && kind == "applied" && (!request || !transaction)) ||
            (wake && (kind != "wake_observed" || state != 2 || !request || !transaction))) {
            throw VoyemuError("OEM_DISPLAY_POWER", "fresh native proof required");
        }

        return {session_,
                binding_->boot,
                binding_->epoch,
                unique,
                kind,
                static_cast<unsigned>(display),
                static_cast<int>(state),
                request,
                transaction,
                expires,
                order,
                sampled};
    }

    std::string session_;
    ClosedWake wake_;
    PassionPointerWake::Recheck recheck_;
    PassionPointerWake::Clock clock_;
    mutable std::mutex mutex_;
    bool enabled_{};
    std::optional<PassionPointerWake::Binding> binding_;
    std::array<std::optional<PassionDisplayProof>, 2> states_;
    std::optional<unsigned> unknown_gesture_;
    std::unique_ptr<PassionPointerWake> gate_;
    std::string gate_boot_, gate_epoch_;
};
} // namespace voyemu::media
