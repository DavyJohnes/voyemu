#include "os/current_user_windows.hpp"
#include "os/held_file.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <vector>

namespace voyemu::os {
namespace {
HANDLE native(std::intptr_t handle) {
    return reinterpret_cast<HANDLE>(handle);
}

// One owned link to a plain disk file, never a reparse point.
bool ordinary(HANDLE file, BY_HANDLE_FILE_INFORMATION &info) {
    return file != INVALID_HANDLE_VALUE && GetFileType(file) == FILE_TYPE_DISK &&
           GetFileInformationByHandle(file, &info) &&
           !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
           info.nNumberOfLinks == 1 && owned_by_current_user(file);
}

std::uint64_t join(DWORD high, DWORD low) {
    return std::uint64_t(high) << 32 | low;
}
} // namespace

HeldFile::~HeldFile() {
    close();
}

HeldFile::Opening HeldFile::open(const std::filesystem::path &path, bool writable) {
    close();
    path_ = path;
    // The producer keeps the file open for writing, so the handle shares writes.
    const HANDLE file = CreateFileW(path.c_str(),
                                    GENERIC_READ | (writable ? GENERIC_WRITE : 0) | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return Opening::open_failed;
    }

    BY_HANDLE_FILE_INFORMATION info{};

    if (!ordinary(file, info)) {
        CloseHandle(file);

        return Opening::invalid;
    }

    handle_ = reinterpret_cast<std::intptr_t>(file);
    volume_ = info.dwVolumeSerialNumber;
    index_ = join(info.nFileIndexHigh, info.nFileIndexLow);

    return Opening::opened;
}

bool HeldFile::bound() const {
    return native(handle_) != INVALID_HANDLE_VALUE;
}

void HeldFile::close() noexcept {
    unmap();

    if (bound()) {
        CloseHandle(native(handle_));
        handle_ = -1;
    }
}

std::optional<std::uint64_t> HeldFile::current_size() const {
    BY_HANDLE_FILE_INFORMATION held{}, named{};

    if (!ordinary(native(handle_), held) || held.dwVolumeSerialNumber != volume_ ||
        join(held.nFileIndexHigh, held.nFileIndexLow) != index_) {
        return std::nullopt;
    }

    const HANDLE current = CreateFileW(path_.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool valid = ordinary(current, named) && named.dwVolumeSerialNumber == volume_ &&
                       join(named.nFileIndexHigh, named.nFileIndexLow) == index_;

    if (current != INVALID_HANDLE_VALUE) {
        CloseHandle(current);
    }

    if (!valid) {
        return std::nullopt;
    }

    return join(held.nFileSizeHigh, held.nFileSizeLow);
}

long long HeldFile::read_at(std::uint64_t offset, void *data, std::size_t size) const {
    LARGE_INTEGER at{};

    at.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;

    if (!SetFilePointerEx(native(handle_), at, nullptr, FILE_BEGIN) ||
        !ReadFile(native(handle_), data, static_cast<DWORD>(size), &count, nullptr)) {
        return -1;
    }

    return count;
}

HeldFile::Mapping HeldFile::map(std::uint64_t offset, std::size_t bytes) {
    unmap();
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto granule = std::uint64_t(system.dwAllocationGranularity);

    if (!granule || granule > 65536) {
        return Mapping::granularity;
    }

    const auto aligned = offset - offset % granule;
    const HANDLE mapping = CreateFileMappingW(native(handle_), nullptr, PAGE_READWRITE, 0, 0,
                                              nullptr);

    if (!mapping) {
        return Mapping::handle_failed;
    }

    const auto length = static_cast<std::size_t>(offset - aligned + bytes);
    void *view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE,
                               static_cast<DWORD>(aligned >> 32), static_cast<DWORD>(aligned),
                               length);

    if (!view) {
        CloseHandle(mapping);

        return Mapping::view_failed;
    }

    mapping_ = reinterpret_cast<std::intptr_t>(mapping);
    base_ = view;
    length_ = length;
    address_ = static_cast<unsigned char *>(view) + (offset - aligned);

    return Mapping::mapped;
}

void HeldFile::unmap() noexcept {
    if (base_) {
        UnmapViewOfFile(base_);
    }

    if (mapping_) {
        CloseHandle(native(mapping_));
    }

    mapping_ = 0;
    base_ = address_ = nullptr;
    length_ = 0;
}

bool sealed_to_user(const std::filesystem::path &) {
    return true;
}
} // namespace voyemu::os
