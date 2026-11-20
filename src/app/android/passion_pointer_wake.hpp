#pragma once
#include "error.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
namespace voyemu::media {
struct PassionDisplayProof {
    std::string session, boot, epoch, unique, kind;
    unsigned display{};
    int state{};
    std::uint64_t request{}, transaction{}, expires_us{}, order{};
    std::int64_t observed_ms{};
};
class PassionPointerWake {
public:
    enum class Phase {
        down,
        move,
        up
    };
    enum class Result {
        pass,
        consumed
    };
    struct Binding {
        bool enabled{};
        std::string session, boot, epoch;
    };
    using Cache = std::function<std::optional<PassionDisplayProof>(unsigned)>;
    using Wake = std::function<PassionDisplayProof(unsigned)>;
    using Recheck = std::function<void()>;
    using Clock = std::function<std::uint64_t()>;

    PassionPointerWake(Binding binding, Cache cache, Wake wake, Recheck recheck, Clock now)
        : binding_(std::move(binding)), cache_(std::move(cache)), wake_(std::move(wake)),
          recheck_(std::move(recheck)), now_(std::move(now)) {
        if (binding_.enabled &&
            (binding_.session.empty() || binding_.boot.empty() || binding_.epoch.empty() ||
             !cache_ || !wake_ || !recheck_ || !now_)) {
            throw VoyemuError("OEM_DISPLAY_POWER", "exact prepared binding required");
        }
    }

    Result before(Phase phase, unsigned display) {
        if (!binding_.enabled) {
            return Result::pass;
        }

        if (display > 1) {
            throw VoyemuError("OEM_DISPLAY_POWER", "unsupported display");
        }

        if (suppressed_) {
            if (*suppressed_ != display) {
                throw VoyemuError("OEM_DISPLAY_POWER", "wake gesture crossed displays");
            }

            if (phase == Phase::up) {
                suppressed_.reset();
            }

            return Result::consumed;
        }

        if (phase != Phase::down) {
            return Result::pass;
        }

        suppressed_ = display;
        auto observed = cache_(display);

        if (!observed || !valid(*observed, display, false)) {
            recheck_();
            throw VoyemuError("OEM_DISPLAY_POWER",
                              "sealed state unknown; asynchronous owned recheck queued");
        }

        // No ADB, IPC or wait on the ordinary known-ON path.
        if (observed->state == 2) {
            suppressed_.reset();

            return Result::pass;
        }

        auto awake = wake_(display);

        if (!valid(awake, display, true) || awake.transaction <= observed->transaction ||
            awake.request <= observed->request) {
            throw VoyemuError("OEM_DISPLAY_POWER", "original wake not observed");
        }

        last_ = std::move(awake);

        return Result::consumed;
    }

    const std::optional<PassionDisplayProof> &last_wake() const {
        return last_;
    }

private:
    bool valid(const PassionDisplayProof &p, unsigned display, bool wake) const {
        const std::string expected = display == 0
                                             ? "local:8141241408256768"
                                             : "virtual:com.android.emulator.multidisplay:1234562";

        return p.session == binding_.session && p.boot == binding_.boot &&
               p.epoch == binding_.epoch && p.display == display && p.unique == expected &&
               p.expires_us > now_() && p.order > 0 && p.observed_ms > 0 &&
               (p.state == 1 || p.state == 2) &&
               (!wake ? (p.kind == "observed" || p.kind == "applied")
                      : (p.kind == "wake_observed" && p.state == 2 && p.request && p.transaction));
    }

    Binding binding_;
    Cache cache_;
    Wake wake_;
    Recheck recheck_;
    Clock now_;
    std::optional<unsigned> suppressed_;
    std::optional<PassionDisplayProof> last_;
};
} // namespace voyemu::media
