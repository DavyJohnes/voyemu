#include "adb_auth.h"
#include "transport.h"

// Owned emulator guests run with ro.adb.secure=0, so the host holds no ADB key. A peer that
// requests AUTH stays unauthorized.
std::deque<std::shared_ptr<RSA>> adb_auth_get_private_keys() {
    return {nullptr};
}

std::string adb_auth_get_userkey() {
    return {};
}

void send_auth_response(const char *, size_t, atransport *transport) {
    transport->SetConnectionState(kCsUnauthorized);
    transport->SetConnectionEstablished(true);
}
