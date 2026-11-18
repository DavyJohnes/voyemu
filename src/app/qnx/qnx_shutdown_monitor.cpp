#include "qnx_shutdown_monitor.hpp"
#include "error.hpp"
#include "os/net.hpp"
#include <chrono>
#include <functional>
#include <stdexcept>
#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
namespace voyemu {
struct QnxShutdownMonitor::Impl {
    Json endpoint;
    std::uint64_t generation = 0;
    std::int64_t pid = 0;
    bool subscribed = false, eof = false, fault = false, shutdown = false, guest = false;
    std::string reason, carry;
    using Deadline = std::chrono::steady_clock::time_point;

    void require(bool yes) {
        if (!yes) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "owned QMP channel unverified");
        }
    }

    void event(const Json &value) {
        if (!value.is_object()) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "malformed event");
        }

        if (value.value("event", std::string{}) != "SHUTDOWN") {
            return;
        }

        const auto &data = value.at("data");

        if (!data.is_object() || !data.contains("guest") || !data.at("guest").is_boolean() ||
            !data.contains("reason") || !data.at("reason").is_string()) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "malformed shutdown cause");
        }

        const auto why = data.at("reason").get<std::string>();
        const bool by_guest = data.at("guest").get<bool>();

        if (shutdown && (reason != why || guest != by_guest)) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "conflicting shutdown causes");
        }

        shutdown = true;
        reason = why;
        guest = by_guest;
    }

    std::string line(Deadline deadline) {
        for (;;) {
            const auto end = carry.find('\n');

            if (end != carry.npos) {
                auto out = carry.substr(0, end);
                carry.erase(0, end + 1);

                return out;
            }

            wait_readable(deadline);
            char bytes[4096];
            const auto count = recv_into(bytes, sizeof bytes);

            if (count < 0) {
                continue;
            }

            require(count > 0 && carry.size() + static_cast<std::size_t>(count) <= 16384);
            carry.append(bytes, static_cast<std::size_t>(count));
        }
    }

    void send(std::string_view message, Deadline deadline) {
        while (!message.empty()) {
            wait_writable(deadline);
            const auto count = send_out(message.data(), static_cast<int>(message.size()));

            if (count < 0) {
                continue;
            }

            require(count > 0);
            message.remove_prefix(static_cast<std::size_t>(count));
        }
    }

#ifndef _WIN32
    std::filesystem::path path;
    int fd = -1;
    struct stat parent{}, socket{};
    bool socket_seen = false;

    ~Impl() {
        if (fd >= 0) {
            ::close(fd);
        }
    }

    void unchanged() {
        struct stat p{}, s{};
        require(::lstat(path.parent_path().c_str(), &p) == 0 && S_ISDIR(p.st_mode) &&
                p.st_uid == ::geteuid() && !(p.st_mode & 0077) && p.st_dev == parent.st_dev &&
                p.st_ino == parent.st_ino && ::lstat(path.c_str(), &s) == 0 &&
                S_ISSOCK(s.st_mode) && s.st_uid == ::geteuid() && !(s.st_mode & 0022));

        if (socket_seen) {
            require(s.st_dev == socket.st_dev && s.st_ino == socket.st_ino);
        } else {
            socket = s;
            socket_seen = true;
        }
    }

    void wait(short events, Deadline deadline) {
        for (;;) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                                           deadline - std::chrono::steady_clock::now())
                                           .count();
            require(remaining > 0);
            pollfd p{fd, events, 0};
            const int result = ::poll(&p, 1, static_cast<int>(remaining));

            if (result < 0 && errno == EINTR) {
                continue;
            }

            require(result > 0 && !(p.revents & (POLLERR | POLLNVAL)));

            return;
        }
    }

    void wait_readable(Deadline deadline) {
        wait(POLLIN, deadline);
    }

    void wait_writable(Deadline deadline) {
        wait(POLLOUT, deadline);
    }

    int recv_into(char *bytes, int count) {
        const auto result = ::recv(fd, bytes, count, 0);

        if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1;
        }

        return static_cast<int>(result);
    }

    int send_out(const char *bytes, int count) {
        const auto result = ::send(fd, bytes, count, 0);

        if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1;
        }

        return static_cast<int>(result);
    }
