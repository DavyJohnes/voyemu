#include "fs.hpp"
#include "platform.hpp"
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace voyemu::os {
bool host_kvm_available() {
    if (dependency_host != "linux/x64") {
        return false;
    }

    const int device = ::open("/dev/kvm", O_RDWR | O_CLOEXEC);

    if (device < 0) {
        return false;
    }

    const bool available = ::ioctl(device, KVM_GET_API_VERSION, 0) == KVM_API_VERSION;
    ::close(device);

    return available;
}

bool rename_exclusive(const std::filesystem::path &from, const std::filesystem::path &to) {
    return ::renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0;
}

bool cloning_volume(const std::filesystem::path &) {
    return false;
}

bool clone_file(const std::filesystem::path &, const std::filesystem::path &) {
    return false;
}

bool exchange_paths(const std::filesystem::path &first, const std::filesystem::path &second) {
    return ::renameat2(AT_FDCWD, first.c_str(), AT_FDCWD, second.c_str(), RENAME_EXCHANGE) == 0;
}
} // namespace voyemu::os
