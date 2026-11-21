#include "os/current_user_windows.hpp"
#include "os/private_region.hpp"
#include "os/private_state_security.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <cstddef>
#include <vector>

namespace voyemu::os {
namespace {
// The file is an ordinary single-link, non-reparse disk file of exactly bytes owned by the user.
bool owned_private_region(HANDLE file, std::size_t bytes) {
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};

    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        info.nNumberOfLinks != 1 || !GetFileSizeEx(file, &size) ||
        size.QuadPart != static_cast<LONGLONG>(bytes)) {
        return false;
    }

    return owned_by_current_user(file);
}

HANDLE native(std::intptr_t handle) {
    return reinterpret_cast<HANDLE>(handle);
}
} // namespace

PrivateRegion::~PrivateRegion() {
    const bool owned = native(file_) != INVALID_HANDLE_VALUE && current();

    release();

    if (owned) {
        DeleteFileW(path_.c_str());
    }
}

void PrivateRegion::release() noexcept {
    if (data_) {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }

    if (mapping_) {
        CloseHandle(native(mapping_));
        mapping_ = 0;
    }

    if (native(file_) != INVALID_HANDLE_VALUE) {
        CloseHandle(native(file_));
        file_ = -1;
    }
}

PrivateRegion::Creation PrivateRegion::create(const std::filesystem::path &path,
                                              std::size_t bytes) {
    path_ = path;
    bytes_ = bytes;
    voyemu::detail::PrivateStateSecurity security;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    &security.attributes, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return Creation::create_failed;
    }

    file_ = reinterpret_cast<std::intptr_t>(file);
    auto failed = [&](Creation creation) {
        release();
        DeleteFileW(path.c_str());

        return creation;
    };
    LARGE_INTEGER extent{};
    extent.QuadPart = static_cast<LONGLONG>(bytes);

    if (!SetFilePointerEx(file, extent, nullptr, FILE_BEGIN) || !SetEndOfFile(file)) {
        return failed(Creation::extent_failed);
    }

    BY_HANDLE_FILE_INFORMATION info{};

    if (!owned_private_region(file, bytes) || !GetFileInformationByHandle(file, &info)) {
        return failed(Creation::ownership_failed);
    }

    volume_ = info.dwVolumeSerialNumber;
    index_ = std::uint64_t(info.nFileIndexHigh) << 32 | info.nFileIndexLow;
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    mapping_ = reinterpret_cast<std::intptr_t>(mapping);
    data_ = mapping ? MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes) : nullptr;

    if (!data_) {
        return failed(Creation::mapping_failed);
    }

    return Creation::created;
}

bool PrivateRegion::current() const {
    const HANDLE named = CreateFileW(path_.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (named == INVALID_HANDLE_VALUE) {
        return false;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    const bool same = GetFileInformationByHandle(named, &info) &&
                      info.dwVolumeSerialNumber == volume_ &&
                      (std::uint64_t(info.nFileIndexHigh) << 32 | info.nFileIndexLow) == index_ &&
                      owned_private_region(named, bytes_);
    CloseHandle(named);

    return same;
}
} // namespace voyemu::os
