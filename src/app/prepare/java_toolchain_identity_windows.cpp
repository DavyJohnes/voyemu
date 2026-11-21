#include "check.hpp"
#include "java_toolchain_identity.hpp"
#include <windows.h>
namespace voyemu {
namespace {
constexpr Check need{"JAVA_TOOLCHAIN"};
} // namespace

Json immutable_input_identity(const std::filesystem::path &path) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    need(file != INVALID_HANDLE_VALUE, "immutable input open failed");
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool valid =
            GetFileInformationByHandle(file, &info) &&
            GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof basic) &&
            info.nNumberOfLinks > 0 &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    CloseHandle(file);
    need(valid, "ordinary immutable input required");

    return {{"volume", info.dwVolumeSerialNumber},
            {"index_high", info.nFileIndexHigh},
            {"index_low", info.nFileIndexLow},
            {"links", info.nNumberOfLinks},
            {"attributes", info.dwFileAttributes},
            {"size_high", info.nFileSizeHigh},
            {"size_low", info.nFileSizeLow},
            {"mtime_high", info.ftLastWriteTime.dwHighDateTime},
            {"mtime_low", info.ftLastWriteTime.dwLowDateTime},
            {"change_time", basic.ChangeTime.QuadPart}};
}
} // namespace voyemu
