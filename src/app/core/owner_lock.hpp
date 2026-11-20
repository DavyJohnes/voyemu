#pragma once
#include "os/fs.hpp"
#include <filesystem>
#include <memory>
#include <system_error>

namespace voyemu {
// The exclusive lock of the one process that installs or prepares a shared resource, such as the
// SDK or a profile. The OS releases it when that process exits, even when it is killed, so the next
// command never meets a stale lock; held() is false while another process owns it. A lock
// directory left by an earlier version at path is removed first.
inline std::unique_ptr<os::FileLock> owner_lock(const std::filesystem::path &path) {
    if (std::filesystem::is_directory(std::filesystem::symlink_status(path))) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    return std::make_unique<os::FileLock>(path, true);
}
} // namespace voyemu
