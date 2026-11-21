#include "os/durable_disk.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::os {
DurableDisk::~DurableDisk() {
    close();
}

void DurableDisk::close() noexcept {
    if (handle_ >= 0) {
        ::close(static_cast<int>(handle_));
        handle_ = -1;
    }
}

bool DurableDisk::open(const std::filesystem::path &path, std::uint64_t bytes) {
    path_ = path;
    const int fd = ::open(path.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    handle_ = fd;
    struct stat identity{};

    if (fd < 0 || ::fstat(fd, &identity) || !S_ISREG(identity.st_mode) ||
        identity.st_uid != ::geteuid() || identity.st_nlink != 1 || (identity.st_mode & 0077) ||
        static_cast<std::uint64_t>(identity.st_size) != bytes) {
        return false;
    }

    volume_ = static_cast<std::uint64_t>(identity.st_dev);
    index_ = static_cast<std::uint64_t>(identity.st_ino);
    size_ = bytes;

    return true;
}

bool DurableDisk::same_file(const DurableDisk &other) const {
    return volume_ == other.volume_ && index_ == other.index_;
}

bool DurableDisk::current() const {
    struct stat now{};

    return !::lstat(path_.c_str(), &now) && static_cast<std::uint64_t>(now.st_dev) == volume_ &&
           static_cast<std::uint64_t>(now.st_ino) == index_ &&
           static_cast<std::uint64_t>(now.st_size) == size_ && now.st_nlink == 1;
}

bool DurableDisk::flush() {
    const int fd = static_cast<int>(handle_);
    int result;

    do {
        result = ::fsync(fd);
    } while (result && errno == EINTR);

#ifdef F_FULLFSYNC
    // macOS fsync leaves the data in the drive cache; F_FULLFSYNC reaches the medium.
    if (!result) {
        do {
            result = ::fcntl(fd, F_FULLFSYNC);
        } while (result && errno == EINTR);
    }
#endif

    return result == 0;
}
} // namespace voyemu::os
