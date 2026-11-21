#pragma once
#include <filesystem>
namespace voyemu {
// Throws DEPS_METADATA unless file is an ordinary file owned by the current user that no other
// identity may modify: the POSIX owner and mode in deps_metadata_posix.cpp, the Windows owner and
// DACL in deps_metadata_windows.cpp.
void verify_owner_controlled_metadata(const std::filesystem::path &file);
} // namespace voyemu
