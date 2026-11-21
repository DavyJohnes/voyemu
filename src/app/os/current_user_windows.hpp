#pragma once
// The token user of this process on Windows: its SID, the accounts trusted beside it and the
// ownership of a file by it. Windows sources only.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>

namespace voyemu::os {
class CurrentUser {
public:
    enum class Reading {
        read,
        token_unavailable,
        extent_differs, // an empty TOKEN_USER or one above 64 KiB
        identity_unavailable
    };
    // Reads the TOKEN_USER of this process.
    Reading read();
    // The SID of the user; valid after a successful read.
    PSID sid() const;

private:
    std::vector<unsigned char> token_;
};

// SYSTEM and Administrators, the accounts a private object may grant beside the current user.
struct TrustedAccounts {
    BYTE system[SECURITY_MAX_SID_SIZE]{}, administrators[SECURITY_MAX_SID_SIZE]{};

    // Creates both SIDs; false when the host cannot.
    bool create();
    bool contains(PSID sid) const;
};

// Whether the owner of file is the valid SID user.
bool owned_by(HANDLE file, PSID user);
// Whether the owner of file is the current user; false when either cannot be read.
bool owned_by_current_user(HANDLE file);
} // namespace voyemu::os
