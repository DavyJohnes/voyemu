#include "volume.hpp"
#include <cctype>
#include <sys/stat.h>
#include <system_error>

namespace voyemu::os {
std::string stable_volume_id(const std::filesystem::path &path) {
    // st_dev is a mount number; the filesystem UUID published by udev names the block device.
    struct stat file{};

    if (::stat(path.c_str(), &file) != 0) {
        return {};
    }

    std::error_code error;

    for (std::filesystem::directory_iterator entry("/dev/disk/by-uuid", error), end;
         !error && entry != end; entry.increment(error)) {
        struct stat device{};

        if (::stat(entry->path().c_str(), &device) != 0 || !S_ISBLK(device.st_mode) ||
            device.st_rdev != file.st_dev) {
            continue;
        }

        std::string id;

        for (auto character : entry->path().filename().string()) {
            if (std::isxdigit(static_cast<unsigned char>(character))) {
                id += char(std::tolower(static_cast<unsigned char>(character)));
            } else if (character != '-') {
                return {};
            }
        }

        return id;
    }

    return {};
}
} // namespace voyemu::os
