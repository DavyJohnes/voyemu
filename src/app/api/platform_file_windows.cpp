#include "model.hpp"
#include "platform_file.hpp"
#include <limits>
#include <stdexcept>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>

#include "private_state_security.hpp"

namespace voyemu::api {
namespace {
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
struct FileLock::Impl {
    Handle file;

    bool open(const std::filesystem::path &path, bool create) {
        // Share mode zero matches cli::OperationLock, including supervisor locks.
        file.value = voyemu::detail::open_private_state_lock(
                path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                create ? OPEN_ALWAYS : OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
        BY_HANDLE_FILE_INFORMATION info{};

        return file.value != INVALID_HANDLE_VALUE && ordinary(file.value, info);
    }
};

FileLock::FileLock(const std::filesystem::path &path) : impl_(std::make_unique<Impl>()) {
    ensure_private_directory(path.parent_path());

    if (!impl_->open(path, true)) {
        throw VoyemuError("TEST_BUSY", "another automation writer or capture is active");
    }
}

FileLock::~FileLock() = default;

bool file_lock_released(const std::filesystem::path &path) {
    FileLock::Impl probe;

    return probe.open(path, false);
}

struct LogFile::Impl {
    Handle file;
    std::string identity;
    std::uint64_t size{};

    explicit Impl(const std::filesystem::path &path) {
        file.value = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (file.value == INVALID_HANDLE_VALUE) {
            throw VoyemuError("TEST_LOGS_UNAVAILABLE", "supervisor log unavailable");
        }

        BY_HANDLE_FILE_INFORMATION info{};

        if (!ordinary(file.value, info)) {
            throw VoyemuError("TEST_LOGS", "ordinary owned log required");
        }

        identity = std::to_string(info.dwVolumeSerialNumber) + ":" +
                   std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
        size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    }
};

LogFile::LogFile(const std::filesystem::path &path) : impl_(std::make_unique<Impl>(path)) {}

LogFile::~LogFile() = default;

std::string LogFile::identity() const {
    return impl_->identity;
}

std::uint64_t LogFile::size() const {
    return impl_->size;
}

std::string LogFile::read(std::uint64_t offset) const {
    std::string bytes(65536, '\0');

    if (offset > std::uint64_t(std::numeric_limits<std::int64_t>::max())) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }

    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;

    if (!SetFilePointerEx(impl_->file.value, position, nullptr, FILE_BEGIN) ||
        !ReadFile(impl_->file.value, bytes.data(), DWORD(bytes.size()), &count, nullptr)) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }

    bytes.resize(count);

    return bytes;
}
} // namespace voyemu::api
