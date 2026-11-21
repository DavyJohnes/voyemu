#pragma once
// The host parts of platform_file.test.cpp: platform_file_posix.test.cpp and
// platform_file_windows.test.cpp.
#include <cstdint>
#include <filesystem>
#include <string>

// Prints the attributes, links, identity and owner of path on Windows; nothing on POSIX.
void metadata(const std::filesystem::path &path);
// Writes value to a new private file at path.
void write(const std::filesystem::path &path, const std::string &value);
// Gives path another owner; true when the change took effect.
bool change_owner(const std::filesystem::path &path);

// The exclusive lock the CLI supervisor holds on path: flock on POSIX, an unshared handle on
// Windows.
class SupervisorLock {
public:
    explicit SupervisorLock(const std::filesystem::path &path);
    ~SupervisorLock();
    SupervisorLock(const SupervisorLock &) = delete;
    SupervisorLock &operator=(const SupervisorLock &) = delete;

private:
    std::intptr_t handle_;
};
