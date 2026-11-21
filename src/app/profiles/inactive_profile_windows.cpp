#include "inactive_profile.hpp"
#include "os/fs.hpp"
#include "private_state_security.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <vector>

namespace voyemu::inactive_profile {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;

    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

// The SID of the current user in user, false when the token cannot be read.
bool current_user(std::vector<unsigned char> &user) {
    Handle token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
        return false;
    }

    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);

    if (!bytes || bytes > 65536) {
        return false;
    }

    user.resize(bytes);

    return GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes);
}

bool owned(PSID sid) {
    std::vector<unsigned char> user;

    return current_user(user) &&
           EqualSid(sid, reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);
}

bool trusted(PSID sid) {
    std::vector<unsigned char> user;

    if (!current_user(user)) {
        return false;
    }

    BYTE system[SECURITY_MAX_SID_SIZE]{};
    DWORD system_size = sizeof system;
    BYTE administrators[SECURITY_MAX_SID_SIZE]{};
    DWORD administrators_size = sizeof administrators;

    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &system_size) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators,
                            &administrators_size)) {
        return false;
    }

    return EqualSid(sid, reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid) ||
           EqualSid(sid, system) || EqualSid(sid, administrators);
}

// The DACL analogue of the POSIX 0077 check: only the current user, SYSTEM and Administrators
// may hold any access.
void object(HANDLE file, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};

    require(GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
            !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
            (directory || info.nNumberOfLinks == 1));
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(GetSecurityInfo(file, SE_FILE_OBJECT,
                            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                            &acl, nullptr, &descriptor) == ERROR_SUCCESS);
    struct Release {
        PSECURITY_DESCRIPTOR value;

        ~Release() {
            if (value) {
                LocalFree(value);
            }
        }
    } release{descriptor};
    require(owner && acl && owned(owner));

    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *raw = nullptr;
        require(GetAce(acl, i, &raw));
        const auto *header = static_cast<const ACE_HEADER *>(raw);

        if (header->AceFlags & INHERIT_ONLY_ACE) {
            continue;
        }

        if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
            continue;
        }

        require(header->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                trusted(const_cast<DWORD *>(
                        &static_cast<const ACCESS_ALLOWED_ACE *>(raw)->SidStart)));
    }
}

void components(const std::filesystem::path &path) {
    require(path.is_absolute() && path.lexically_normal() == path);

    for (const auto &at : os::ancestors(path)) {
        const auto attributes = GetFileAttributesW(at.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES &&
                !(attributes & FILE_ATTRIBUTE_REPARSE_POINT));
    }
}

std::uint64_t file_index(const BY_HANDLE_FILE_INFORMATION &info) {
    return (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
}
} // namespace

void ordinary(const std::filesystem::path &path, bool directory) {
    components(path);
    Handle handle;
    handle.value = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                                       (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0),
                               nullptr);
    require(handle.value != INVALID_HANDLE_VALUE);
    object(handle.value, directory);
}

bool same_directory(const Json &recorded, const std::filesystem::path &directory) {
    return same_directory_tail(std::filesystem::path(recorded.get<std::string>()), directory, 3);
}

void absent(const Json &pid) {
    require(pid.is_number_integer());
    const auto value = pid.get<std::int64_t>();
    require(value >= 0 && value <= UINT32_MAX);
    Handle process;
    process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(value));

    if (!process.value) {
        require(GetLastError() == ERROR_INVALID_PARAMETER);

        return;
    }

    require(WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0);
}

// Exclusive open is the Windows holder probe: success proves no foreign handle, sharing
// violation proves a holder.
void unused(const std::filesystem::path &path) {
    ordinary(path);
    Handle handle;
    handle.value = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                               nullptr);
    require(handle.value != INVALID_HANDLE_VALUE);
}

void endpoint_absent(const std::filesystem::path &endpoint, const std::string &id) {
    require(endpoint.string() == "\\\\.\\pipe\\voyemu-" + id);
    require(!WaitNamedPipeW(endpoint.c_str(), 1) && GetLastError() == ERROR_FILE_NOT_FOUND);
}

SupervisorLock::SupervisorLock(const std::filesystem::path &path) : path_(path) {
    components(path);
    Handle lock;
    lock.value = detail::open_private_state_lock(
            path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
    require(lock.value != INVALID_HANDLE_VALUE);
    BY_HANDLE_FILE_INFORMATION held{};
    require(GetFileType(lock.value) == FILE_TYPE_DISK &&
            GetFileInformationByHandle(lock.value, &held) && held.nNumberOfLinks == 1);
    volume_ = held.dwVolumeSerialNumber;
    index_ = file_index(held);
    object(lock.value, false);
    handle_ = reinterpret_cast<std::intptr_t>(lock.value);
    lock.value = INVALID_HANDLE_VALUE;
}

SupervisorLock::~SupervisorLock() {
    if (handle_ != -1) {
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
}

void SupervisorLock::verify() const {
    components(path_);
    const auto lock = reinterpret_cast<HANDLE>(handle_);
    BY_HANDLE_FILE_INFORMATION now{};
    require(GetFileType(lock) == FILE_TYPE_DISK && GetFileInformationByHandle(lock, &now) &&
            now.nNumberOfLinks == 1 && now.dwVolumeSerialNumber == volume_ &&
            file_index(now) == index_);
    object(lock, false);
}
} // namespace voyemu::inactive_profile
