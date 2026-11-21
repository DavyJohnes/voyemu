#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
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
// one. held() is false while another holder conflicts; any other failure to open or lock path,
// such as a file of another user, throws LOCK_FILE naming path and its cause. The OS releases the
// lock when the holder closes it or exits.
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
#ifndef _WIN32
// Exclusive lock of a directory that the writers of its files take in turn until deadline: the
// threads of this process queue in arrival order and processes wait blocked in the kernel, so a
// writer that releases and takes the lock again does not starve a waiter. The OS releases the lock
// when its holder exits. Windows writers coordinate through a named mutex per file instead.
class DirectoryLock {
public:
    enum class Status {
        held,
        timeout,
        // The directory cannot be opened.
        unavailable,
        // The directory cannot be locked.
        failed
    };

    DirectoryLock(const std::filesystem::path &directory,
                  std::chrono::steady_clock::time_point deadline);
    ~DirectoryLock();
    DirectoryLock(const DirectoryLock &) = delete;
    DirectoryLock &operator=(const DirectoryLock &) = delete;
    Status status() const;

private:
    void leave_queue();

    int handle_ = -1;
    Status status_ = Status::failed;
    std::uint64_t device_ = 0, inode_ = 0, ticket_ = 0;
};
#endif
// Opens a new private file for reading and writing as a binary stdio stream; null when path
// already exists or cannot be created.
std::FILE *open_exclusive_stream(const std::filesystem::path &path);
// Sets the size of the file behind stream, whose buffer the caller flushed; false on failure.
bool truncate_stream(std::FILE *stream, std::uint64_t bytes);
// Flushes the file behind stream, whose buffer the caller flushed, to the device; false on failure.
bool sync_stream(std::FILE *stream);
// The outcome of create_junction.
enum class JunctionResult {
    created,
    // The host has no junctions.
    unsupported,
    // link already exists.
    exists,
    // target does not fit a reparse point; the empty link directory remains.
    target_too_long,
    // Setting the reparse point failed; no link remains.
    failed
};
// Creates link as a directory junction to target, which needs no symlink privilege; throws
// std::filesystem::filesystem_error when the link directory cannot be created.
JunctionResult create_junction(const std::filesystem::path &target,
                               const std::filesystem::path &link);
// The volume and the file index that tell a file apart from every other on the host.
struct FileObject {
    std::uint64_t volume = 0, index = 0;
};
// Whether path could be opened, and its FileObject when it is a directory or an ordinary file
// with one link, not a reparse point, and on POSIX owned by the effective user (Windows callers
// check the owner on a handle). POSIX inspects path without opening it, so opened stays true.
struct OwnedFileObject {
    bool opened = false;
    std::optional<FileObject> object;
};
OwnedFileObject owned_file_object(const std::filesystem::path &path, bool directory);
// The outcome of publish_new_name.
enum class Publication {
    published,
    // to exists or from cannot take its name.
    publish_failed,
    // POSIX published to as a second link but cannot remove from.
    release_failed
};
// Gives the file at from the new name to, failing when to exists: a move on Windows, a hard link
// and the removal of from on POSIX.
Publication publish_new_name(const std::filesystem::path &from, const std::filesystem::path &to);
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
// The bytes of an ordinary file with one link at path, without following a final link, owned by the
// effective user on POSIX, read with one call of at most limit bytes; none when any of that fails.
std::optional<std::string> read_private_file(const std::filesystem::path &path, std::size_t limit);
// The home directory of the user: USERPROFILE on Windows, HOME elsewhere; empty when unset.
std::filesystem::path home_directory();
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
