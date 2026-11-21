#include "../platform/wine_native_backend_windows.hpp"
#include "deps_metadata.hpp"
#include "error.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <cstddef>
namespace voyemu {
// Windows analogue of the POSIX owner + no-group/world-write refusal, following the admission.cpp
// DACL walk: the owner is the current user and no ACE grants a mutation right to a foreign SID.
void verify_owner_controlled_metadata(const std::filesystem::path &file) {
    HANDLE handle = CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    struct Close {
        HANDLE value;

        ~Close() {
            if (value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    } close{handle};
    BY_HANDLE_FILE_INFORMATION entry{};

    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &entry) ||
        (entry.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;

    if (GetSecurityInfo(handle, SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &acl, nullptr, &descriptor) != ERROR_SUCCESS) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    struct Release {
        PSECURITY_DESCRIPTOR value;

        ~Release() {
            if (value) {
                LocalFree(value);
            }
        }
    } release{descriptor};

    if (!owner || !acl) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    HANDLE token = INVALID_HANDLE_VALUE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    struct CloseToken {
        HANDLE value;

        ~CloseToken() {
            if (value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    } close_token{token};
    std::byte user[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_USER)];
    DWORD bytes = sizeof(user);

    if (!GetTokenInformation(token, TokenUser, user, bytes, &bytes)) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    const PSID current = reinterpret_cast<TOKEN_USER *>(user)->User.Sid;
    BYTE system[SECURITY_MAX_SID_SIZE]{}, administrators[SECURITY_MAX_SID_SIZE]{};
    DWORD system_size = sizeof(system), administrators_size = sizeof(administrators);

    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &system_size) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators,
                            &administrators_size)) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    // Wine reports the anonymous logon owner for every node of the DOS volume mapped to the Unix
    // root; accept that owner only on a Wine host so the DACL guard below stays live there.
    BYTE anonymous[SECURITY_MAX_SID_SIZE]{};
    DWORD anonymous_size = sizeof(anonymous);
    const bool wine = wine_native_host() &&
                      CreateWellKnownSid(WinAnonymousSid, nullptr, anonymous, &anonymous_size);
    auto trusted = [&](PSID sid) {
        return EqualSid(sid, current) || EqualSid(sid, system) || EqualSid(sid, administrators) ||
               (wine && EqualSid(sid, anonymous));
    };

    if (!trusted(owner)) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }

    const DWORD mutation = GENERIC_ALL | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER | DELETE |
                           FILE_DELETE_CHILD | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                           FILE_WRITE_ATTRIBUTES;

    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *raw = nullptr;

        if (!GetAce(acl, i, &raw)) {
            throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
        }

        const auto *header = static_cast<const ACE_HEADER *>(raw);

        if (header->AceFlags & INHERIT_ONLY_ACE) {
            continue;
        }

        if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
            continue;
        }

        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
        }

        const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw);

        if ((ace->Mask & mutation) && !trusted(const_cast<DWORD *>(&ace->SidStart))) {
            throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
        }
    }
}
} // namespace voyemu
