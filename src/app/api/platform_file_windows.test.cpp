#include "os/private_state_security.hpp"
#include "platform_file_test_host.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <cassert>
#include <iostream>
#include <sddl.h>
using namespace voyemu;

void metadata(const std::filesystem::path &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    std::cout << "METADATA " << path.string();

    if (file == INVALID_HANDLE_VALUE) {
        std::cout << " open_error=" << GetLastError() << std::endl;

        return;
    }

    BY_HANDLE_FILE_INFORMATION info{};

    if (GetFileInformationByHandle(file, &info)) {
        std::cout << " attributes=" << info.dwFileAttributes << " links=" << info.nNumberOfLinks
                  << " volume=" << info.dwVolumeSerialNumber << " index=" << info.nFileIndexHigh
                  << ":" << info.nFileIndexLow;
    }

    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto status = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner,
                                        nullptr, nullptr, nullptr, &descriptor);
    LPWSTR sid = nullptr;

    if (status == ERROR_SUCCESS && ConvertSidToStringSidW(owner, &sid)) {
        std::wstring value(sid);
        std::cout << " owner=" << std::string(value.begin(), value.end());
        LocalFree(sid);
    } else {
        std::cout << " owner_error=" << status;
    }

    if (descriptor) {
        LocalFree(descriptor);
    }

    CloseHandle(file);
    std::cout << std::endl;
}

void write(const std::filesystem::path &path, const std::string &value) {
    detail::PrivateStateSecurity security;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &security.attributes, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);

    assert(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    assert(WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) &&
           written == value.size());
    assert(CloseHandle(file));
}

bool change_owner(const std::filesystem::path &path) {
    BYTE sid[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(sid);

    assert(CreateWellKnownSid(WinWorldSid, nullptr, sid, &size));
    HANDLE file = CreateFileW(path.c_str(), WRITE_OWNER | READ_CONTROL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const auto result = SetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, sid,
                                        nullptr, nullptr, nullptr);
    PSID observed = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto readback = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                          &observed, nullptr, nullptr, nullptr, &descriptor);
    const bool changed = result == ERROR_SUCCESS && readback == ERROR_SUCCESS && observed &&
                         EqualSid(observed, sid);

    if (!changed) {
        std::cout << "OWNER_FIXTURE setter_status=" << result << " readback_status=" << readback
                  << " requested_sid_observed=false" << std::endl;
    }

    if (descriptor) {
        LocalFree(descriptor);
    }

    CloseHandle(file);

    return changed;
}

SupervisorLock::SupervisorLock(const std::filesystem::path &path)
    : handle_(reinterpret_cast<std::intptr_t>(
              CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr))) {
    assert(reinterpret_cast<HANDLE>(handle_) != INVALID_HANDLE_VALUE);
}

SupervisorLock::~SupervisorLock() {
    const bool closed = CloseHandle(reinterpret_cast<HANDLE>(handle_));

    assert(closed);
    (void)closed;
}
