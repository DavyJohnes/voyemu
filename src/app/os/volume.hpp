#pragma once
#include <filesystem>
#include <string>

namespace voyemu::os {
// Lowercase hex identity of the filesystem holding path that survives reboot and remount; empty
// when the host cannot name it. POSIX hosts only: Windows callers use the volume serial number.
std::string stable_volume_id(const std::filesystem::path &path);
} // namespace voyemu::os
