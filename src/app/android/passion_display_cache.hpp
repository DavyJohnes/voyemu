#pragma once
#include "error.hpp"
#include "model.hpp"
#include "passion_pointer_wake.hpp"
#include <array>
namespace voyemu::media {
// Only Session's retained same-SDK watch reader may feed this cache; no public injection route.
class PassionDisplayCache {
public:
    explicit PassionDisplayCache(PassionPointerWake::Binding binding)
        : binding_(std::move(binding)) {}

    void invalidate() {
        for (auto &p : states_) {
            p.reset();
        }
    }

    bool accept(const Json &event, std::uint64_t host_now_us, std::int64_t guest_now_ms) {
        last_accepted_ = false;
        last_reason_ = "unvalidated";
        last_host_us_ = host_now_us;
        last_guest_ms_ = guest_now_ms;
        try {
            if (event.at("schema") != 1 || event.at("kind") != "display_state_event" ||
                event.at("session_id") != binding_.session ||
                event.at("watch_id") != binding_.epoch || event.at("boot_id") != binding_.boot) {
                last_reason_ = "envelope";
                invalidate();

                return false;
            }

            auto sequence = positive(event.at("sequence"));

            if (!sequence || sequence <= sequence_) {
                last_reason_ = "sequence";

                return false;
            }

            const auto &items = event.at("states");

            if (!items.is_array() || items.size() > 2) {
                last_reason_ = "state_count";
                invalidate();

                return false;
            }

            std::array<std::optional<PassionDisplayProof>, 2> next;
            std::array<bool, 2> seen{};

            for (const auto &item : items) {
                unsigned display = item.at("display").get<unsigned>();

                if (display > 1 || seen[display]) {
                    throw VoyemuError("OEM_DISPLAY_STATE", "Duplicate display state");
                }

                seen[display] = true;
                const auto sampled = item.at("observed_ms").get<std::int64_t>();

                if (sampled <= 0 || sampled > guest_now_ms + 250 || sampled < guest_now_ms - 1500) {
                    continue;
                }

                int state = item.at("state").get<int>();
                std::string kind = item.at("kind").get<std::string>(),
                            unique = item.at("unique_id").get<std::string>();
                const std::string expected =
                        display == 0 ? "local:8141241408256768"
                                     : "virtual:com.android.emulator.multidisplay:1234562";

                if (unique != expected || (state != 1 && state != 2) ||
                    (kind != "observed" && kind != "applied")) {
                    throw VoyemuError("OEM_DISPLAY_STATE", "Canonical observed state required");
                }

                auto request = nonnegative(item.at("request")),
                     transaction = nonnegative(item.at("transaction")),
                     order = positive(item.at("order"));

                if (kind == "applied" && (!request || !transaction)) {
                    throw VoyemuError("OEM_DISPLAY_STATE", "Applied proof absent");
                }

                auto age = guest_now_ms > sampled
                                   ? static_cast<std::uint64_t>(guest_now_ms - sampled)
                                   : 0;
                next[display] = PassionDisplayProof{binding_.session,
                                                    binding_.boot,
                                                    binding_.epoch,
                                                    unique,
                                                    kind,
                                                    display,
                                                    state,
                                                    request,
                                                    transaction,
                                                    host_now_us + (2500 - age) * 1000,
                                                    order,
                                                    sampled};

                if (states_[display] && states_[display].value().order > order) {
                    next[display] = states_[display];
                }
            }

            sequence_ = sequence;
            states_ = std::move(next);
            last_accepted_ = true;
            last_reason_ = states_[0] || states_[1] ? "admitted"
                           : items.empty()          ? "empty_snapshot"
                                                    : "no_fresh_samples";

            return true;
        } catch (const std::exception &) {
            last_reason_ = "invalid_state";
            invalidate();

            return false;
        }
    }

    bool confirmWake(const PassionDisplayProof &p, std::uint64_t now_us) {
        if (p.display > 1 || p.session != binding_.session || p.boot != binding_.boot ||
            p.epoch != binding_.epoch ||
            p.unique != (p.display == 0 ? "local:8141241408256768"
                                        : "virtual:com.android.emulator.multidisplay:1234562") ||
            p.kind != "wake_observed" || p.state != 2 || !p.request || !p.transaction || !p.order ||
            p.observed_ms <= 0 || p.expires_us <= now_us) {
            return false;
        }

        auto &prior = states_[p.display];

        if (prior && prior->order >= p.order) {
            return false;
        }

        prior = p;
        prior->kind = "applied";

        return true;
    }

    std::optional<PassionDisplayProof> get(unsigned display, std::uint64_t now_us) const {
        if (display > 1 || !states_[display] || states_[display].value().expires_us <= now_us) {
            return {};
        }

        return states_[display];
    }

    Json record(std::uint64_t now_us) const {
        Json result = Json::array();

        for (unsigned id = 0; id < 2; id++) {
            if (auto p = get(id, now_us)) {
                result.push_back({{"session_id", p->session},
                                  {"boot_id", p->boot},
                                  {"watch_id", p->epoch},
                                  {"display", id},
                                  {"unique_id", p->unique},
                                  {"kind", p->kind},
                                  {"state", p->state},
                                  {"request", p->request},
                                  {"transaction", p->transaction},
                                  {"expires_monotonic_us", p->expires_us},
                                  {"order", p->order},
                                  {"observed_ms", p->observed_ms}});
            }
        }

        return result;
    }

    Json diagnostic() const {
        return {{"source", "retained_sameboot_watch"},
                {"accepted_envelope", last_accepted_},
                {"reason", last_reason_},
                {"host_monotonic_us", last_host_us_},
                {"comparison_guest_ms", last_guest_ms_}};
    }

private:
    std::string last_reason_ = "not_observed";
    bool last_accepted_{};
    std::uint64_t last_host_us_{};
    std::int64_t last_guest_ms_{};

    static std::uint64_t nonnegative(const Json &v) {
        if (!v.is_number_integer() ||
            (v.is_number_integer() && !v.is_number_unsigned() && v.get<std::int64_t>() < 0)) {
            throw VoyemuError("OEM_DISPLAY_STATE", "Nonnegative integer required");
        }

        return v.get<std::uint64_t>();
    }

    static std::uint64_t positive(const Json &v) {
        auto n = nonnegative(v);

        if (!n) {
            throw VoyemuError("OEM_DISPLAY_STATE", "Positive order required");
        }

        return n;
    }

    PassionPointerWake::Binding binding_;
    std::uint64_t sequence_{};
    std::array<std::optional<PassionDisplayProof>, 2> states_;
};
} // namespace voyemu::media
