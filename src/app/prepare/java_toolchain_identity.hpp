#pragma once
#include "model.hpp"
namespace voyemu {
// The identity of an immutable SDK or library input at the normalized absolute path, which shared
// inputs pin by exact SHA-256 rather than as exclusive mutable cache files: the Windows file index
// and times in java_toolchain_identity_windows.cpp; the POSIX inode, owner and times in
// java_toolchain_identity_posix.cpp, which also accepts a root-owned system JDK. Throws
// JAVA_TOOLCHAIN for a link, a non-regular file or, on POSIX, a foreign writable one.
Json immutable_input_identity(const std::filesystem::path &path);
} // namespace voyemu
