#pragma once
#include "frame_transport.hpp"
#include "model.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
namespace voyemu::api {
// QNX cluster lifecycle and observations the public API dispatches. Each call receives the
// validated public request; replies are supervisor receipts or samples and failures are
// "CODE: message" exceptions.
class GuestQnx {
public:
    virtual ~GuestQnx() = default;
    virtual bool lifecycle_supported(const Json &request) = 0;
    virtual Json power(const Json &request, bool enabled) = 0;
    virtual Json restart(const Json &request) = 0;
    // One Gauge scene sample: generation, status and, when sampled, identity and nodes.
    virtual Json ui(const Json &request, int timeout_ms) = 0;
    // One Gauge process diagnostic sample: generation, status and, when sampled, identity.
    virtual Json system(const Json &request, int timeout_ms) = 0;
    // Exported frame newer than after_sequence; nullopt when none arrives before the timeout.
    virtual std::optional<frames::Frame> frame(const std::filesystem::path &session_directory,
                                               std::uint64_t after_sequence, int timeout_ms) = 0;
};
} // namespace voyemu::api
