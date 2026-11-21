#include "error.hpp"
#include "provider_reaper.hpp"
#include <cerrno>
#include <signal.h>
#include <sys/wait.h>

namespace voyemu::runtime {
ProviderReaper::ProviderReaper(ChildProcess &child) : child_(child), pid_(child.pid()) {}

ProviderReaper::~ProviderReaper() {
    if (active_) {
        ::kill(-static_cast<pid_t>(pid_), SIGKILL);
    }

    try {
        child_.terminate(0);
    } catch (...) { /* Ignored: termination is best-effort here. */
    }
}

bool ProviderReaper::exited() {
    for (;;) {
        siginfo_t exited{};

        if (::waitid(P_PID, static_cast<id_t>(pid_), &exited, WEXITED | WNOHANG | WNOWAIT) == 0) {
            return exited.si_pid != 0;
        }

        if (errno != EINTR) {
            throw VoyemuError("PROVIDER_WAIT", "provider wait failed");
        }
    }
}

void ProviderReaper::finish() {
    // Keep the leader unreaped until its group has been killed: its PID cannot be reused.
    ::kill(-static_cast<pid_t>(pid_), SIGKILL);
    active_ = false;
    child_.alive();
}
} // namespace voyemu::runtime
