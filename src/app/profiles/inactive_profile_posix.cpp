#include "inactive_profile.hpp"
#include "os/process.hpp"
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::inactive_profile {
namespace {
// A private single-link regular file of the effective user.
bool sealed(const struct stat &s) {
    return S_ISREG(s.st_mode) && s.st_uid == ::geteuid() && s.st_nlink == 1 && !(s.st_mode & 0077);
}
} // namespace

void ordinary(const std::filesystem::path &path, bool directory) {
    struct stat s{};

    require(path.is_absolute() && path.lexically_normal() == path &&
            std::filesystem::canonical(path) == path && ::lstat(path.c_str(), &s) == 0 &&
            s.st_uid == ::geteuid() && !(s.st_mode & 0077) &&
            (directory ? S_ISDIR(s.st_mode) : (S_ISREG(s.st_mode) && s.st_nlink == 1)));
}

bool same_directory(const Json &recorded, const std::filesystem::path &directory) {
    return recorded == directory.string();
}

void absent(const Json &pid) {
    require(pid.is_number_integer());
    const auto value = pid.get<std::int64_t>();
    require(value == 0 || (value > 1 && value <= INT_MAX &&
                           ::kill(static_cast<pid_t>(value), 0) < 0 && errno == ESRCH));
}

void unused(const std::filesystem::path &path) {
    ordinary(path);
    require(os::file_use(path) == os::FileUse::unused);
}

void endpoint_absent(const std::filesystem::path &endpoint, const std::string &id) {
    require(endpoint.is_absolute() && endpoint.lexically_normal() == endpoint &&
            endpoint.filename() == id + ".sock");
    ordinary(endpoint.parent_path(), true);
    struct stat socket{};
    errno = 0;
    require(::lstat(endpoint.c_str(), &socket) < 0 && errno == ENOENT);
}

SupervisorLock::SupervisorLock(const std::filesystem::path &path) : path_(path) {
    ordinary(path);
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0);
    struct stat held{}, named{};

    if (!(::fstat(fd, &held) == 0 && ::lstat(path.c_str(), &named) == 0 && sealed(held) &&
          held.st_dev == named.st_dev && held.st_ino == named.st_ino &&
          ::flock(fd, LOCK_EX | LOCK_NB) == 0)) {
        ::close(fd);
        require(false);
    }

    handle_ = fd;
    volume_ = std::uint64_t(held.st_dev);
    index_ = std::uint64_t(held.st_ino);
}

SupervisorLock::~SupervisorLock() {
    if (handle_ >= 0) {
        ::close(static_cast<int>(handle_));
    }
}

void SupervisorLock::verify() const {
    ordinary(path_);
    struct stat named{}, held{};
    require(::lstat(path_.c_str(), &named) == 0 && ::fstat(static_cast<int>(handle_), &held) == 0 &&
            std::uint64_t(named.st_dev) == volume_ && std::uint64_t(named.st_ino) == index_ &&
            std::uint64_t(held.st_dev) == volume_ && std::uint64_t(held.st_ino) == index_ &&
            sealed(held));
}
} // namespace voyemu::inactive_profile
