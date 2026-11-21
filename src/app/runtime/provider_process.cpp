#include "provider_process.hpp"
#include "error.hpp"
#include "provider_reaper.hpp"
#include <stdexcept>
#include <thread>
namespace voyemu::runtime {
ProcessResult run_provider(const ProcessSpec &specification, int timeout_ms) {
    if (timeout_ms < 1 || timeout_ms > 10000 || specification.detached) {
        throw VoyemuError("PROVIDER_RUN_BOUNDS", "provider run outside its bounds");
    }

    auto child = ChildProcess::launch(specification);
    ProviderReaper reaper(child);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        if (reaper.exited()) {
            reaper.finish();

            return {child.exit_code(), {}, false};
        }

        const auto left = deadline - std::chrono::steady_clock::now();

        if (left <= decltype(left)::zero()) {
            return {-1, {}, true};
        }

        std::this_thread::sleep_for(
                std::min(left, std::chrono::steady_clock::duration(std::chrono::milliseconds(5))));
    }
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
