#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace voyemu::api {
// Shares the supervisor's cross-process lock mechanism; never creates on probe.
class FileLock {
public:
    explicit FileLock(const std::filesystem::path &path);
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;

private:
    friend bool file_lock_released(const std::filesystem::path &path);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool file_lock_released(const std::filesystem::path &path);
// Metadata and bounded reads refer to the same no-follow, current-user-owned handle.
class LogFile {
public:
    explicit LogFile(const std::filesystem::path &path);
    ~LogFile();
    LogFile(const LogFile &) = delete;
    LogFile &operator=(const LogFile &) = delete;
    std::string identity() const;
    std::uint64_t size() const;
    std::string read(std::uint64_t offset) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voyemu::api
