#include "model.hpp"
#include "platform_file_internal.hpp"
#include <limits>
#include <stdexcept>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>

#include "private_state_security.hpp"

namespace voyemu::api::platform {
namespace {
HANDLE native(const File &file) {
    return reinterpret_cast<HANDLE>(file.value);
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;

    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

bool owned(HANDLE file) {
    Handle token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
        return false;
    }

    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    std::vector<std::byte> bytes(size);

    if (!size || !GetTokenInformation(token.value, TokenUser, bytes.data(), size, &size)) {
        return false;
    }

    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto status = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner,
                                        nullptr, nullptr, nullptr, &descriptor);
    const bool same = status == ERROR_SUCCESS && owner &&
                      EqualSid(owner, reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid);

    if (descriptor) {
        LocalFree(descriptor);
    }

    return same;
}

bool ordinary(HANDLE file, BY_HANDLE_FILE_INFORMATION &info) {
    return GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
           !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
           info.nNumberOfLinks == 1 && owned(file);
}
} // namespace

File::~File() {
    if (native(*this) != INVALID_HANDLE_VALUE) {
        CloseHandle(native(*this));
    }
}

bool lock(File &file, const std::filesystem::path &path, bool create) {
    // Share mode zero matches cli::OperationLock, including supervisor locks.
    const HANDLE handle = voyemu::detail::open_private_state_lock(
            path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL, create ? OPEN_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);

    file.value = reinterpret_cast<os::FileHandle>(handle);
    BY_HANDLE_FILE_INFORMATION info{};

    return handle != INVALID_HANDLE_VALUE && ordinary(handle, info);
}

LogOpening open_log(File &file, const std::filesystem::path &path) {
    LogOpening opening;
    const HANDLE handle = CreateFileW(
            path.c_str(), GENERIC_READ | READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    file.value = reinterpret_cast<os::FileHandle>(handle);
    opening.opened = handle != INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info{};

    if (opening.opened && ordinary(handle, info)) {
        opening.ordinary = true;
        opening.identity =
                std::to_string(info.dwVolumeSerialNumber) + ":" +
                std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
        opening.size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    }

    return opening;
}

long long read_at(const File &file, std::uint64_t offset, char *data, std::size_t size) {
    LARGE_INTEGER position{};

    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;

    if (!SetFilePointerEx(native(file), position, nullptr, FILE_BEGIN) ||
        !ReadFile(native(file), data, DWORD(size), &count, nullptr)) {
        return -1;
    }

    return count;
}
} // namespace voyemu::api::platform
