#include "admission.hpp"
#include "error.hpp"
#include "process.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::api {
namespace {
[[noreturn]] void fail() {
    throw VoyemuError("ADMISSION_STATE", "private receipt unavailable");
}

constexpr std::size_t payload_limit = 128 * 1024;
constexpr std::size_t record_limit = 8 * 1024 * 1024;
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

std::string digest(const std::string &text) {
    return sha256_bytes(text);
}

std::string key(const std::string &scope, const std::string &id) {
    if (scope.empty() || id.empty() || scope.size() > 4096 || id.size() > 4096) {
        throw VoyemuError("INVALID_REQUEST", "invalid admission key");
    }

    return digest(Json::array({scope, id}).dump());
}

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

void atomic_state(Locked &lock, const std::string &name, const Json &state) {
    auto text = state.dump();

    if (text.size() > record_limit) {
        throw VoyemuError("ADMISSION_EXTENT", "receipt exceeds 8 MiB");
    }

    auto pending = name + ".pending";
    Fd file(openat(lock.dir.value, pending.c_str(),
                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    write(file.value, text);

    if (renameat(lock.dir.value, pending.c_str(), lock.dir.value, (name + ".json").c_str()) ||
        fsync(lock.dir.value)) {
        fail();
    }
}

AdmissionResult existing(Locked &lock, const std::string &name,
                         const std::optional<std::string> &expected) {
    AdmissionResult result{false, name.substr(0, 32), "unknown", std::nullopt};
    auto raw = read(lock.reservation.value);
    Json reservation;

    try {
        reservation = Json::parse(raw);
    } catch (...) {
        return result;
    }

    if (!reservation.is_object() || reservation.value("version", 0) != 1 ||
        !reservation.contains("digest") || !reservation["digest"].is_string()) {
        return result;
    }

    if (expected && reservation["digest"] != *expected) {
        throw VoyemuError("REQUEST_CONFLICT", "request_id already used with a different payload");
    }

    result.status = "pending";
    int fd = openat(lock.dir.value, (name + ".json").c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if (fd < 0) {
        if (errno == ENOENT) {
            return result;
        }

        fail();
    }

    Fd terminal(fd);
    try {
        auto state = Json::parse(read(terminal.value));

        if (state.at("operation_id") != result.operation_id ||
            state.at("digest") != reservation["digest"]) {
            fail();
        }

        if (state.at("status") == "terminal") {
            result.response = state.at("response");
            result.status = "terminal";
        } else if (state.at("status") != "pending") {
            fail();
        }
    } catch (...) {
        fail();
    }

    return result;
}
} // namespace

AdmissionStore::AdmissionStore(std::filesystem::path directory)
    : directory_(std::filesystem::absolute(directory).lexically_normal()) {
    Fd directory_fd(open_store(directory_, true));
}

AdmissionResult AdmissionStore::begin(const std::string &scope, const std::string &request_id,
                                      const Json &payload) {
    auto name = key(scope, request_id);
    auto canonical = payload.dump();

    if (canonical.size() > payload_limit) {
        throw VoyemuError("INVALID_REQUEST", "action payload too large");
    }

    auto hash = digest(canonical);
    Locked lock(directory_, name, true);

    if (!lock.created) {
        return existing(lock, name, hash);
    }

    write(lock.reservation.value, Json{{"version", 1}, {"digest", hash}}.dump());
    atomic_state(
            lock, name,
            Json{{"operation_id", name.substr(0, 32)}, {"digest", hash}, {"status", "pending"}});

    return {true, name.substr(0, 32), "pending", std::nullopt};
}

std::optional<AdmissionResult> AdmissionStore::lookup(const std::string &scope,
                                                      const std::string &request_id) {
    auto name = key(scope, request_id);
    Fd dir(open_store(directory_, false));
    struct stat st{};

    if (fstatat(dir.value, (name + ".lock").c_str(), &st, AT_SYMLINK_NOFOLLOW)) {
        if (errno == ENOENT) {
            return std::nullopt;
        }

        fail();
    }

    Locked lock(directory_, name, false);

    return existing(lock, name, std::nullopt);
}

void AdmissionStore::complete(const std::string &scope, const std::string &request_id,
                              const std::string &operation_id, const Json &response) {
    auto name = key(scope, request_id);
    Locked lock(directory_, name, false);
    auto result = existing(lock, name, std::nullopt);

    if (result.operation_id != operation_id || result.status == "unknown") {
        fail();
    }

    if (result.response) {
        if (*result.response != response) {
            fail();
        }

        return;
    }

    auto reservation = Json::parse(read(lock.reservation.value));
    atomic_state(lock, name,
                 Json{{"operation_id", operation_id},
                      {"digest", reservation.at("digest")},
                      {"status", "terminal"},
                      {"response", response}});
}
} // namespace voyemu::api
