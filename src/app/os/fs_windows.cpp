#include "cli_messages.hpp"
#include "fs.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <system_error>
#include <winioctl.h>

namespace voyemu::os {
namespace {
[[noreturn]] void lock_file_failed(const std::filesystem::path &path, DWORD error) {
    throw cli::LocalizedError("LOCK_FILE", "error.lock_file",
                              {{"path", path.string()},
                               {"cause", std::system_category().message(static_cast<int>(error))}});
}
} // namespace

std::wstring win32_file_path(const std::filesystem::path &path) {
    auto native = path.native();

    if (native.size() < MAX_PATH - 12 || !path.is_absolute() || path != path.lexically_normal() ||
        native.starts_with(L"\\\\?\\") || native.starts_with(L"\\\\.\\")) {
        return native;
    }

    native = std::filesystem::path(path).make_preferred().native();

    if (native.starts_with(L"\\\\")) {
        return L"\\\\?\\UNC\\" + native.substr(2);
    }

    return L"\\\\?\\" + native;
}

bool rename_exclusive(const std::filesystem::path &from, const std::filesystem::path &to) {
    // Without MOVEFILE_REPLACE_EXISTING the move fails when the destination exists.
    return ::MoveFileExW(win32_file_path(from).c_str(), win32_file_path(to).c_str(),
                         MOVEFILE_WRITE_THROUGH) != 0;
}

ExclusiveFile::ExclusiveFile(const std::filesystem::path &path, bool share_read)
    : handle_(reinterpret_cast<std::intptr_t>(::CreateFileW(
              win32_file_path(path).c_str(), GENERIC_WRITE, share_read ? FILE_SHARE_READ : 0,
              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr))) {
    if (!open()) {
        const auto error = ::GetLastError();
        exists_ = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
    }
}

ExclusiveFile::~ExclusiveFile() {
    close();
}

bool ExclusiveFile::open() const {
    return reinterpret_cast<HANDLE>(handle_) != INVALID_HANDLE_VALUE;
}

bool ExclusiveFile::write(const void *data, std::size_t size) {
    const auto *cursor = static_cast<const char *>(data);

    while (size) {
        const auto chunk = static_cast<DWORD>(size < (1U << 30) ? size : (1U << 30));
        DWORD done = 0;

        if (!::WriteFile(reinterpret_cast<HANDLE>(handle_), cursor, chunk, &done, nullptr) ||
            !done) {
            return false;
        }

        cursor += done;
        size -= done;
    }

    return true;
}

bool ExclusiveFile::sync() {
    return ::FlushFileBuffers(reinterpret_cast<HANDLE>(handle_)) != 0;
}

bool ExclusiveFile::close() {
    if (!open()) {
        return true;
    }

    const bool closed = ::CloseHandle(reinterpret_cast<HANDLE>(handle_)) != 0;
    handle_ = reinterpret_cast<std::intptr_t>(INVALID_HANDLE_VALUE);

    return closed;
}

FileLock::FileLock(const std::filesystem::path &path, bool exclusive)
    : handle_(reinterpret_cast<std::intptr_t>(::CreateFileW(
              win32_file_path(path).c_str(), GENERIC_READ | GENERIC_WRITE,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr))) {
    OVERLAPPED range{};

    // A sharing violation is a passing opener of the same file, like a conflicting holder.
    if (!held()) {
        if (const auto error = ::GetLastError(); error != ERROR_SHARING_VIOLATION) {
            lock_file_failed(path, error);
        }

        return;
    }

    if (!::LockFileEx(reinterpret_cast<HANDLE>(handle_),
                      (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0) | LOCKFILE_FAIL_IMMEDIATELY, 0, 1,
                      0, &range)) {
        const auto error = ::GetLastError();
        ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
        handle_ = reinterpret_cast<std::intptr_t>(INVALID_HANDLE_VALUE);

        if (error != ERROR_LOCK_VIOLATION) {
            lock_file_failed(path, error);
        }
    }
}

FileLock::~FileLock() {
    if (held()) {
        ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
}

bool FileLock::held() const {
    return reinterpret_cast<HANDLE>(handle_) != INVALID_HANDLE_VALUE;
}

bool link_exclusive(const std::filesystem::path &existing, const std::filesystem::path &link) {
    return ::CreateHardLinkW(win32_file_path(link).c_str(), win32_file_path(existing).c_str(),
                             nullptr) != 0;
}

bool replace_file(const std::filesystem::path &from, const std::filesystem::path &to) {
    return ::MoveFileExW(win32_file_path(from).c_str(), win32_file_path(to).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

void resize_file(const std::filesystem::path &path, std::uint64_t bytes) {
    const auto file = ::CreateFileW(win32_file_path(path).c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw std::filesystem::filesystem_error(
                "resize_file", path,
                std::error_code(static_cast<int>(::GetLastError()), std::system_category()));
    }

    LARGE_INTEGER size{};
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(bytes);
    DWORD returned = 0;

    // POSIX growth leaves a hole; a filesystem without sparse files allocates the extent instead.
    if (::GetFileSizeEx(file, &size) && size.QuadPart < end.QuadPart) {
        ::DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
    }

    const bool resized = ::SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && ::SetEndOfFile(file);
    const auto error = ::GetLastError();
    ::CloseHandle(file);

    if (!resized) {
        throw std::filesystem::filesystem_error(
                "resize_file", path,
                std::error_code(static_cast<int>(error), std::system_category()));
    }
}

bool reparse_point(const std::filesystem::path &path) {
    const auto attributes = ::GetFileAttributesW(win32_file_path(path).c_str());

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool foreign_owner(const std::filesystem::path &) {
    return false;
}

bool error_terminal() {
    DWORD mode = 0;

    return GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode) != 0;
}
} // namespace voyemu::os
