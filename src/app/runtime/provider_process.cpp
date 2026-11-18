#include "provider_process.hpp"
#include "error.hpp"
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <cerrno>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace voyemu::runtime {
ProcessResult run_provider(const ProcessSpec &specification, int timeout_ms) {
    if (timeout_ms < 1 || timeout_ms > 10000 || specification.detached) {
        throw VoyemuError("PROVIDER_RUN_BOUNDS", "provider run outside its bounds");
    }

#ifdef _WIN32
    // ChildProcess owns a non-inheritable kill-on-close job; observing leader exit
    // closes that job and terminates any trusted worker descendants before return.
    auto child = ChildProcess::launch(specification);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (child.alive()) {
        const auto left = deadline - std::chrono::steady_clock::now();

        if (left <= decltype(left)::zero()) {
            child.terminate(0);

            return {-1, {}, true};
        }

        std::this_thread::sleep_for(
                std::min(left, std::chrono::steady_clock::duration(std::chrono::milliseconds(5))));
    }

    return {child.exit_code(), {}, false};
#else
    auto child = ChildProcess::launch(specification);
    const auto pid = child.pid();
    struct Cleanup {
        ChildProcess &child;
        std::int64_t pid;
        bool active = true;

        ~Cleanup() {
            if (active) {
                ::kill(-static_cast<pid_t>(pid), SIGKILL);
            }

            try {
                child.terminate(0);
            } catch (...) { /* Ignored: termination is best-effort here. */
            }
        }
    } cleanup{child, pid};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        siginfo_t exited{};

        if (::waitid(P_PID, static_cast<id_t>(pid), &exited, WEXITED | WNOHANG | WNOWAIT) != 0) {
            if (errno == EINTR) {
                continue;
            }

            throw VoyemuError("PROVIDER_WAIT", "provider wait failed");
        }

        if (exited.si_pid) {
            // Keep the leader unreaped until its group has been killed: its PID cannot be reused.
            ::kill(-static_cast<pid_t>(pid), SIGKILL);
            cleanup.active = false;
            child.alive();

            return {child.exit_code(), {}, false};
        }

        const auto left = deadline - std::chrono::steady_clock::now();

        if (left <= decltype(left)::zero()) {
            return {-1, {}, true};
        }

        std::this_thread::sleep_for(
                std::min(left, std::chrono::steady_clock::duration(std::chrono::milliseconds(5))));
    }
#endif
}

SourceSample provider_sample(const Json &observation) {
    SourceSample result;

    result.value = observation;
    result.status = observation.value("status", std::string("unavailable"));

    // Absence is a completed observation, not a failed transport.
    if (result.status == "absent") {
        result.status = "ready";
    }

    result.instance = observation.value("source_instance", std::string{});
    result.boot_id = observation.value("boot_id", std::string{});
    result.revision = observation.value("revision", std::string{});
    result.continuous = false;

    return result;
}
} // namespace voyemu::runtime
