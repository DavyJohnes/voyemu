#include "error.hpp"
#include "os/stat_time.hpp"
#include "os/volume.hpp"
#include "quickboot_stamp.hpp"
#include <sys/stat.h>
#include <unistd.h>
namespace voyemu {
Json file_stamp(const std::filesystem::path &path, std::uintmax_t expected_links) {
    struct stat info{};

    if (::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() ||
        info.st_nlink != expected_links) {
        throw VoyemuError("QUICKBOOT_FILE", "storage identity rejected");
    }

    const auto volume_uuid = os::stable_volume_id(path);

    if (volume_uuid.empty()) {
        throw VoyemuError("QUICKBOOT_FILE", "stable volume identity unavailable");
    }

    return {{"volume_uuid", volume_uuid},
            {"inode", info.st_ino},
            {"size", info.st_size},
            {"mtime_seconds", os::modified_time(info).tv_sec},
            {"mtime_ns", os::modified_time(info).tv_nsec},
            {"ctime_seconds", os::changed_time(info).tv_sec},
            {"ctime_ns", os::changed_time(info).tv_nsec}};
}
} // namespace voyemu
