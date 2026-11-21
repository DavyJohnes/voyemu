#include "os/file_view.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace voyemu::os {
namespace {
HANDLE native(std::intptr_t handle) {
    return reinterpret_cast<HANDLE>(handle);
}
} // namespace

FileView::~FileView() {
    reset();
}

void FileView::reset() {
    if (data_) {
        UnmapViewOfFile(data_);
    }

    if (mapping_) {
        CloseHandle(native(mapping_));
    }

    if (native(file_) != INVALID_HANDLE_VALUE) {
        CloseHandle(native(file_));
    }

    data_ = nullptr;
    file_ = -1;
    mapping_ = 0;
}

FileView::Opening FileView::open(const std::filesystem::path &path, std::size_t bytes, bool create,
                                 bool writable) {
    bytes_ = bytes;
    const HANDLE file = CreateFileW(path.c_str(),
                                    writable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    create ? CREATE_NEW : OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    file_ = reinterpret_cast<std::intptr_t>(file);

    if (file == INVALID_HANDLE_VALUE) {
        return !create && GetLastError() == ERROR_FILE_NOT_FOUND ? Opening::absent
                                                                 : Opening::open_failed;
    }

    BY_HANDLE_FILE_INFORMATION info{};

    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        return Opening::not_ordinary;
    }

    LARGE_INTEGER size{};
    size.QuadPart = static_cast<LONGLONG>(bytes);

    if (create && (!SetFilePointerEx(file, size, nullptr, FILE_BEGIN) || !SetEndOfFile(file))) {
        return Opening::resize_failed;
    }

    if (!GetFileSizeEx(file, &size) || static_cast<std::uint64_t>(size.QuadPart) != bytes) {
        return Opening::extent_differs;
    }

    const HANDLE mapping = CreateFileMappingW(
            file, nullptr, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, nullptr);
    mapping_ = reinterpret_cast<std::intptr_t>(mapping);

    if (!mapping) {
        return Opening::mapping_failed;
    }

    data_ = static_cast<std::byte *>(MapViewOfFile(
            mapping, writable ? FILE_MAP_WRITE | FILE_MAP_READ : FILE_MAP_READ, 0, 0, bytes));

    return data_ ? Opening::opened : Opening::view_failed;
}

FileView::Identity FileView::current(const std::filesystem::path &path) const {
    const HANDLE candidate =
            CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (candidate == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
                       ? Identity::absent
                       : Identity::identity_failed;
    }

    BY_HANDLE_FILE_INFORMATION original{}, replacement{};
    const bool valid = GetFileInformationByHandle(native(file_), &original) &&
                       GetFileInformationByHandle(candidate, &replacement);
    CloseHandle(candidate);

    if (!valid || (replacement.dwFileAttributes &
                   (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        return Identity::not_ordinary;
    }

    return original.dwVolumeSerialNumber == replacement.dwVolumeSerialNumber &&
                           original.nFileIndexHigh == replacement.nFileIndexHigh &&
                           original.nFileIndexLow == replacement.nFileIndexLow
                   ? Identity::same
                   : Identity::replaced;
}
} // namespace voyemu::os
