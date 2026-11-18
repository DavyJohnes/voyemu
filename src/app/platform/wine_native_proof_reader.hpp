#pragma once
#include "error.hpp"
#ifdef _WIN32
#include <windows.h>

#include "os/fs.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
namespace voyemu::detail {
// The native publisher atomically replaces snapshots without a Windows mutex.
// A retained ordinary incarnation may already be unlinked, but never aliased.
class WineNativeProofReader {
    HANDLE file_ = INVALID_HANDLE_VALUE;

public:
    explicit WineNativeProofReader(const std::filesystem::path &path) {
        for (const auto &current :
             os::ancestors(std::filesystem::absolute(path).lexically_normal())) {
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(current))) {
                throw VoyemuError("DIAGNOSTIC_PROOF", "symlink path rejected");
            }
        }

        file_ = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);

        if (file_ == INVALID_HANDLE_VALUE) {
            throw VoyemuError("DIAGNOSTIC_PROOF", "snapshot open failed");
        }
    }

    ~WineNativeProofReader() {
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
        }
    }

    WineNativeProofReader(const WineNativeProofReader &) = delete;
    WineNativeProofReader &operator=(const WineNativeProofReader &) = delete;

    std::string text() {
        BY_HANDLE_FILE_INFORMATION before{}, after{};
        auto ordinary = [&](BY_HANDLE_FILE_INFORMATION &info) {
            return GetFileType(file_) == FILE_TYPE_DISK &&
                   GetFileInformationByHandle(file_, &info) &&
                   !(info.dwFileAttributes &
                     (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                   info.nNumberOfLinks <= 1 && info.nFileSizeHigh == 0 &&
                   info.nFileSizeLow <= 16384;
        };

        if (!ordinary(before)) {
            throw VoyemuError("DIAGNOSTIC_PROOF", "ordinary bounded snapshot required");
        }

        std::string result;
        char bytes[4096];

        for (;;) {
            DWORD count = 0;

            if (!ReadFile(file_, bytes, sizeof bytes, &count, nullptr)) {
                throw VoyemuError("DIAGNOSTIC_PROOF", "snapshot read failed");
            }

            if (!count) {
                break;
            }

            if (result.size() + count > 16384) {
                throw VoyemuError("DIAGNOSTIC_PROOF", "snapshot grew");
            }

            result.append(bytes, count);
        }

        if (!ordinary(after) || before.nFileSizeLow != after.nFileSizeLow ||
            before.ftLastWriteTime.dwLowDateTime != after.ftLastWriteTime.dwLowDateTime ||
            before.ftLastWriteTime.dwHighDateTime != after.ftLastWriteTime.dwHighDateTime ||
            result.size() != before.nFileSizeLow) {
            throw VoyemuError("DIAGNOSTIC_PROOF", "snapshot changed during read");
        }

        return result;
    }
};
} // namespace voyemu::detail
#endif
