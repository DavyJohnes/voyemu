#pragma once
#include "error.hpp"
// Windows security descriptor shared by the state layer and its lock sites.
// Mirrors api/admission.cpp PrivateSecurity: the object is created owned by the
// current user with a single protected ALLOW ACE for that user, so the
// InactiveProfileLease DACL predicate accepts it. Wine synthesises the DACL
// from the creating call and ignores later SetNamedSecurityInfoW, so the
// descriptor has to ride CreateDirectoryW/CreateFileW. Non-Windows builds see an
// empty header: no new dependency and no <windows.h> reaches the POSIX branches.
#ifdef _WIN32
#include "os/fs.hpp"
#include <windows.h>
#include <aclapi.h>

#include <filesystem>
#include <stdexcept>
#include <vector>
namespace voyemu::detail {
struct PrivateStateSecurity {
    std::vector<unsigned char> user;
    PACL acl = nullptr;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};

    PrivateStateSecurity() {
        HANDLE token = nullptr;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            throw VoyemuError("STATE_OWNER", "user identity unavailable");
        }

        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);

        if (!bytes || bytes > 65536) {
            CloseHandle(token);
            throw VoyemuError("STATE_OWNER", "user identity extent differs");
        }

        user.resize(bytes);
        const bool loaded = GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes);
        CloseHandle(token);

        if (!loaded) {
            throw VoyemuError("STATE_OWNER", "user identity unavailable");
        }

        PSID owner = reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;

        if (!IsValidSid(owner)) {
            throw VoyemuError("STATE_OWNER", "invalid user identity");
        }

        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(owner);

        if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) {
            throw VoyemuError("STATE_OWNER", "private DACL unavailable");
        }

        if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorOwner(&descriptor, owner, FALSE) ||
            !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
            !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
            LocalFree(acl);
            acl = nullptr;
            throw VoyemuError("STATE_OWNER", "private descriptor unavailable");
        }
    }

    // Existing Windows policy trusts the current user, SYSTEM and Administrators.
    // Artifacts may be readable by other principals, but never writable by them.
    bool accepts(HANDLE file, bool private_access) const {
        PSID owner = nullptr;
        PACL observed = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;

        if (GetSecurityInfo(file, SE_FILE_OBJECT,
                            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                            &observed, nullptr, &security) != ERROR_SUCCESS) {
            return false;
        }

        struct Release {
            PSECURITY_DESCRIPTOR value;

            ~Release() {
                if (value) {
                    LocalFree(value);
                }
            }
        } release{security};

        return accepts(owner, observed, private_access);
    }

    bool accepts(PSID owner, PACL observed, bool private_access) const {
        const auto current = reinterpret_cast<const TOKEN_USER *>(user.data())->User.Sid;

        if (!owner || !IsValidSid(owner) || !EqualSid(owner, current) || !observed ||
            !IsValidAcl(observed)) {
            return false;
        }

        BYTE system[SECURITY_MAX_SID_SIZE]{}, administrators[SECURITY_MAX_SID_SIZE]{};
        DWORD system_size = sizeof system, administrators_size = sizeof administrators;

        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &system_size) ||
            !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators,
                                &administrators_size)) {
            return false;
        }

        constexpr DWORD read_access = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE | GENERIC_READ |
                                      GENERIC_EXECUTE;

        for (DWORD index = 0; index < observed->AceCount; ++index) {
            void *raw = nullptr;

            if (!GetAce(observed, index, &raw)) {
                return false;
            }

            const auto header = static_cast<const ACE_HEADER *>(raw);

            if (header->AceFlags & INHERIT_ONLY_ACE) {
                continue;
            }

            if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
                continue;
            }

            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
                return false;
            }

            const auto ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw);
            auto sid = const_cast<DWORD *>(&ace->SidStart);

            if (!IsValidSid(sid)) {
                return false;
            }

            if (EqualSid(sid, current) || EqualSid(sid, system) || EqualSid(sid, administrators)) {
                continue;
            }

            if (private_access ? ace->Mask != 0 : (ace->Mask & ~read_access) != 0) {
                return false;
            }
        }

        return true;
    }

    PrivateStateSecurity(const PrivateStateSecurity &) = delete;
    PrivateStateSecurity &operator=(const PrivateStateSecurity &) = delete;

    ~PrivateStateSecurity() {
        if (acl) {
            LocalFree(acl);
        }
    }
};

// Every creator of the lease lock files (the CLI, the API and the portable
// runtime leases) opens them the same way: exclusive sharing, and the private
// descriptor above so the object passes the InactiveProfileLease DACL predicate
// no matter which creator ran first. The byte-range lock also reaches the Unix
// filesystem under Wine, where macOS Voyemu can use the same machine directory.
inline HANDLE open_private_state_lock(const std::filesystem::path &path, DWORD access,
                                      DWORD disposition, DWORD flags) {
    PrivateStateSecurity security;
    const HANDLE file = CreateFileW(os::win32_file_path(path).c_str(), access, 0,
                                    &security.attributes, disposition, flags, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return file;
    }

    OVERLAPPED range{};

    if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)) {
        return file;
    }

    const DWORD error = GetLastError();
    CloseHandle(file);
    SetLastError(error);

    return INVALID_HANDLE_VALUE;
}
} // namespace voyemu::detail
#endif
