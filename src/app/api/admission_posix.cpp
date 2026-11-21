#include "admission_store.hpp"
#include "error.hpp"
#include "process.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::api::admission {
namespace {
struct Fd {
    int value;

    explicit Fd(int fd) : value(fd) {
        if (fd < 0) {
            fail();
        }
    }

    ~Fd() {
        ::close(value);
    }

    Fd(const Fd &) = delete;

    int release() {
        int fd = value;
        value = -1;

        return fd;
    }
};

void check(int fd, bool directory = false) {
    struct stat st{};

    if (fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 077) != 0 ||
        (directory ? (!S_ISDIR(st.st_mode) || (st.st_mode & 07777) != 0700)
                   : !S_ISREG(st.st_mode)) ||
        (!directory && st.st_nlink != 1)) {
        fail();
    }
}

void check_parent(int fd) {
    struct stat st{};

    if (fstat(fd, &st) || !S_ISDIR(st.st_mode) || (st.st_uid != 0 && st.st_uid != geteuid()) ||
        (st.st_mode & 022) != 0) {
        fail();
    }
}

int open_store(const std::filesystem::path &path, bool create) {
    Fd parent(open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));

    check_parent(parent.value);

    for (const auto &component : path.parent_path().relative_path()) {
        if (create) {
            if (mkdirat(parent.value, component.c_str(), 0700) == 0) {
                if (fsync(parent.value)) {
                    fail();
                }
            } else if (errno != EEXIST) {
                fail();
            }
        }

        Fd next(openat(parent.value, component.c_str(),
                       O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        check_parent(next.value);
        std::swap(parent.value, next.value);
    }

    auto leaf = path.filename();

    if (leaf.empty()) {
        fail();
    }

    if (create && mkdirat(parent.value, leaf.c_str(), 0700) != 0 && errno != EEXIST) {
        fail();
    }

    Fd directory(
            openat(parent.value, leaf.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    check(directory.value, true);

    if (create && fsync(parent.value)) {
        fail();
    }

    return directory.release();
}

std::string read(int fd) {
    check(fd);
    struct stat st{};

    if (fstat(fd, &st) || st.st_size < 0 ||
        static_cast<unsigned long long>(st.st_size) > record_limit) {
        fail();
    }

    std::string result(static_cast<std::size_t>(st.st_size), '\0');
    std::size_t offset = 0;

    while (offset < result.size()) {
        auto n = pread(fd, result.data() + offset, result.size() - offset,
                       static_cast<off_t>(offset));

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            fail();
        }

        offset += static_cast<std::size_t>(n);
    }

    return result;
}

void write(int fd, const std::string &text) {
    if (text.size() > record_limit) {
        fail();
    }

    std::size_t offset = 0;

    while (offset < text.size()) {
        auto n = pwrite(fd, text.data() + offset, text.size() - offset, static_cast<off_t>(offset));

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            fail();
        }

        offset += static_cast<std::size_t>(n);
    }

    if (ftruncate(fd, static_cast<off_t>(text.size())) || fsync(fd)) {
        fail();
    }
}

struct Locked {
    Fd dir;
    bool created = false;
    Fd reservation;

    Locked(const std::filesystem::path &path, const std::string &name, bool create)
        : dir(open_store(path, false)),
          reservation(open_reservation(dir.value, name, create, created)) {
        check(dir.value, true);
        check(reservation.value);

        if (flock(reservation.value, LOCK_EX)) {
            fail();
        }

        // The durable directory entry itself is a fail-closed reservation after a crash.
        if (fsync(dir.value)) {
            fail();
        }
    }

    static int open_reservation(int dir, const std::string &name, bool create, bool &created) {
        created = false;

        if (create) {
            int fd = openat(dir, (name + ".lock").c_str(),
                            O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

            if (fd >= 0) {
                created = true;

                return fd;
            }

            if (errno != EEXIST) {
                fail();
            }
        }

        return openat(dir, (name + ".lock").c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    }
};
} // namespace

struct Reservation::Impl {
    Locked lock;
    std::string name;

    Impl(const std::filesystem::path &store, const std::string &reserved, bool create)
        : lock(store, reserved, create), name(reserved) {}
};

void open_store_directory(const std::filesystem::path &store, bool create) {
    Fd directory_fd(open_store(store, create));
}

bool reservation_exists(const std::filesystem::path &store, const std::string &name) {
    Fd dir(open_store(store, false));
    struct stat st{};

    if (fstatat(dir.value, (name + ".lock").c_str(), &st, AT_SYMLINK_NOFOLLOW)) {
        if (errno == ENOENT) {
            return false;
        }

        fail();
    }

    return true;
}

Reservation::Reservation(const std::filesystem::path &store, const std::string &name, bool create)
    : impl_(std::make_unique<Impl>(store, name, create)) {}

Reservation::~Reservation() = default;

bool Reservation::created() const {
    return impl_->lock.created;
}

std::string Reservation::read_reservation() {
    return read(impl_->lock.reservation.value);
}

void Reservation::write_reservation(const std::string &text) {
    write(impl_->lock.reservation.value, text);
}

std::optional<std::string> Reservation::read_state() {
    const int fd = openat(impl_->lock.dir.value, (impl_->name + ".json").c_str(),
                          O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if (fd < 0) {
        if (errno == ENOENT) {
            return std::nullopt;
        }

        fail();
    }

    Fd terminal(fd);

    return read(terminal.value);
}

void Reservation::write_state(const std::string &text) {
    const auto pending = impl_->name + ".pending";
    Fd file(openat(impl_->lock.dir.value, pending.c_str(),
                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));

    write(file.value, text);

    if (renameat(impl_->lock.dir.value, pending.c_str(), impl_->lock.dir.value,
                 (impl_->name + ".json").c_str()) ||
        fsync(impl_->lock.dir.value)) {
        fail();
    }
}
} // namespace voyemu::api::admission