#else
    SOCKET socket_ = INVALID_SOCKET;
    bool wsa = false;

    ~Impl() {
        if (socket_ != INVALID_SOCKET) {
            ::closesocket(socket_);
        }

        if (wsa) {
            WSACleanup();
        }
    }

    void wait(bool writable, Deadline deadline) {
        for (;;) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                                           deadline - std::chrono::steady_clock::now())
                                           .count();
            require(remaining > 0);
            fd_set selected;
            FD_ZERO(&selected);
            FD_SET(socket_, &selected);
            timeval timeout{static_cast<long>(remaining / 1000),
                            static_cast<long>((remaining % 1000) * 1000)};
            const int result = ::select(0, writable ? nullptr : &selected,
                                        writable ? &selected : nullptr, nullptr, &timeout);

            if (result < 0 && WSAGetLastError() == WSAEINTR) {
                continue;
            }

            require(result > 0);

            return;
        }
    }

    void wait_readable(Deadline deadline) {
        wait(false, deadline);
    }

    void wait_writable(Deadline deadline) {
        wait(true, deadline);
    }

    int recv_into(char *bytes, int count) {
        const auto result = ::recv(socket_, bytes, count, 0);

        if (result < 0) {
            const auto error = WSAGetLastError();

            if (error == WSAEINTR || error == WSAEWOULDBLOCK) {
                return -1;
            }
        }

        return static_cast<int>(result);
    }

    int send_out(const char *bytes, int count) {
        const auto result = ::send(socket_, bytes, count, 0);

        if (result < 0) {
            const auto error = WSAGetLastError();

            if (error == WSAEINTR || error == WSAEWOULDBLOCK) {
                return -1;
            }
        }

        return static_cast<int>(result);
    }
#endif
};

QnxShutdownMonitor::QnxShutdownMonitor(Json endpoint, std::uint64_t generation)
    : impl_(std::make_unique<Impl>()) {
    impl_->endpoint = std::move(endpoint);
    impl_->generation = generation;
#ifndef _WIN32
    auto &p = *impl_;
    p.require(generation > 0 && p.endpoint.is_string());
    p.path = std::filesystem::path(p.endpoint.get<std::string>());
    struct stat socket{};
    p.require(p.path.is_absolute() && p.path.lexically_normal() == p.path &&
              std::filesystem::canonical(p.path.parent_path()) == p.path.parent_path() &&
              ::lstat(p.path.parent_path().c_str(), &p.parent) == 0 && S_ISDIR(p.parent.st_mode) &&
              p.parent.st_uid == ::geteuid() && !(p.parent.st_mode & 0077));
    errno = 0;
    p.require(::lstat(p.path.c_str(), &socket) < 0 && errno == ENOENT);
#else
    auto &p = *impl_;
    p.require(generation > 0 && p.endpoint.is_object() &&
              p.endpoint.value("host", std::string{}) == "127.0.0.1" &&
              p.endpoint.contains("port") && p.endpoint.at("port").is_number_unsigned() &&
              p.endpoint.at("port").get<std::uint64_t>() >= 1024 &&
              p.endpoint.at("port").get<std::uint64_t>() <= 65535);
#endif
}

QnxShutdownMonitor::~QnxShutdownMonitor() = default;

void QnxShutdownMonitor::connect_owned(std::int64_t pid,
                                       const std::function<void()> &verify_owner) {
#ifndef _WIN32
    auto &p = *impl_;
    (void)verify_owner;

    if (p.subscribed) {
        p.require(p.pid == pid);

        return;
    }

    p.require(pid > 1 && !p.eof && !p.fault);
    p.unchanged();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    p.require(fd >= 0);
    try {
        p.fd = fd;
        p.require(::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && ::fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
        p.require(os::suppress_sigpipe(fd));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path = p.path.string();
        p.require(path.size() < sizeof address.sun_path);
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        const int connected = ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address);

        if (connected < 0) {
            p.require(errno == EINPROGRESS);
            p.wait(POLLOUT, deadline);
            int error = 0;
            socklen_t bytes = sizeof error;
            p.require(::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &bytes) == 0 && error == 0);
        }

        p.require(os::unix_peer_pid(fd) == pid);
        p.unchanged();
        const auto greeting = Json::parse(p.line(deadline));
        const auto &version = greeting.at("QMP").at("version").at("qemu");
        p.require(version.at("major") == 11 && version.at("minor") == 1 &&
                  version.at("micro") == 2);
        p.send("{\"execute\":\"qmp_capabilities\",\"id\":\"shutdown-events\"}\n", deadline);
        bool acknowledged = false;

        for (unsigned i = 0; i < 32; ++i) {
            const auto reply = Json::parse(p.line(deadline));

            if (reply.value("id", std::string{}) == "shutdown-events") {
                p.require(reply.contains("return") && !reply.contains("error"));
                acknowledged = true;
                break;
            }

            p.event(reply);
        }

        p.require(acknowledged);
        p.unchanged();
        p.pid = pid;
        p.subscribed = true;
    } catch (...) {
        p.fd = -1;
        ::close(fd);
        p.carry.clear();
        p.shutdown = false;
        p.reason.clear();
        throw;
    }
