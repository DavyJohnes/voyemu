#include "media_rpc_test_host.hpp"
#include "os/current_user_windows.hpp"
#include <windows.h>
#include <aclapi.h>
#include <cassert>

// Wine may report successful owner assignment without changing the owner SID.
bool assign_world_owner(const std::filesystem::path &path) {
    HANDLE file = CreateFileW(path.c_str(), WRITE_OWNER | READ_CONTROL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    bool foreign = false;

    if (file != INVALID_HANDLE_VALUE) {
        BYTE sid[SECURITY_MAX_SID_SIZE]{};
        DWORD size = sizeof(sid);
        assert(CreateWellKnownSid(WinWorldSid, nullptr, sid, &size));
        const auto changed = SetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, sid,
                                             nullptr, nullptr, nullptr);
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        const auto observed = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                              &owner, nullptr, nullptr, nullptr, &security);
        foreign = changed == ERROR_SUCCESS && observed == ERROR_SUCCESS && owner &&
                  EqualSid(owner, sid);

        if (security) {
            LocalFree(security);
        }

        CloseHandle(file);
    }

    return foreign;
}

bool private_frame_file(const std::filesystem::path &path, std::size_t bytes) {
    if (std::filesystem::file_size(path) != bytes) {
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), READ_CONTROL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const bool owned = voyemu::os::owned_by_current_user(file);
    CloseHandle(file);

    return owned;
}
