#include "platform_file.hpp"
#include "model.hpp"
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>

#include "private_state_security.hpp"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voyemu::api {
namespace {
#ifdef _WIN32
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
#else
struct Handle {
    int value = -1;

    ~Handle() {
        if (value >= 0) {
            ::close(value);
        }
    }
};

bool ordinary(int file, struct stat &info) {
    return ::fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == ::geteuid() &&
           info.st_nlink == 1;
}
#endif
} // namespace
struct FileLock::Impl {
    Handle file;

    bool open(const std::filesystem::path &path, bool create) {
#ifdef _WIN32
        // Share mode zero matches cli::OperationLock, including supervisor locks.
        file.value = voyemu::detail::open_private_state_lock(
                path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                create ? OPEN_ALWAYS : OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
        BY_HANDLE_FILE_INFORMATION info{};

        return file.value != INVALID_HANDLE_VALUE && ordinary(file.value, info);
#else
        file.value = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT : 0),
                            0600);
        struct stat info{};

        return file.value >= 0 && ordinary(file.value, info) &&
               ::flock(file.value, LOCK_EX | LOCK_NB) == 0;
#endif
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
#ifdef _WIN32
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
#else
        file.value = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

        if (file.value < 0) {
            throw VoyemuError("TEST_LOGS_UNAVAILABLE", "supervisor log unavailable");
        }

        struct stat info{};

        if (!ordinary(file.value, info)) {
            throw VoyemuError("TEST_LOGS", "ordinary owned log required");
        }

        identity = std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino);
        size = static_cast<std::uint64_t>(info.st_size);
#endif
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

#ifdef _WIN32
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;

    if (!SetFilePointerEx(impl_->file.value, position, nullptr, FILE_BEGIN) ||
        !ReadFile(impl_->file.value, bytes.data(), DWORD(bytes.size()), &count, nullptr)) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }
#else
    const auto count = ::pread(impl_->file.value, bytes.data(), bytes.size(),
                               static_cast<off_t>(offset));

    if (count < 0) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }
#endif
    bytes.resize(count);

    return bytes;
}
} // namespace voyemu::api
