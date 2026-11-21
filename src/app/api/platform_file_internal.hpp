#pragma once
// The host file primitives behind FileLock and LogFile; platform_file_posix.cpp and
// platform_file_windows.cpp implement them.
#include "os/process.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

namespace voyemu::api::platform {
// An open file of the host, closed with the object.
struct File {
    os::FileHandle value = os::no_file;

    File() = default;
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File();
};

// Opens the lock file at path, creating it when create is set, and takes the cross-process lock of
// the supervisor without waiting; false when the file is absent, foreign or held.
bool lock(File &file, const std::filesystem::path &path, bool create);

// An opened log: whether it opened, whether it is an ordinary file of the current user with one
// link, and its identity and size when it is.
struct LogOpening {
    bool opened = false, ordinary = false;
    std::string identity;
    std::uint64_t size = 0;
};
LogOpening open_log(File &file, const std::filesystem::path &path);
// Reads at most size bytes at offset: the byte count, or -1 on failure.
long long read_at(const File &file, std::uint64_t offset, char *data, std::size_t size);
} // namespace voyemu::api::platform
