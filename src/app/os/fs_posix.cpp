#include "cli_messages.hpp"
#include "fs.hpp"
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>

// The file primitives that macOS and Linux share; fs_linux.cpp and fs_macos.cpp hold the renames.
namespace voyemu::os {
namespace {
[[noreturn]] void lock_file_failed(const std::filesystem::path &path, int error) {
    throw cli::LocalizedError(
            "LOCK_FILE", "error.lock_file",
            {{"path", path.string()}, {"cause", std::generic_category().message(error)}});
}
} // namespace

ExclusiveFile::ExclusiveFile(const std::filesystem::path &path, bool share_read)
    : handle_(::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600)),
      exists_(handle_ < 0 && errno == EEXIST) {
    (void)share_read; // POSIX does not exclude readers of an open file.
}

bool create_private_file(const std::filesystem::path &path) {
    const auto fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_NOFOLLOW | O_WRONLY | O_CLOEXEC,
                           0600);

    return fd >= 0 && ::close(fd) == 0;
}

std::FILE *open_exclusive_stream(const std::filesystem::path &path) {
    const auto fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_NOFOLLOW | O_RDWR | O_CLOEXEC, 0600);
    auto *stream = fd >= 0 ? ::fdopen(fd, "w+b") : nullptr;

    if (!stream && fd >= 0) {
        ::close(fd);
    }

    return stream;
}

bool truncate_stream(std::FILE *stream, std::uint64_t bytes) {
    return ::ftruncate(::fileno(stream), static_cast<off_t>(bytes)) == 0;
}

bool sync_stream(std::FILE *stream) {
    return ::fsync(::fileno(stream)) == 0;
}

OwnedFileObject owned_file_object(const std::filesystem::path &path, bool directory) {
    struct stat info{};

    if (::lstat(path.c_str(), &info) || info.st_uid != ::geteuid() ||
        (directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode) || info.st_nlink != 1)) {
        return {true, std::nullopt};
    }

    return {true, FileObject{std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
}

std::optional<std::string> read_private_file(const std::filesystem::path &path, std::size_t limit) {
    const auto file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (file < 0) {
        return std::nullopt;
    }

    struct Close {
        int file;

        ~Close() {
            ::close(file);
        }
    } close{file};
    struct stat info{};

    if (fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_uid != geteuid()) {
        return std::nullopt;
    }

    std::string data(limit, '\0');
    const auto bytes = ::read(file, data.data(), data.size());

    if (bytes < 0) {
        return std::nullopt;
    }

    data.resize(static_cast<std::size_t>(bytes));

    return data;
}

OwnedFile read_owned_file(const std::filesystem::path &path, std::size_t limit) {
    OwnedFile result;
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if (fd < 0) {
        return result;
    }

    struct Close {
        int fd;

        ~Close() {
            ::close(fd);
        }
    } close{fd};
    struct stat info{};

    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() ||
        info.st_nlink != 1 || info.st_size <= 0 ||
        static_cast<std::uint64_t>(info.st_size) > limit) {
        result.status = OwnedFile::Status::foreign;

        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(info.st_size));
    std::size_t offset = 0;

    while (offset < result.bytes.size()) {
        const auto count = ::read(fd, result.bytes.data() + offset, result.bytes.size() - offset);

        if (count <= 0) {
            result.status = OwnedFile::Status::truncated;

            return result;
        }

        offset += static_cast<std::size_t>(count);
    }

    result.status = OwnedFile::Status::read;

    return result;
}

std::filesystem::path home_directory() {
    const auto *home = std::getenv("HOME");

    return home ? std::filesystem::path(home) : std::filesystem::path();
}

bool private_to_user(const std::filesystem::path &path, bool directory) {
    struct stat st{};

    return !::lstat(path.c_str(), &st) && (!directory || S_ISDIR(st.st_mode)) &&
           st.st_uid == ::geteuid() && !(st.st_mode & 0077);
}

