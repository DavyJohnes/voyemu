#pragma once
#include "media_wire.hpp"
#include "model.hpp"
#include <filesystem>
#include <string>
namespace voyemu::api {
// Android multimedia lifecycle and observations the public API dispatches. Each call receives the
// validated public request; replies are supervisor receipts or observations and failures are
// "CODE: message" exceptions.
class GuestAndroid {
public:
    virtual ~GuestAndroid() = default;
    virtual Json reboot(const Json &request, const std::string &mode, int timeout_ms) = 0;
    virtual Json power(const Json &request, bool enabled, int timeout_ms) = 0;
    // app holds package, screen, boot_id and an optional component.
    virtual Json launch(const Json &request, const Json &app) = 0;
    // UI observer owner: session_id, android_pid and connection; recover replaces a lost transport.
    virtual Json ui_observer(const Json &request, bool recover, int timeout_ms) = 0;
    // One observer message exchange; transport failures are TREE_TIMEOUT_UNKNOWN or
    // TREE_TRANSPORT_UNKNOWN.
    virtual Json ui_exchange(const Json &request, const Json &connection, const Json &message,
                             int timeout_ms) = 0;
    // Current boot_id and android_pid.
    virtual Json boot_identity(const Json &request, int timeout_ms) = 0;
    // Latest SDK frame of an Android screen at its size.
    virtual media::Image frame(const std::filesystem::path &session_directory,
                               const std::string &session_id, media::GuestDisplay display,
                               int timeout_ms) = 0;
    // Multimedia system observation reply carrying state.
    virtual Json system(const Json &request, int timeout_ms) = 0;
    // Synthetic phone command (method, payload); "snapshot" reads state, other methods mutate.
    virtual Json phone(const Json &request, const Json &command) = 0;
    // Diagnostic logs reply; query holds source, timeout_ms and source-specific fields.
    virtual Json logs(const Json &request, const Json &query, int timeout_ms) = 0;
};
} // namespace voyemu::api
