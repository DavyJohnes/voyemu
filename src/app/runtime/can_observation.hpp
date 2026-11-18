#pragma once
#include "model.hpp"
#include "process.hpp"
#include <functional>
namespace voyemu {
// Fixed Android provider: content://com.voyemu.output.vehicle, call method observe.
// Request contains only schema/states/cursor/limit, never the public routing envelope.
std::string can_transport_diagnostic(const ProcessResult &result, const std::string &secret = {});
void validate_can_request(const Json &request);
Json parse_can_observation(const Json &observation);
struct CanGuestIdentity {
    std::string session_id, avd_name, cached_boot_nonce;
    bool resumed = false;
};
using CanGuestRunner = std::function<ProcessResult(const std::vector<std::string> &, int)>;
// Hosting Session fences its owned emulator PID/listeners around this read.
// Runner receives fixed SDK arguments only; no caller-provided command or shell endpoint.
Json collect_can_observation(const Json &query, const CanGuestIdentity &identity, int timeout_ms,
                             const CanGuestRunner &runner);
// Same provider, call method ipk-history: the IPK frames the CanBus component sent and received.
// Request contains only schema/cursor/limit; events carry no frame text.
void validate_ipk_request(const Json &request);
Json parse_ipk_history(const Json &history);
Json collect_ipk_history(const Json &query, const CanGuestIdentity &identity, int timeout_ms,
                         const CanGuestRunner &runner);
} // namespace voyemu