PositionalFile::PositionalFile(const std::filesystem::path &path, bool writable)
    : handle_(::open(path.c_str(), (writable ? O_RDWR : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC)) {}

PositionalFile::~PositionalFile() {
    if (handle_ >= 0) {
        ::close(static_cast<int>(handle_));
    }
}

bool PositionalFile::open() const {
    return handle_ >= 0;
}

std::optional<FileObject> PositionalFile::object() const {
    struct stat st{};

    if (handle_ < 0 || ::fstat(static_cast<int>(handle_), &st)) {
        return std::nullopt;
    }

    return FileObject{std::uint64_t(st.st_dev), std::uint64_t(st.st_ino)};
}

bool PositionalFile::read(std::uint64_t offset, char *data, std::size_t size) const {
    std::size_t done = 0;

    while (done < size) {
        const auto count = ::pread(static_cast<int>(handle_), data + done, size - done,
                                   static_cast<off_t>(offset + done));

        if (count < 0 && errno == EINTR) {
            continue;
        }

        if (count <= 0) {
            return false;
        }

        done += static_cast<std::size_t>(count);
    }

    return true;
}

bool PositionalFile::write(std::uint64_t offset, const char *data, std::size_t size) {
    std::size_t done = 0;

    while (done < size) {
        const auto count = ::pwrite(static_cast<int>(handle_), data + done, size - done,
                                    static_cast<off_t>(offset + done));

        if (count < 0 && errno == EINTR) {
            continue;
        }

        if (count <= 0) {
            return false;
        }

        done += static_cast<std::size_t>(count);
    }

    return true;
}

bool PositionalFile::sync() {
    return ::fsync(static_cast<int>(handle_)) == 0;
}

SparseFile create_sparse_file(const std::filesystem::path &path, std::uint64_t bytes) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

    if (fd < 0) {
        return SparseFile::create_failed;
    }

    const bool okay = ::ftruncate(fd, static_cast<off_t>(bytes)) == 0 && ::fsync(fd) == 0;
    ::close(fd);

    if (!okay) {
        ::unlink(path.c_str());

        return SparseFile::extent_failed;
    }

    return SparseFile::created;
}

FileSync sync_file(const std::filesystem::path &path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);

    if (fd < 0) {
        return FileSync::open_failed;
    }

    const auto synced = ::fsync(fd), closed = ::close(fd);

    return synced || closed ? FileSync::sync_failed : FileSync::synced;
}

JunctionResult create_junction(const std::filesystem::path &, const std::filesystem::path &) {
    return JunctionResult::unsupported;
}

ExclusiveFile::~ExclusiveFile() {
    close();
}

bool ExclusiveFile::open() const {
    return handle_ >= 0;
}

bool ExclusiveFile::write(const void *data, std::size_t size) {
    const auto *cursor = static_cast<const char *>(data);

    while (size) {
        const auto done = ::write(int(handle_), cursor, size);

        if (done < 0 && errno == EINTR) {
            continue;
        }

        if (done <= 0) {
            return false;
        }

        cursor += done;
        size -= std::size_t(done);
    }

    return true;
}

bool ExclusiveFile::sync() {
    return ::fsync(int(handle_)) == 0;
}

bool ExclusiveFile::close() {
    if (handle_ < 0) {
        return true;
    }

    const bool closed = ::close(int(handle_)) == 0;
    handle_ = -1;

    return closed;
}

