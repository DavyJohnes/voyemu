#pragma once
#include <ctime>
#include <sys/stat.h>

namespace voyemu::os {
// POSIX stat timestamps with nanoseconds: Darwin names them st_*timespec, Linux st_*tim.
inline const timespec &modified_time(const struct stat &info) {
#ifdef __APPLE__
    return info.st_mtimespec;
#else
    return info.st_mtim;
#endif
}

inline const timespec &changed_time(const struct stat &info) {
#ifdef __APPLE__
    return info.st_ctimespec;
#else
    return info.st_ctim;
#endif
}
} // namespace voyemu::os
