#pragma once
#include "os/fs.hpp"
#include <filesystem>
#include <memory>

namespace voyemu {
// The exclusive lock of the one process that installs or prepares a shared resource, such as the
// SDK or a profile. The OS releases it when that process exits, even when it is killed, so the next
// command never meets a stale lock; held() is false while another process owns it.
inline std::unique_ptr<os::FileLock> owner_lock(const std::filesystem::path &path) {
    return std::make_unique<os::FileLock>(path, true);
}
} // namespace voyemu
