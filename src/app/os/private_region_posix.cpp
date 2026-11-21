#include "os/private_region.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::os {
namespace {
bool named(const std::filesystem::path &path, std::uint64_t volume, std::uint64_t index) {
    struct stat st{};

    return ::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == ::geteuid() &&
           std::uint64_t(st.st_dev) == volume && std::uint64_t(st.st_ino) == index &&
           st.st_nlink == 1;
}
} // namespace

PrivateRegion::~PrivateRegion() {
    const bool owned = file_ >= 0 && named(path_, volume_, index_);

    release();

    if (owned) {
        ::unlink(path_.c_str());
    }
}

void PrivateRegion::release() noexcept {
    if (data_) {
        ::munmap(data_, bytes_);
        data_ = nullptr;
    }

    if (file_ >= 0) {
        ::close(static_cast<int>(file_));
        file_ = -1;
    }
}

PrivateRegion::Creation PrivateRegion::create(const std::filesystem::path &path,
                                              std::size_t bytes) {
    path_ = path;
    bytes_ = bytes;
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);

    if (fd < 0) {
        return Creation::create_failed;
    }

    file_ = fd;
    auto failed = [&](Creation creation) {
        release();
        ::unlink(path.c_str());

        return creation;
    };

    if (::ftruncate(fd, static_cast<off_t>(bytes))) {
        return failed(Creation::extent_failed);
    }

    struct stat st{};

    if (::fstat(fd, &st) || st.st_uid != ::geteuid() || st.st_nlink != 1 || !S_ISREG(st.st_mode)) {
        return failed(Creation::ownership_failed);
    }

    volume_ = std::uint64_t(st.st_dev);
    index_ = std::uint64_t(st.st_ino);
    void *data = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (data == MAP_FAILED) {
        return failed(Creation::mapping_failed);
    }

    data_ = data;

    return Creation::created;
}

bool PrivateRegion::current() const {
    struct stat st{};

    return ::fstat(static_cast<int>(file_), &st) == 0 && st.st_size == static_cast<off_t>(bytes_) &&
           st.st_uid == ::geteuid() && st.st_nlink == 1 && !(st.st_mode & 0077) &&
           named(path_, volume_, index_);
}
} // namespace voyemu::os
