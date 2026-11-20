#include "fs.hpp"
#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

// The file primitives that macOS and Linux share; fs_linux.cpp and fs_macos.cpp hold the renames.
namespace voyemu::os {
ExclusiveFile::ExclusiveFile(const std::filesystem::path &path, bool share_read)
    : handle_(::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600)),
      exists_(handle_ < 0 && errno == EEXIST) {
    (void)share_read; // POSIX does not exclude readers of an open file.
}

ExclusiveFile::~ExclusiveFile() {
    close();
}

bool ExclusiveFile::open() const {
    return handle_ >= 0;
}

bool ExclusiveFile::write(const void *data, std::size_t size) {
    const auto *cursor = static_cast<const char *>(data);

    while (size) {
        const auto done = ::write(int(handle_), cursor, size);

        if (done < 0 && errno == EINTR) {
            continue;
        }

        if (done <= 0) {
            return false;
        }

        cursor += done;
        size -= std::size_t(done);
    }

    return true;
}

bool ExclusiveFile::sync() {
    return ::fsync(int(handle_)) == 0;
}

bool ExclusiveFile::close() {
    if (handle_ < 0) {
        return true;
    }

    const bool closed = ::close(int(handle_)) == 0;
    handle_ = -1;

    return closed;
}

FileLock::FileLock(const std::filesystem::path &path, bool exclusive)
    : handle_(::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600)) {
    if (handle_ >= 0 && ::flock(int(handle_), (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
        ::close(int(handle_));
        handle_ = -1;
    }
}

FileLock::~FileLock() {
    if (handle_ >= 0) {
        ::close(int(handle_));
    }
}

bool FileLock::held() const {
    return handle_ >= 0;
}

bool link_exclusive(const std::filesystem::path &existing, const std::filesystem::path &link) {
    return ::link(existing.c_str(), link.c_str()) == 0;
}

bool replace_file(const std::filesystem::path &from, const std::filesystem::path &to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
}

bool reparse_point(const std::filesystem::path &) {
    return false;
}

bool foreign_owner(const std::filesystem::path &path) {
    struct stat status{};

    return ::stat(path.c_str(), &status) != 0 || status.st_uid != ::geteuid();
}

bool error_terminal() {
    return ::isatty(STDERR_FILENO) == 1;
}
} // namespace voyemu::os
