#include "provider_reaper.hpp"

namespace voyemu::runtime {
// ChildProcess owns a non-inheritable kill-on-close job; observing the leader exit closes that job
// and terminates any trusted worker descendants.
ProviderReaper::ProviderReaper(ChildProcess &child) : child_(child), pid_(child.pid()) {}

ProviderReaper::~ProviderReaper() {
    if (active_) {
        try {
            child_.terminate(0);
        } catch (...) { /* Ignored: the job object still ends the child. */
        }
    }
}

bool ProviderReaper::exited() {
    return !child_.alive();
}

void ProviderReaper::finish() {
    active_ = false;
}
} // namespace voyemu::runtime