FileLock::FileLock(const std::filesystem::path &path, bool exclusive)
    : handle_(::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600)) {
    if (handle_ < 0) {
        lock_file_failed(path, errno);
    }

    if (::flock(int(handle_), (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
        const auto error = errno;
        ::close(int(handle_));
        handle_ = -1;

        if (error != EWOULDBLOCK) {
            lock_file_failed(path, error);
        }
    }
}

FileLock::~FileLock() {
    if (handle_ >= 0) {
        ::close(int(handle_));
    }
}

bool FileLock::held() const {
    return handle_ >= 0;
}

namespace {
// The arrival order of the threads of this process waiting for each locked directory.
struct DirectoryQueues {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::deque<std::uint64_t>> waiting;
    std::uint64_t next = 0;
};

DirectoryQueues &directory_queues() {
    static DirectoryQueues queues;

    return queues;
}

// A blocking flock on a duplicate of the directory handle, which a helper thread waits in so the
// caller can give up at its deadline; an abandoned attempt releases the lock as soon as it gets it.
struct KernelWait {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    int error = 0;
};
} // namespace

DirectoryLock::DirectoryLock(const std::filesystem::path &directory,
                             std::chrono::steady_clock::time_point deadline)
    : handle_(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
    struct stat info{};

    if (handle_ < 0 || ::fstat(handle_, &info) != 0) {
        status_ = Status::unavailable;
        leave_queue();

        return;
    }

    device_ = static_cast<std::uint64_t>(info.st_dev);
    inode_ = static_cast<std::uint64_t>(info.st_ino);
    auto &queues = directory_queues();
    {
        std::unique_lock lock(queues.mutex);
        ticket_ = ++queues.next;
        auto &line = queues.waiting[{device_, inode_}];
        line.push_back(ticket_);

        if (!queues.changed.wait_until(lock, deadline, [&] {
                return line.front() == ticket_;
            })) {
            lock.unlock();
            status_ = Status::timeout;
            leave_queue();

            return;
        }
    }

    if (::flock(handle_, LOCK_EX | LOCK_NB) == 0) {
        status_ = Status::held;

        return;
    }

    const int waiting = errno == EWOULDBLOCK ? ::fcntl(handle_, F_DUPFD_CLOEXEC, 0) : -1;

    if (waiting < 0) {
        status_ = Status::failed;
        leave_queue();

        return;
    }

    std::shared_ptr<KernelWait> wait;
    std::thread helper;

    try {
        wait = std::make_shared<KernelWait>();
        helper = std::thread([wait, waiting] {
            int result = 0;

            while ((result = ::flock(waiting, LOCK_EX)) != 0 && errno == EINTR) {}

            const int error = result == 0 ? 0 : errno;
            // The caller keeps its own handle of the same open description; a caller that gave up
            // closed it, so closing this last handle releases the lock it may have just taken.
            ::close(waiting);
            std::lock_guard lock(wait->mutex);
            wait->done = true;
            wait->error = error;
            wait->changed.notify_all();
        });
    } catch (const std::exception &) {
        ::close(waiting);
        status_ = Status::failed;
        leave_queue();

        return;
    }

    helper.detach();
    std::unique_lock lock(wait->mutex);

    if (!wait->changed.wait_until(lock, deadline, [&] {
            return wait->done;
        })) {
        lock.unlock();
        status_ = Status::timeout;
        leave_queue();

        return;
    }

    status_ = wait->error == 0 ? Status::held : Status::failed;

    if (status_ != Status::held) {
        lock.unlock();
        leave_queue();
    }
}

DirectoryLock::~DirectoryLock() {
    if (status_ == Status::held) {
        leave_queue();
    }
}

DirectoryLock::Status DirectoryLock::status() const {
    return status_;
}

void DirectoryLock::leave_queue() {
    if (handle_ >= 0) {
        ::close(handle_);
        handle_ = -1;
    }

    if (!ticket_) {
        return;
    }

    auto &queues = directory_queues();
    {
        std::lock_guard lock(queues.mutex);
        const auto line = queues.waiting.find({device_, inode_});
        std::erase(line->second, ticket_);

        if (line->second.empty()) {
            queues.waiting.erase(line);
        }
    }
    ticket_ = 0;
    queues.changed.notify_all();
}

bool link_exclusive(const std::filesystem::path &existing, const std::filesystem::path &link) {
    return ::link(existing.c_str(), link.c_str()) == 0;
}

bool replace_file(const std::filesystem::path &from, const std::filesystem::path &to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
}

void resize_file(const std::filesystem::path &path, std::uint64_t bytes) {
    std::filesystem::resize_file(path, bytes);
}

bool reparse_point(const std::filesystem::path &) {
    return false;
}

bool foreign_owner(const std::filesystem::path &path) {
    struct stat status{};

    return ::stat(path.c_str(), &status) != 0 || status.st_uid != ::geteuid();
}

bool error_terminal() {
    return ::isatty(STDERR_FILENO) == 1;
}
} // namespace voyemu::os
