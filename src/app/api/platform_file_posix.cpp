#include "model.hpp"
#include "platform_file_internal.hpp"
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace voyemu::api::platform {
namespace {
bool ordinary(int file, struct stat &info) {
    return ::fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == ::geteuid() &&
           info.st_nlink == 1;
}
} // namespace

File::~File() {
    if (value >= 0) {
        ::close(static_cast<int>(value));
    }
}

bool lock(File &file, const std::filesystem::path &path, bool create) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT : 0),
                          0600);

    file.value = fd;
    struct stat info{};

    return fd >= 0 && ordinary(fd, info) && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
}

LogOpening open_log(File &file, const std::filesystem::path &path) {
    LogOpening opening;
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    file.value = fd;
    opening.opened = fd >= 0;
    struct stat info{};

    if (opening.opened && ordinary(fd, info)) {
        opening.ordinary = true;
        opening.identity = std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino);
        opening.size = static_cast<std::uint64_t>(info.st_size);
    }

    return opening;
}

long long read_at(const File &file, std::uint64_t offset, char *data, std::size_t size) {
    return ::pread(static_cast<int>(file.value), data, size, static_cast<off_t>(offset));
}
} // namespace voyemu::api::platform
