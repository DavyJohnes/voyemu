#include "os/file_view.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::os {
FileView::~FileView() {
    reset();
}

void FileView::reset() {
    if (data_) {
        munmap(data_, bytes_);
    }

    if (file_ >= 0) {
        close(static_cast<int>(file_));
    }

    data_ = nullptr;
    file_ = -1;
}

FileView::Opening FileView::open(const std::filesystem::path &path, std::size_t bytes, bool create,
                                 bool writable) {
    bytes_ = bytes;
    const int fd = ::open(path.c_str(),
                          (create     ? O_RDWR | O_CREAT | O_EXCL
                           : writable ? O_RDWR
                                      : O_RDONLY) |
                                  O_NOFOLLOW,
                          0600);
    file_ = fd;

    if (fd < 0) {
        return !create && errno == ENOENT ? Opening::absent : Opening::open_failed;
    }

    struct stat st{};

    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
        return Opening::not_ordinary;
    }

    if (create && ftruncate(fd, static_cast<off_t>(bytes))) {
        return Opening::resize_failed;
    }

    if (fstat(fd, &st) || static_cast<std::uint64_t>(st.st_size) != bytes) {
        return Opening::extent_differs;
    }

    void *p = mmap(nullptr, bytes, writable ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd,
                   0);

    if (p == MAP_FAILED) {
        return Opening::view_failed;
    }

    data_ = static_cast<std::byte *>(p);

    return Opening::opened;
}

FileView::Identity FileView::current(const std::filesystem::path &path) const {
    struct stat original{}, replacement{};

    if (lstat(path.c_str(), &replacement)) {
        return errno == ENOENT ? Identity::absent : Identity::identity_failed;
    }

    if (fstat(static_cast<int>(file_), &original) || !S_ISREG(replacement.st_mode)) {
        return Identity::not_ordinary;
    }

    return original.st_dev == replacement.st_dev && original.st_ino == replacement.st_ino
                   ? Identity::same
                   : Identity::replaced;
}
} // namespace voyemu::os
