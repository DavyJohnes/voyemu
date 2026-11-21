#include "os/held_file.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::os {
namespace {
bool sealed(const struct stat &info) {
    return S_ISREG(info.st_mode) && info.st_uid == ::getuid() && info.st_nlink == 1 &&
           !(info.st_mode & 0022);
}
} // namespace

HeldFile::~HeldFile() {
    close();
}

HeldFile::Opening HeldFile::open(const std::filesystem::path &path, bool writable) {
    close();
    path_ = path;
    const int fd = ::open(path.c_str(), (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0) {
        return Opening::open_failed;
    }

    struct stat info{};

    if (::fstat(fd, &info) || !sealed(info)) {
        ::close(fd);

        return Opening::invalid;
    }

    handle_ = fd;
    volume_ = std::uint64_t(info.st_dev);
    index_ = std::uint64_t(info.st_ino);

    return Opening::opened;
}

bool HeldFile::bound() const {
    return handle_ >= 0;
}

void HeldFile::close() noexcept {
    unmap();

    if (handle_ >= 0) {
        ::close(static_cast<int>(handle_));
        handle_ = -1;
    }
}

std::optional<std::uint64_t> HeldFile::current_size() const {
    struct stat held{}, named{};

    if (handle_ < 0 || ::fstat(static_cast<int>(handle_), &held) ||
        ::lstat(path_.c_str(), &named) || !sealed(held) || !sealed(named) ||
        std::uint64_t(held.st_dev) != volume_ || std::uint64_t(held.st_ino) != index_ ||
        std::uint64_t(named.st_dev) != volume_ || std::uint64_t(named.st_ino) != index_ ||
        held.st_size < 0) {
        return std::nullopt;
    }

    return std::uint64_t(held.st_size);
}

long long HeldFile::read_at(std::uint64_t offset, void *data, std::size_t size) const {
    return ::pread(static_cast<int>(handle_), data, size, static_cast<off_t>(offset));
}

HeldFile::Mapping HeldFile::map(std::uint64_t offset, std::size_t bytes) {
    unmap();
    const auto granule = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
    const auto aligned = offset - offset % granule;
    const auto length = static_cast<std::size_t>(offset - aligned + bytes);
    void *mapped = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED,
                          static_cast<int>(handle_), static_cast<off_t>(aligned));

    if (mapped == MAP_FAILED) {
        return Mapping::view_failed;
    }

    base_ = mapped;
    length_ = length;
    address_ = static_cast<unsigned char *>(mapped) + (offset - aligned);

    return Mapping::mapped;
}

void HeldFile::unmap() noexcept {
    if (base_) {
        ::munmap(base_, length_);
    }

    base_ = address_ = nullptr;
    length_ = 0;
}

bool sealed_to_user(const std::filesystem::path &path) {
    struct stat info{};

    return !::lstat(path.c_str(), &info) && info.st_uid == ::getuid() && !(info.st_mode & 0022);
}
} // namespace voyemu::os
