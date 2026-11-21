#pragma once
#include "model.hpp"
namespace voyemu {
// The identity, size and times of an owned regular file with expected_links links: the volume,
// file index and change time on Windows (quickboot_stamp_windows.cpp), the stable volume id, inode
// and times of a file of the current user on POSIX (quickboot_stamp_posix.cpp). Throws
// QUICKBOOT_FILE when the file cannot be inspected or is not such a file.
Json file_stamp(const std::filesystem::path &path, std::uintmax_t expected_links);
} // namespace voyemu
