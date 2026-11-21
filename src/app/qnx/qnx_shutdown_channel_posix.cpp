#include "os/net.hpp"
#include "qnx_shutdown_channel.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace voyemu {
struct QnxShutdownChannel::State {
    std::filesystem::path path;
    int fd = -1;
    struct stat parent{}, socket{};
    bool valid = false, socket_seen = false;

    // The private directory and, once seen, the socket file stay the ones first observed.
    void unchanged() {
        struct stat p{}, s{};
        qnx_shutdown_channel_require(
                ::lstat(path.parent_path().c_str(), &p) == 0 && S_ISDIR(p.st_mode) &&
                p.st_uid == ::geteuid() && !(p.st_mode & 0077) && p.st_dev == parent.st_dev &&
                p.st_ino == parent.st_ino && ::lstat(path.c_str(), &s) == 0 &&
                S_ISSOCK(s.st_mode) && s.st_uid == ::geteuid() && !(s.st_mode & 0022));

        if (socket_seen) {
            qnx_shutdown_channel_require(s.st_dev == socket.st_dev && s.st_ino == socket.st_ino);
        } else {
            socket = s;
            socket_seen = true;
        }
    }
};

QnxShutdownChannel::QnxShutdownChannel(const Json &endpoint) : state_(std::make_unique<State>()) {
    auto &s = *state_;

    if (!endpoint.is_string()) {
        return;
    }

    s.path = std::filesystem::path(endpoint.get<std::string>());
    std::error_code error;
    struct stat socket{};
    s.valid = s.path.is_absolute() && s.path.lexically_normal() == s.path &&
              std::filesystem::canonical(s.path.parent_path(), error) == s.path.parent_path() &&
              !error && ::lstat(s.path.parent_path().c_str(), &s.parent) == 0 &&
              S_ISDIR(s.parent.st_mode) && s.parent.st_uid == ::geteuid() &&
              !(s.parent.st_mode & 0077);
    errno = 0;
    s.valid = s.valid && ::lstat(s.path.c_str(), &socket) < 0 && errno == ENOENT;
}

QnxShutdownChannel::~QnxShutdownChannel() {
    close();
}

bool QnxShutdownChannel::valid() const {
    return state_->valid;
}

void QnxShutdownChannel::connect(std::int64_t pid, Deadline deadline) {
    auto &s = *state_;

    s.unchanged();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    qnx_shutdown_channel_require(fd >= 0);
    s.fd = fd;
    qnx_shutdown_channel_require(::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
                                 ::fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    qnx_shutdown_channel_require(os::suppress_sigpipe(fd));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto path = s.path.string();
    qnx_shutdown_channel_require(path.size() < sizeof address.sun_path);
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    const int connected = ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address);

    if (connected < 0) {
        qnx_shutdown_channel_require(errno == EINPROGRESS);
        wait(true, deadline);
        int error = 0;
        socklen_t bytes = sizeof error;
        qnx_shutdown_channel_require(::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &bytes) == 0 &&
                                     error == 0);
    }

    qnx_shutdown_channel_require(os::unix_peer_pid(fd) == pid);
}

void QnxShutdownChannel::verify(const std::function<void()> &) {
    state_->unchanged();
}

void QnxShutdownChannel::wait(bool writable, Deadline deadline) {
    for (;;) {
        const auto remaining = qnx_shutdown_channel_remaining(deadline);
        pollfd p{state_->fd, static_cast<short>(writable ? POLLOUT : POLLIN), 0};
        const int result = ::poll(&p, 1, static_cast<int>(remaining));

        if (result < 0 && errno == EINTR) {
            continue;
        }

        qnx_shutdown_channel_require(result > 0 && !(p.revents & (POLLERR | POLLNVAL)));

        return;
    }
}

int QnxShutdownChannel::receive(char *bytes, int count) {
    const auto result = ::recv(state_->fd, bytes, count, 0);

    if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return -1;
    }

    return static_cast<int>(result);
}

int QnxShutdownChannel::send(const char *bytes, int count) {
    const auto result = ::send(state_->fd, bytes, count, 0);

    if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return -1;
    }

    return static_cast<int>(result);
}

void QnxShutdownChannel::close() noexcept {
    if (state_ && state_->fd >= 0) {
        ::close(state_->fd);
        state_->fd = -1;
    }
}

void QnxShutdownChannel::finish() {
    close();
    struct stat socket{};
    errno = 0;

    if (::lstat(state_->path.c_str(), &socket) < 0) {
        qnx_shutdown_channel_require(errno == ENOENT);

        return;
    }

    state_->unchanged();
    qnx_shutdown_channel_require(::unlink(state_->path.c_str()) == 0);
}
} // namespace voyemu
