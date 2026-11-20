#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace voyemu::os {
// Atomically renames from to to only when to does not exist; false leaves errno or
// GetLastError() describing the failure.
bool rename_exclusive(const std::filesystem::path &from, const std::filesystem::path &to);
#ifdef _WIN32
// Supplies a normalized absolute path to a Unicode Win32 filesystem call.
// Extended syntax keeps deeply nested private workspaces independent of MAX_PATH.
std::wstring win32_file_path(const std::filesystem::path &path);
#else
// Atomically exchanges two existing paths; Windows has no equivalent.
bool exchange_paths(const std::filesystem::path &first, const std::filesystem::path &second);
#endif
// Creates a new private file for writing; open() is false when path already exists or cannot be
// created. The destructor closes a file that close() did not.
class ExclusiveFile {
public:
    explicit ExclusiveFile(const std::filesystem::path &path, bool share_read = false);
    ~ExclusiveFile();
    ExclusiveFile(const ExclusiveFile &) = delete;
    ExclusiveFile &operator=(const ExclusiveFile &) = delete;
    bool open() const;

    // Whether creation failed because the name was already taken, rather than an I/O failure.
    bool already_exists() const {
        return exists_;
    }

    // Writes every byte; false on the first failed or empty write.
    bool write(const void *data, std::size_t size);
    // Flushes file data and metadata to the device.
    bool sync();
    bool close();

private:
    std::intptr_t handle_;
    bool exists_ = false;
};
// Non-blocking advisory lock on path, created if absent: shared holders exclude an exclusive
// one. held() is false while another holder conflicts or path cannot be opened. The OS releases
// the lock when the holder closes it or exits.
class FileLock {
public:
    FileLock(const std::filesystem::path &path, bool exclusive);
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
    bool held() const;

private:
    std::intptr_t handle_;
};
// Adds link as a second name for existing; fails when link exists.
bool link_exclusive(const std::filesystem::path &existing, const std::filesystem::path &link);
// Atomically replaces to with from, durably where the host supports it; false on failure.
bool replace_file(const std::filesystem::path &from, const std::filesystem::path &to);
// Sets the size of an existing file to bytes, as std::filesystem::resize_file does, throwing
// std::filesystem::filesystem_error on failure; growth leaves a sparse zero-filled hole where the
// filesystem supports one. The libc++ resize_file narrows the size to a 32-bit off_t on Windows.
void resize_file(const std::filesystem::path &path, std::uint64_t bytes);
// Whether path is a Windows reparse point; POSIX symlinks are checked through symlink_status.
bool reparse_point(const std::filesystem::path &path);
// Whether path belongs to another user than the effective one; false on Windows, whose callers
// check the owner on an open handle.
bool foreign_owner(const std::filesystem::path &path);
// Whether the standard error stream of this process is an interactive terminal.
bool error_terminal();

// True for the volume root of an absolute path: / on POSIX, a drive root such as C:\ or a UNC share
// root such as \\server\share on Windows. Ancestor walks over an absolute path stop there without
// inspecting it: its attributes describe the mount rather than the path (Wine reports Z:\ as a
// reparse point, and the server part of a UNC path has no attributes at all).
inline bool volume_root(const std::filesystem::path &path) {
    if (!path.is_absolute()) {
        return false;
    }

    const auto name = path.root_name().native();
    const auto separator = [](auto c) {
        return c == '/' || c == '\\';
    };
    const bool unc = name.size() > 2 && separator(name[0]) && separator(name[1]);
    std::size_t parts = 0;

    for (const auto &part : path.relative_path()) {
        if (!part.empty()) {
            ++parts;
        }
    }

    return parts == 0 || (unc && parts == 1);
}

// The path and each ancestor above it, nearest first, for ancestor symlink and reparse checks: an
// absolute walk ends below its volume root, a relative one at its first component.
inline std::vector<std::filesystem::path> ancestors(const std::filesystem::path &path) {
    std::vector<std::filesystem::path> result;

    for (auto at = path; !at.empty() && !volume_root(at); at = at.parent_path()) {
        result.push_back(at);

        if (at == at.parent_path()) {
            break;
        }
    }

    return result;
}

// Whether path or an ancestor below its volume root is a symlink.
inline bool linked_ancestor(const std::filesystem::path &path) {
    for (const auto &at : ancestors(path)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
            return true;
        }
    }

    return false;
}
} // namespace voyemu::os
