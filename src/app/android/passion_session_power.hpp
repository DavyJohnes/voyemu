#pragma once
#include "error.hpp"
#include "passion_display_cache.hpp"
namespace voyemu::media {
// Owned Session holds the process/boot fence and supplies only fixed-token callbacks.
// All methods run on the Session owner/input dispatcher, never the renderer.
class PassionSessionPower {
public:
    using Fence = std::function<void()>;
    using Wake = std::function<PassionDisplayProof(unsigned, const std::string &)>;
    using Recheck = std::function<void()>;
    using Clock = std::function<std::uint64_t()>;

    PassionSessionPower(PassionPointerWake::Binding binding, Fence fence, Wake wake,
                        Recheck recheck, Clock clock)
        : binding_(binding), cache_(binding), fence_(std::move(fence)), wake_(std::move(wake)),
          recheck_(std::move(recheck)), clock_(std::move(clock)) {}

    bool event(const Json &value, std::uint64_t now_us, std::int64_t guest_ms) {
        checkedFence();

        return cache_.accept(value, now_us, guest_ms);
    }

    std::optional<PassionDisplayProof> cached(unsigned display, std::uint64_t now_us) const {
        return cache_.get(display, now_us);
    }

    void lost() {
        cache_.invalidate();
    }

    // Closed Session op. The normal ON fast path calls cached(), not this method.
    PassionDisplayProof wake(unsigned display, const std::string &session, const std::string &boot,
                             std::uint64_t now_us) {
        if (!binding_.enabled || session != binding_.session || boot != binding_.boot ||
            display > 1) {
            throw VoyemuError("OEM_DISPLAY_POWER", "closed operation binding mismatch");
        }

        checkedFence();
        auto before = cache_.get(display, now_us);

        if (!before || before->state != 1) {
            recheck_();
            throw VoyemuError("OEM_DISPLAY_POWER", "fresh OFF observation required");
        }

        auto proof = wake_(display, binding_.boot);
        checkedFence();

        if (proof.request <= before->request || proof.transaction <= before->transaction ||
            !cache_.confirmWake(proof, clock_())) {
            throw VoyemuError("OEM_DISPLAY_POWER", "original wake readback rejected");
        }

        return proof;
    }

    // API input is one complete trajectory; consume the whole gesture after a wake.
    std::optional<PassionDisplayProof> gesture(unsigned display, std::uint64_t now_us) {
        if (!binding_.enabled) {
            return {};
        }

        auto before = cache_.get(display, now_us);

        if (!before) {
            recheck_();
            throw VoyemuError("OEM_DISPLAY_POWER", "asynchronous state recheck queued");
        }

        if (before->state == 2) {
            return {};
        }

        return wake(display, binding_.session, binding_.boot, now_us);
    }

    Json record(std::uint64_t now_us) const {
        return cache_.record(now_us);
    }

    Json diagnostic() const {
        return cache_.diagnostic();
    }

private:
    void checkedFence() {
        try {
            fence_();
        } catch (...) {
            cache_.invalidate();
            throw;
        }
    }

    PassionPointerWake::Binding binding_;
    PassionDisplayCache cache_;
    Fence fence_;
    Wake wake_;
    Recheck recheck_;
    Clock clock_;
};
} // namespace voyemu::media
