#include "error.hpp"
#include "quickboot_stamp.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace voyemu {
Json file_stamp(const std::filesystem::path &path, std::uintmax_t expected_links) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw VoyemuError("QUICKBOOT_FILE", "cannot open storage");
    }

    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool valid =
            GetFileInformationByHandle(file, &info) &&
            GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof basic) &&
            info.nNumberOfLinks == expected_links &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    CloseHandle(file);

    if (!valid) {
        throw VoyemuError("QUICKBOOT_FILE", "storage identity rejected");
    }

    return {{"volume", info.dwVolumeSerialNumber},
            {"index_high", info.nFileIndexHigh},
            {"index_low", info.nFileIndexLow},
            {"size_high", info.nFileSizeHigh},
            {"size_low", info.nFileSizeLow},
            {"mtime_high", info.ftLastWriteTime.dwHighDateTime},
            {"mtime_low", info.ftLastWriteTime.dwLowDateTime},
            {"change_time", basic.ChangeTime.QuadPart}};
}
} // namespace voyemu
