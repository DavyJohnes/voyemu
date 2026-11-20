#pragma once
#include "cli_messages.hpp"
#include "process.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
namespace voyemu {
// A request file in the private session directory asks its supervisor to stop its guests; the same
// path works on every platform, before the supervisor publishes its control endpoint.
inline std::filesystem::path supervisor_stop_request(const std::filesystem::path &session) {
    return session / "supervisor-stop";
}

inline bool supervisor_stop_requested(const std::filesystem::path &session) {
    return std::filesystem::exists(supervisor_stop_request(session));
}

// Asks the supervisor to stop its guests, waits up to grace for it to exit and only then kills it;
// PROCESS_STOP when it still runs, because it may hold the machine.
inline void stop_starting_supervisor(ChildProcess &child, const std::filesystem::path &session,
                                     std::chrono::milliseconds grace) {
    if (std::ofstream(supervisor_stop_request(session))) {
        const auto deadline = std::chrono::steady_clock::now() + grace;

        while (child.alive() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    try {
        child.terminate(1000);
    } catch (const VoyemuError &) { /* Ignored: a refused termination leaves the process alive,
                                       which the check below reports. */
    }

    if (child.alive()) {
        throw cli::LocalizedError(
                "PROCESS_STOP", "error.start_not_stopped",
                {{"log", std::filesystem::absolute(session / "supervisor.log").string()}});
    }
}
} // namespace voyemu
