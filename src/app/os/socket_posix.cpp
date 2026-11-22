#include "os/net.hpp"
#include "os/process.hpp"
#include "os/socket.hpp"
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/un.h>
#include <unistd.h>

namespace voyemu::os {
SocketLibrary::SocketLibrary() = default;

SocketLibrary::~SocketLibrary() = default;

unsigned socket_error() {
    return static_cast<unsigned>(errno);
}

bool socket_interrupted() {
    return errno == EINTR;
}

bool socket_would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool socket_connecting() {
    return errno == EINPROGRESS;
}

bool socket_reset() {
    return errno == ECONNRESET;
}

SocketHandle open_stream_socket(int family) {
    return ::socket(family, SOCK_STREAM, 0);
}

SocketHandle open_unix_listener(std::string_view path, int backlog) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return no_socket;
    }

    path.copy(address.sun_path, path.size());
    const auto socket = open_stream_socket(AF_UNIX);

    if (socket == no_socket) {
        return no_socket;
    }

    if (!bind_socket(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ||
        !listen_socket(socket, backlog)) {
        close_socket(socket);
        return no_socket;
    }

    return socket;
}

SocketHandle open_private_tcp_socket() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bool configured = fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
                      fcntl(fd, F_SETFL, O_NONBLOCK) == 0;

#ifdef SO_NOSIGPIPE
    int enabled = 1;
    configured = configured &&
                 setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#endif

    if (!configured) {
        if (fd >= 0) {
            ::close(fd);
        }

        return no_socket;
    }

    return fd;
}

void close_socket(SocketHandle socket) {
    ::close(static_cast<int>(socket));
}

bool make_private_socket(SocketHandle socket) {
    const int fd = static_cast<int>(socket);

    return ::fcntl(fd, F_SETFL, O_NONBLOCK) == 0 && ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
           suppress_sigpipe(fd);
}

SocketHandle accept_socket(SocketHandle socket) {
    return ::accept(static_cast<int>(socket), nullptr, nullptr);
}

int poll_socket(SocketHandle socket, bool writing, int timeout_ms) {
    pollfd ready{static_cast<int>(socket), static_cast<short>(POLLIN | (writing ? POLLOUT : 0)), 0};

    return ::poll(&ready, 1, timeout_ms);
}

bool bind_socket(SocketHandle socket, const sockaddr *address, SocketLength size) {
    return ::bind(static_cast<int>(socket), address, size) == 0;
}

bool connect_socket(SocketHandle socket, const sockaddr *address, SocketLength size) {
    return ::connect(static_cast<int>(socket), address, size) == 0;
}

bool socket_name(SocketHandle socket, sockaddr *address, SocketLength *size) {
    return ::getsockname(static_cast<int>(socket), address, size) == 0;
}

bool peer_name(SocketHandle socket, sockaddr *address, SocketLength *size) {
    return ::getpeername(static_cast<int>(socket), address, size) == 0;
}

bool set_nonblocking_socket(SocketHandle socket) {
    const int fd = static_cast<int>(socket);

    const int flags = ::fcntl(fd, F_GETFL, 0);

    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
           ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

bool suppress_socket_signal(SocketHandle socket) {
    return suppress_sigpipe(static_cast<int>(socket));
}

int pending_socket_error(SocketHandle socket) {
    int error = 0;
    socklen_t size = sizeof(error);

    return ::getsockopt(static_cast<int>(socket), SOL_SOCKET, SO_ERROR, &error, &size) ? -1 : error;
}

long long send_socket(SocketHandle socket, const char *data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif

    return ::send(static_cast<int>(socket), data, size, flags);
}

long long receive_socket(SocketHandle socket, char *data, std::size_t size) {
    return ::recv(static_cast<int>(socket), data, size, 0);
}

bool listen_socket(SocketHandle socket, int backlog) {
    return ::listen(static_cast<int>(socket), backlog) == 0;
}

bool shutdown_socket(SocketHandle socket, bool receiving) {
    return ::shutdown(static_cast<int>(socket), receiving ? SHUT_RDWR : SHUT_WR) == 0;
}

bool set_socket_buffer(SocketHandle socket, bool sending, int bytes) {
    return ::setsockopt(static_cast<int>(socket), SOL_SOCKET, sending ? SO_SNDBUF : SO_RCVBUF,
                        &bytes, sizeof bytes) == 0;
}

bool reuse_address(SocketHandle socket) {
    // Completed SDK connections in TIME_WAIT are reusable; REUSEPORT would hide a live listener.
    int enabled = 1;

    return ::setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_REUSEADDR, &enabled,
                        sizeof enabled) == 0;
}

bool exclusive_address(SocketHandle) {
    return true;
}

int wait_socket(SocketHandle socket, bool writing, long long timeout_us) {
    pollfd selected{static_cast<int>(socket), static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    const int result = ::poll(&selected, 1, static_cast<int>((timeout_us + 999) / 1000));

    if (result > 0) {
        return (selected.revents & selected.events) ? 1 : -2;
    }

    return result;
}

std::optional<SocketRefusal> loopback_listener(int family, const sockaddr *address,
                                               SocketLength size, unsigned, TcpPortProbe *) {
    // Reject live listeners before the SDK-reusable bind; wildcard REUSEADDR must not hide them.
    const int probe = ::socket(family, SOCK_STREAM, 0);

    if (probe < 0) {
        return SocketRefusal{"probe_socket", socket_error()};
    }

    const SocketCloser probe_cleanup{probe};

    if (::fcntl(probe, F_SETFL, O_NONBLOCK) != 0) {
        return SocketRefusal{"probe_nonblocking", socket_error()};
    }

    int connected = ::connect(probe, address, size);
    int error = connected == 0 ? 0 : errno;

    if (connected < 0 && error == EINPROGRESS) {
        pollfd item{probe, POLLOUT, 0};
        int waited;

        do {
            waited = ::poll(&item, 1, 100);
        } while (waited < 0 && errno == EINTR);

        socklen_t length = sizeof error;

        if (waited <= 0 || ::getsockopt(probe, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
            error = ETIMEDOUT;
        }
    }

    if (error != ECONNREFUSED) {
        return SocketRefusal{"connect", static_cast<unsigned>(error)};
    }

    return std::nullopt;
}

LoopbackHolder loopback_listener_holder(std::uint64_t pid, unsigned port) {
    for (const auto &socket : process_tcp_sockets(static_cast<std::int64_t>(pid))) {
        if (socket.state == TcpSocket::State::listen && socket.local.ipv4 &&
            socket.local.port == port && socket.local.address4 == htonl(INADDR_LOOPBACK)) {
            return LoopbackHolder::owner;
        }
    }

    return LoopbackHolder::absent;
}

LoopbackHolder loopback_connection_holder(std::uint64_t pid, unsigned local_port,
                                          unsigned remote_port) {
    for (const auto &socket : process_tcp_sockets(static_cast<std::int64_t>(pid))) {
        if (socket.state == TcpSocket::State::established && socket.local.ipv4 &&
            socket.local.address4 == htonl(INADDR_LOOPBACK) &&
            socket.remote.address4 == htonl(INADDR_LOOPBACK) && socket.local.port == local_port &&
            socket.remote.port == remote_port) {
            return LoopbackHolder::owner;
        }
    }

    return LoopbackHolder::absent;
}
} // namespace voyemu::os
