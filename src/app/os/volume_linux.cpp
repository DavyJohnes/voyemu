#include "volume.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <linux/btrfs.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

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

    // A container may see the mounted filesystem without the host's udev block-device links.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (fd < 0) {
        return {};
    }

    struct stat opened{};

    if (::fstat(fd, &opened) != 0 || file.st_dev != opened.st_dev || file.st_ino != opened.st_ino) {
        ::close(fd);

        return {};
    }

#ifdef FS_IOC_GETFSUUID
    struct fsuuid2 uuid{};
    constexpr auto get_uuid = FS_IOC_GETFSUUID;
#else
    // FS_IOC_GETFSUUID can be available on the running kernel with older build headers.
    struct {
        unsigned char len;
        unsigned char uuid[16];
    } uuid{};
    constexpr auto get_uuid = _IOR(0x15, 0, decltype(uuid));
#endif

    if (::ioctl(fd, get_uuid, &uuid) != 0 || uuid.len != sizeof(uuid.uuid)) {
        struct btrfs_ioctl_fs_info_args info{};

        if (::ioctl(fd, BTRFS_IOC_FS_INFO, &info) != 0) {
            ::close(fd);

            return {};
        }

        static_assert(sizeof(info.fsid) == sizeof(uuid.uuid));
        std::memcpy(uuid.uuid, info.fsid, sizeof(uuid.uuid));
    }

    ::close(fd);

    if (std::all_of(std::begin(uuid.uuid), std::end(uuid.uuid), [](auto byte) {
            return byte == 0;
        })) {
        return {};
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(2 * sizeof(uuid.uuid));

    for (const auto byte : uuid.uuid) {
        id += hex[byte >> 4];
        id += hex[byte & 15];
    }

    return id;
}
} // namespace voyemu::os
