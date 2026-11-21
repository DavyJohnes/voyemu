#include "model.hpp"
#include "platform_file.hpp"
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace voyemu::api {
namespace {
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
} // namespace
struct FileLock::Impl {
    Handle file;

    bool open(const std::filesystem::path &path, bool create) {
        file.value = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT : 0),
                            0600);
        struct stat info{};

        return file.value >= 0 && ordinary(file.value, info) &&
               ::flock(file.value, LOCK_EX | LOCK_NB) == 0;
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

    const auto count = ::pread(impl_->file.value, bytes.data(), bytes.size(),
                               static_cast<off_t>(offset));

    if (count < 0) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }

    bytes.resize(count);

    return bytes;
}
} // namespace voyemu::api
