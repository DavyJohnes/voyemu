#include "volume.hpp"
#include <algorithm>
#include <cstdint>
#include <sys/attr.h>
#include <unistd.h>

namespace voyemu::os {
std::string stable_volume_id(const std::filesystem::path &path) {
    // st_dev is a mount number and can change after reboot without changing any file.
    struct attrlist attributes{};

    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.volattr = ATTR_VOL_INFO | ATTR_VOL_UUID;
    struct {
        std::uint32_t length;
        unsigned char uuid[16];
    } volume{};

    if (::getattrlist(path.c_str(), &attributes, &volume, sizeof(volume), 0) != 0 ||
        volume.length != sizeof(volume) ||
        std::all_of(volume.uuid, volume.uuid + 16, [](auto value) {
            return value == 0;
        })) {
        return {};
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string volume_uuid;
    volume_uuid.reserve(32);

    for (auto byte : volume.uuid) {
        volume_uuid += hex[byte >> 4];
        volume_uuid += hex[byte & 15];
    }

    return volume_uuid;
}
} // namespace voyemu::os
