#include "os/durable_disk.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <array>

namespace voyemu::os {
namespace {
bool owned(HANDLE file) {
    HANDLE token = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    alignas(TOKEN_USER) std::array<unsigned char, 256> user{};
    DWORD bytes = 0;
    const bool found = GetTokenInformation(token, TokenUser, user.data(),
                                           static_cast<DWORD>(user.size()), &bytes) != 0;
    CloseHandle(token);

    if (!found) {
        return false;
    }

    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto status = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner,
                                        nullptr, nullptr, nullptr, &descriptor);
    const auto sid = reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
    const bool valid = status == ERROR_SUCCESS && owner && IsValidSid(owner) && IsValidSid(sid) &&
                       EqualSid(owner, sid);

    if (descriptor) {
        LocalFree(descriptor);
    }

    return valid;
}

// The identity of an ordinary single-link writable disk file of the current user.
bool information(HANDLE file, BY_HANDLE_FILE_INFORMATION &value) {
    return file != INVALID_HANDLE_VALUE && GetFileType(file) == FILE_TYPE_DISK &&
           GetFileInformationByHandle(file, &value) && value.nNumberOfLinks == 1 &&
           !(value.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)) &&
           owned(file);
}

std::uint64_t join(DWORD high, DWORD low) {
    return std::uint64_t(high) << 32 | low;
}

bool matches(const BY_HANDLE_FILE_INFORMATION &info, std::uint64_t volume, std::uint64_t index,
             std::uint64_t size, std::uint64_t created) {
    return info.dwVolumeSerialNumber == volume &&
           join(info.nFileIndexHigh, info.nFileIndexLow) == index &&
           join(info.nFileSizeHigh, info.nFileSizeLow) == size &&
           join(info.ftCreationTime.dwHighDateTime, info.ftCreationTime.dwLowDateTime) == created;
}
} // namespace

DurableDisk::~DurableDisk() {
    close();
}

void DurableDisk::close() noexcept {
    const auto handle = reinterpret_cast<HANDLE>(handle_);

    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle_ = -1;
    }
}

bool DurableDisk::open(const std::filesystem::path &path, std::uint64_t bytes) {
    path_ = path;
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                                      nullptr);
    handle_ = reinterpret_cast<std::intptr_t>(handle);
    BY_HANDLE_FILE_INFORMATION info{};

    if (!information(handle, info) || join(info.nFileSizeHigh, info.nFileSizeLow) != bytes) {
        return false;
    }

    volume_ = info.dwVolumeSerialNumber;
    index_ = join(info.nFileIndexHigh, info.nFileIndexLow);
    size_ = bytes;
    created_ = join(info.ftCreationTime.dwHighDateTime, info.ftCreationTime.dwLowDateTime);

    return true;
}

bool DurableDisk::same_file(const DurableDisk &other) const {
    return volume_ == other.volume_ && index_ == other.index_;
}

bool DurableDisk::current() const {
    BY_HANDLE_FILE_INFORMATION retained{}, named{};

    if (!information(reinterpret_cast<HANDLE>(handle_), retained) ||
        !matches(retained, volume_, index_, size_, created_)) {
        return false;
    }

    const HANDLE file = CreateFileW(path_.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool valid = information(file, named) && matches(named, volume_, index_, size_, created_);

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }

    return valid;
}

bool DurableDisk::flush() {
    return FlushFileBuffers(reinterpret_cast<HANDLE>(handle_)) != 0;
}
} // namespace voyemu::os
