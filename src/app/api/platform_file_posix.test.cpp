#include "platform_file_test_host.hpp"
#include <cassert>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>

void metadata(const std::filesystem::path &path) {
    (void)path;
}

void write(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path, std::ios::binary);

    file << value;
    assert(file.good());
}

bool change_owner(const std::filesystem::path &path) {
    return ::chown(path.c_str(), ::geteuid() + 1, static_cast<gid_t>(-1)) == 0;
}

SupervisorLock::SupervisorLock(const std::filesystem::path &path)
    : handle_(::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600)) {
    assert(handle_ >= 0 && ::flock(static_cast<int>(handle_), LOCK_EX | LOCK_NB) == 0);
}

SupervisorLock::~SupervisorLock() {
    const int closed = ::close(static_cast<int>(handle_));

    assert(closed == 0);
    (void)closed;
}
