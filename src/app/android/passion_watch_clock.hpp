#pragma once
#include "error.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace voyemu::media {
// One sameboot/watch binding owns this projection. No host wall-clock arithmetic.
class PassionWatchClock {
public:
    void launched(std::uint64_t now) {
        launch_ = now;
        launched_ = true;
    }

    void reset() {
        valid_ = false;
        launched_ = false;
    }

    std::int64_t event(std::int64_t guest, std::uint64_t receipt, std::int64_t verified_utc) {
        if (guest <= 0 || guest > std::numeric_limits<std::int64_t>::max() - 1000000 ||
            !launched_) {
            throw VoyemuError("PASSION_WATCH", "Watch clock envelope absent");
        }

        if (!valid_) {
            if (verified_utc <= 0 ||
                verified_utc > std::numeric_limits<std::int64_t>::max() - 1000000 ||
                guest < verified_utc - 2000 || guest > verified_utc + 2000) {
                throw VoyemuError("PASSION_WATCH", "Watch clock coarse epoch mismatch");
            }

            if (receipt < launch_ || receipt - launch_ > 1500000) {
                throw VoyemuError("PASSION_WATCH", "Watch clock first frame late");
            }

            base_guest_ = guest;
            base_host_ = receipt;
            uncertainty_ = (receipt - launch_ + 999) / 1000;
            valid_ = true;
            last_guest_ = guest;
            last_receipt_ = receipt;
        }

        if (receipt < last_receipt_ || receipt < base_host_ ||
            receipt - last_receipt_ > 120000000) {
            throw VoyemuError("PASSION_WATCH", "Watch clock anchor expired");
        }

        const auto elapsed = static_cast<std::int64_t>((receipt - base_host_) / 1000);

        if (elapsed > std::numeric_limits<std::int64_t>::max() - base_guest_ -
                              static_cast<std::int64_t>(uncertainty_)) {
            throw VoyemuError("PASSION_WATCH", "Watch clock projection overflow");
        }

        const auto projected = base_guest_ + elapsed;

        if (guest < last_guest_ || guest < projected - 1500 ||
            guest > projected + static_cast<std::int64_t>(uncertainty_) + 250) {
            throw VoyemuError("PASSION_WATCH", "Watch clock envelope discontinuity");
        }

        // Renew stream liveness only after validation; retain the original projection to prevent
        // drift.
        last_guest_ = guest;
        last_receipt_ = receipt;

        return projected + static_cast<std::int64_t>(uncertainty_);
    }

    std::int64_t upper(std::uint64_t now) const {
        if (!valid_ || now < last_receipt_ || now < base_host_ || now - last_receipt_ > 120000000) {
            throw VoyemuError("PASSION_WATCH", "Watch clock projection unavailable");
        }

        const auto elapsed = static_cast<std::int64_t>((now - base_host_) / 1000);

        if (elapsed > std::numeric_limits<std::int64_t>::max() - base_guest_ -
                              static_cast<std::int64_t>(uncertainty_)) {
            throw VoyemuError("PASSION_WATCH", "Watch clock projection overflow");
        }

        return base_guest_ + elapsed + static_cast<std::int64_t>(uncertainty_);
    }

    std::uint64_t uncertainty() const {
        return uncertainty_;
    }

private:
    bool valid_{}, launched_{};
    std::uint64_t launch_{}, base_host_{}, last_receipt_{}, uncertainty_{};
    std::int64_t base_guest_{}, last_guest_{};
};
} // namespace voyemu::media