#else
    auto &p = *impl_;

    if (p.subscribed) {
        p.require(p.pid == pid);

        return;
    }

    p.require(pid > 1 && !p.eof && !p.fault);
    WSADATA data{};
    p.require(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    p.wsa = true;
    const auto port = p.endpoint.at("port").get<unsigned>();
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    p.require(socket != INVALID_SOCKET);
    try {
        p.socket_ = socket;
        u_long nonblocking = 1;
        p.require(ioctlsocket(socket, FIONBIO, &nonblocking) == 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(port));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        const int connected = ::connect(socket, reinterpret_cast<sockaddr *>(&address),
                                        sizeof address);

        if (connected != 0) {
            p.require(WSAGetLastError() == WSAEWOULDBLOCK);
            p.wait_writable(deadline);
            int error = 0;
            int bytes = sizeof error;
            p.require(::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                                   &bytes) == 0 &&
                      error == 0);
        }

        if (verify_owner) {
            verify_owner();
        }

        const auto greeting = Json::parse(p.line(deadline));
        const auto &version = greeting.at("QMP").at("version").at("qemu");
        p.require(version.at("major") == 11 && version.at("minor") == 1 &&
                  version.at("micro") == 2);
        p.send("{\"execute\":\"qmp_capabilities\",\"id\":\"shutdown-events\"}\n", deadline);
        bool acknowledged = false;

        for (unsigned i = 0; i < 32; ++i) {
            const auto reply = Json::parse(p.line(deadline));

            if (reply.value("id", std::string{}) == "shutdown-events") {
                p.require(reply.contains("return") && !reply.contains("error"));
                acknowledged = true;
                break;
            }

            p.event(reply);
        }

        p.require(acknowledged);

        if (verify_owner) {
            verify_owner();
        }

        p.pid = pid;
        p.subscribed = true;
    } catch (...) {
        p.socket_ = INVALID_SOCKET;
        ::closesocket(socket);
        p.carry.clear();
        p.shutdown = false;
        p.reason.clear();
        throw;
    }
#endif
}

void QnxShutdownMonitor::drain() {
    auto &p = *impl_;

    if (!p.subscribed || p.eof || p.fault) {
        return;
    }

    try {
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            for (;;) {
                const auto end = p.carry.find('\n');

                if (end == p.carry.npos) {
                    break;
                }

                const auto value = Json::parse(p.carry.substr(0, end));
                p.carry.erase(0, end + 1);
                p.event(value);
            }

            char bytes[4096];
            const auto count = p.recv_into(bytes, sizeof bytes);

            if (count > 0) {
                p.require(p.carry.size() + static_cast<std::size_t>(count) <= 16384);
                p.carry.append(bytes, static_cast<std::size_t>(count));
                continue;
            }

            if (count == 0) {
                p.eof = true;
                p.require(p.carry.empty());

                return;
            }

            if (count < 0) {
                return;
            }

            p.require(false);
        }

        p.require(false);
    } catch (...) {
        p.fault = true;
        p.shutdown = false;
        p.reason.clear();
    }
}

bool QnxShutdownMonitor::guest_reset(std::int64_t pid, std::uint64_t generation) const {
    const auto &p = *impl_;

    return p.subscribed && !p.fault && p.shutdown && p.guest && p.reason == "guest-reset" &&
           p.pid == pid && p.generation == generation;
}

Json QnxShutdownMonitor::evidence() const {
    const auto &p = *impl_;

    return {{"source", "owned_QMP_SHUTDOWN"},
            {"pid", p.pid},
            {"generation", p.generation},
            {"subscribed", p.subscribed},
            {"shutdown", p.shutdown},
            {"guest", p.guest},
            {"reason", p.reason},
            {"fault", p.fault},
            {"eof", p.eof}};
}

void QnxShutdownMonitor::finish_owned_exit(std::int64_t pid, std::uint64_t generation) {
#ifndef _WIN32
    auto &p = *impl_;
    p.require((!p.subscribed || p.pid == pid) && p.generation == generation);

    if (p.fd >= 0) {
        ::close(p.fd);
        p.fd = -1;
    }

    struct stat socket{};
    errno = 0;

    if (::lstat(p.path.c_str(), &socket) < 0) {
        p.require(errno == ENOENT);

        return;
    }

    p.unchanged();
    p.require(::unlink(p.path.c_str()) == 0);
#else
    auto &p = *impl_;
    p.require((!p.subscribed || p.pid == pid) && p.generation == generation);

    if (p.socket_ != INVALID_SOCKET) {
        ::closesocket(p.socket_);
        p.socket_ = INVALID_SOCKET;
    }
#endif
}
} // namespace voyemu
