#include "error.hpp"
#include "os/stat_time.hpp"
#include "provider_files.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::runtime::detail {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("PROVIDER_INVALID", "invalid provider");
}

struct FD {
    int n;

    explicit FD(int v) : n(v) {
        if (n < 0) {
            invalid();
        }
    }

    ~FD() {
        close(n);
    }

    FD(const FD &) = delete;
};

void path_check(const std::filesystem::path &p) {
    if (!p.is_absolute() || p.lexically_normal() != p) {
        invalid();
    }

    std::filesystem::path prefix = "/";

    for (const auto &part : p.relative_path()) {
        prefix /= part;
        struct stat s{};

        if (lstat(prefix.c_str(), &s) || S_ISLNK(s.st_mode)) {
            invalid();
        }
    }
}
} // namespace

std::string provider_read(const std::filesystem::path &p, std::size_t limit, bool private_file) {
    path_check(p);
    FD directory(open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));

    for (const auto &part : p.parent_path().relative_path()) {
        FD next(openat(directory.n, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        std::swap(directory.n, next.n);
    }

    FD fd(openat(directory.n, p.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat a{}, b{};

    if (fstat(fd.n, &a) || !S_ISREG(a.st_mode) || a.st_uid != geteuid() || a.st_nlink != 1 ||
        (a.st_mode & 022) || a.st_size < 0 || static_cast<std::uint64_t>(a.st_size) > limit ||
        (private_file && (a.st_mode & 077))) {
        invalid();
    }

    std::string out;
    char bytes[8192];

    while (true) {
        auto n = read(fd.n, bytes, sizeof bytes);

        if (n < 0) {
            invalid();
        }

        if (!n) {
            break;
        }

        out.append(bytes, n);

        if (out.size() > limit) {
            invalid();
        }
    }

    if (fstat(fd.n, &b) || a.st_dev != b.st_dev || a.st_ino != b.st_ino || a.st_size != b.st_size ||
        os::modified_time(a).tv_sec != os::modified_time(b).tv_sec ||
        os::modified_time(a).tv_nsec != os::modified_time(b).tv_nsec ||
        os::changed_time(a).tv_sec != os::changed_time(b).tv_sec ||
        os::changed_time(a).tv_nsec != os::changed_time(b).tv_nsec ||
        out.size() != static_cast<std::size_t>(b.st_size)) {
        invalid();
    }

    struct stat current{};

    if (fstatat(directory.n, p.filename().c_str(), &current, AT_SYMLINK_NOFOLLOW) ||
        current.st_dev != a.st_dev || current.st_ino != a.st_ino) {
        invalid();
    }

    return out;
}

struct ProviderPinnedFile::State {
    std::filesystem::path path;
    std::size_t limit;
    bool private_access;
};

ProviderPinnedFile::ProviderPinnedFile(const std::filesystem::path &path, std::size_t limit,
                                       bool private_access)
    : state_(std::make_unique<State>(State{path, limit, private_access})) {}

ProviderPinnedFile::~ProviderPinnedFile() = default;

std::string ProviderPinnedFile::text() {
    return provider_read(state_->path, state_->limit, state_->private_access);
}

struct ProviderDirectory::State {};

ProviderDirectory::ProviderDirectory(const std::filesystem::path &path, bool private_leaf,
                                     bool create_leaf, bool fresh) {
    if (create_leaf && ::mkdir(path.c_str(), 0700) != 0 && (fresh || errno != EEXIST)) {
        invalid();
    }

    path_check(path);
    struct stat status{};

    if (::lstat(path.c_str(), &status) || !S_ISDIR(status.st_mode) ||
        (private_leaf && (status.st_uid != ::geteuid() || (status.st_mode & 077)))) {
        invalid();
    }
}

ProviderDirectory::~ProviderDirectory() = default;

void provider_request(const std::filesystem::path &pending, const std::filesystem::path &request,
                      const std::string &bytes) {
    {
        FD fd(open(pending.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        std::size_t pos = 0;

        while (pos < bytes.size()) {
            auto n = write(fd.n, bytes.data() + pos, bytes.size() - pos);

            if (n <= 0) {
                invalid();
            }

            pos += n;
        }

        if (fsync(fd.n)) {
            invalid();
        }
    }

    if (rename(pending.c_str(), request.c_str())) {
        invalid();
    }
}
} // namespace voyemu::runtime::detail
