#include "check.hpp"
#include "java_toolchain_identity.hpp"
#include "os/stat_time.hpp"
#include <sys/stat.h>
#include <unistd.h>
namespace voyemu {
namespace {
constexpr Check need{"JAVA_TOOLCHAIN"};
} // namespace

Json immutable_input_identity(const std::filesystem::path &path) {
    struct stat info{};

    // An installed system JDK is root-owned and not writable by anyone else.
    need(!::lstat(path.c_str(), &info) && S_ISREG(info.st_mode) &&
                 (info.st_uid == ::geteuid() || (info.st_uid == 0 && !(info.st_mode & 0022))) &&
                 info.st_nlink > 0,
         "owned ordinary immutable input required");

    return {{"device", info.st_dev},
            {"inode", info.st_ino},
            {"owner", info.st_uid},
            {"mode", info.st_mode},
            {"links", info.st_nlink},
            {"size", info.st_size},
            {"mtime_seconds", os::modified_time(info).tv_sec},
            {"mtime_ns", os::modified_time(info).tv_nsec},
            {"ctime_seconds", os::changed_time(info).tv_sec},
            {"ctime_ns", os::changed_time(info).tv_nsec}};
}
} // namespace voyemu
