#include "core/process.hpp"
#include "os/zero_view.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

namespace voyemu::os {
ZeroView::~ZeroView() {
    if (data_) {
        UnmapViewOfFile(data_);
    }

    if (mapping_) {
        CloseHandle(reinterpret_cast<HANDLE>(mapping_));
    }

    if (reinterpret_cast<HANDLE>(file_) != INVALID_HANDLE_VALUE) {
        CloseHandle(reinterpret_cast<HANDLE>(file_));
    }
}

ZeroView::Opening ZeroView::open(std::size_t bytes, const std::filesystem::path &directory) {
    bytes_ = bytes;
    const auto name = directory / ("qnx-zero-reference-" + random_token() + ".bin");
    const HANDLE file = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    file_ = reinterpret_cast<std::intptr_t>(file);

    if (file == INVALID_HANDLE_VALUE) {
        return Opening::file_failed;
    }

    DWORD returned = 0;

    if (!DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
        return Opening::sparse_failed;
    }

    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(bytes);

    if (!SetFilePointerEx(file, end, nullptr, FILE_BEGIN) || !SetEndOfFile(file)) {
        return Opening::extent_failed;
    }

    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    mapping_ = reinterpret_cast<std::intptr_t>(mapping);

    if (!mapping) {
        return Opening::mapping_failed;
    }

    data_ = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, bytes);

    return data_ ? Opening::mapped : Opening::view_failed;
}
} // namespace voyemu::os
