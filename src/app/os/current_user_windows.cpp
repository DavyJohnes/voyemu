#include "os/current_user_windows.hpp"
#include <aclapi.h>

namespace voyemu::os {
CurrentUser::Reading CurrentUser::read() {
    HANDLE token = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return Reading::token_unavailable;
    }

    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);

    if (!bytes || bytes > 65536) {
        CloseHandle(token);

        return Reading::extent_differs;
    }

    token_.resize(bytes);
    const bool loaded = GetTokenInformation(token, TokenUser, token_.data(), bytes, &bytes) != 0;
    CloseHandle(token);

    return loaded ? Reading::read : Reading::identity_unavailable;
}

PSID CurrentUser::sid() const {
    return reinterpret_cast<const TOKEN_USER *>(token_.data())->User.Sid;
}

bool TrustedAccounts::create() {
    DWORD system_size = sizeof system, administrators_size = sizeof administrators;

    return CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &system_size) &&
           CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators,
                              &administrators_size);
}

bool TrustedAccounts::contains(PSID sid) const {
    return EqualSid(sid, const_cast<BYTE *>(system)) ||
           EqualSid(sid, const_cast<BYTE *>(administrators));
}

bool owned_by(HANDLE file, PSID user) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto status = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner,
                                        nullptr, nullptr, nullptr, &descriptor);
    const bool same = status == ERROR_SUCCESS && owner && IsValidSid(owner) && IsValidSid(user) &&
                      EqualSid(owner, user);

    if (descriptor) {
        LocalFree(descriptor);
    }

    return same;
}

bool owned_by_current_user(HANDLE file) {
    CurrentUser user;

    return user.read() == CurrentUser::Reading::read && owned_by(file, user.sid());
}
} // namespace voyemu::os
