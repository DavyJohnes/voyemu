#include "sysdeps.h"

#include <atomic>
#include <chrono>
#include <climits>
#include <map>
#include <mutex>

#include "adb_utils.h"
#include "sysdeps/uio.h"

#undef strerror
#undef accept
#undef setsockopt

namespace {
class Socket {
public:
    explicit Socket(SOCKET value) : value(value) {}

    ~Socket() {
        if (value != INVALID_SOCKET) {
            closesocket(value);
        }
    }

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    SOCKET value;
    std::atomic<bool> block_on_io{true};
    std::atomic<bool> read_closed{false};
    std::atomic<bool> write_closed{false};
};

struct Winsock {
    Winsock() {
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~Winsock() {
        if (ready) {
            WSACleanup();
        }
    }

    WSADATA data{};
    bool ready = false;
};

struct Registry {
    std::mutex mutex;
    std::map<int, std::shared_ptr<Socket>> sockets;
    int next = 0;
};

Registry &registry() {
    static Registry value;

    return value;
}

std::shared_ptr<Socket> lookup(int fd) {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    auto it = state.sockets.find(fd);

    if (it != state.sockets.end()) {
        return it->second;
    }

    errno = EBADF;

    return {};
}

int set_socket_error(int error) {
    switch (error) {
    case WSAEINTR:
        errno = EINTR;
        break;
    case WSAEWOULDBLOCK:
        errno = EAGAIN;
        break;
    case WSAEINPROGRESS:
        errno = EINPROGRESS;
        break;
    case WSAECONNRESET:
        errno = ECONNRESET;
        break;
    case WSAECONNABORTED:
        errno = ECONNABORTED;
        break;
    case WSAECONNREFUSED:
        errno = ECONNREFUSED;
        break;
    case WSAESHUTDOWN:
        errno = EPIPE;
        break;
    case WSAENOTCONN:
        errno = ENOTCONN;
        break;
    case WSAENOTSOCK:
        errno = EBADF;
        break;
    default:
        errno = EIO;
        break;
    }

    return -1;
}

int result(int rc) {
    return rc == SOCKET_ERROR ? set_socket_error(WSAGetLastError()) : rc;
}

bool make_nonblocking(SOCKET socket) {
    u_long nonblocking = 1;

    return result(ioctlsocket(socket, FIONBIO, &nonblocking)) == 0;
}

int insert(std::shared_ptr<Socket> socket) {
    if (!make_nonblocking(socket->value)) {
        return -1;
    }

    auto &state = registry();
    std::lock_guard lock(state.mutex);

    if (state.next == INT_MAX) {
        errno = EMFILE;

        return -1;
    }

    const int fd = state.next++;
    state.sockets.emplace(fd, std::move(socket));

    return fd;
}

SOCKET create_socket(int domain, int type, int protocol) {
    static Winsock winsock;

    if (!winsock.ready) {
        errno = EIO;

        return INVALID_SOCKET;
    }

    return WSASocketW(domain, type, protocol, nullptr, 0,
                      WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
}

sockaddr_in loopback(uint16_t port) {
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    return address;
}

// Winsock shutdown need not release another thread's synchronous send or
// receive. Keep all registered handles nonblocking and emulate blocking waits
// at the adapter boundary so the operation watchdog can always end them.
template <typename Transfer> int transfer(borrowed_fd fd, bool writing, Transfer operation) {
    auto socket = lookup(fd.get());

    if (!socket) {
        return -1;
    }

    auto &stopped = writing ? socket->write_closed : socket->read_closed;
    const short events = writing ? POLLOUT : POLLIN;

    while (!stopped.load()) {
        const int count = result(operation(socket->value));

        if (count >= 0 || errno != EAGAIN || !socket->block_on_io.load()) {
            return count;
        }

        WSAPOLLFD wait{socket->value, events, 0};

        if (result(WSAPoll(&wait, 1, 50)) < 0 && errno != EINTR) {
            return -1;
        }
    }

    if (!writing) {
        return 0;
    }

    errno = EPIPE;

    return -1;
}

int stop_io(Socket &socket, int direction) {
    if (direction != SD_SEND) {
        socket.read_closed = true;
    }

    if (direction != SD_RECEIVE) {
        socket.write_closed = true;
    }

    return result(shutdown(socket.value, direction));
}
} // namespace

int adb_register_socket(SOCKET socket) {
    if (socket == INVALID_SOCKET) {
        return set_socket_error(WSAGetLastError());
    }

    return insert(std::make_shared<Socket>(socket));
}

int adb_socket(int domain, int type, int protocol) {
    return adb_register_socket(create_socket(domain, type, protocol));
}

int adb_close(int fd) {
    auto &state = registry();
    std::shared_ptr<Socket> socket;
    {
        std::lock_guard lock(state.mutex);
        auto it = state.sockets.find(fd);

        if (it == state.sockets.end()) {
            errno = EBADF;

            return -1;
        }

        socket = std::move(it->second);
        state.sockets.erase(it);
    }

    // Close can race with an outstanding I/O lease. Shutdown wakes that call
    // while the last lease retains the OS handle until it finishes.
    stop_io(*socket, SD_BOTH);

    return 0;
}

int adb_shutdown(borrowed_fd fd, int direction) {
    auto socket = lookup(fd.get());

    return socket ? stop_io(*socket, direction) : -1;
}

int adb_read(borrowed_fd fd, void *data, int length) {
    return transfer(fd, false, [&](SOCKET socket) {
        return recv(socket, static_cast<char *>(data), length, 0);
    });
}

int adb_write(borrowed_fd fd, const void *data, int length) {
    return transfer(fd, true, [&](SOCKET socket) {
        return send(socket, static_cast<const char *>(data), length, 0);
    });
}

ssize_t adb_writev(borrowed_fd fd, const adb_iovec *iov, int count) {
    auto socket = lookup(fd.get());

    if (!socket) {
        return -1;
    }

    std::vector<WSABUF> buffers;
    buffers.reserve(count);

    for (int i = 0; i < count; ++i) {
        buffers.push_back({iov[i].iov_len, static_cast<char *>(iov[i].iov_base)});
    }

    DWORD bytes = 0;

    return result(WSASend(socket->value, buffers.data(), static_cast<DWORD>(buffers.size()), &bytes,
                          0, nullptr, nullptr)) < 0
                   ? -1
                   : static_cast<ssize_t>(bytes);
}

bool set_file_block_mode(borrowed_fd fd, bool block) {
    auto socket = lookup(fd.get());

    if (!socket) {
        return false;
    }

    socket->block_on_io = block;

    return true;
}

int adb_setsockopt(borrowed_fd fd, int level, int option, const void *value, socklen_t length) {
    auto socket = lookup(fd.get());

    return socket ? result(::setsockopt(socket->value, level, option,
                                        static_cast<const char *>(value), length))
                  : -1;
}

int adb_poll(adb_pollfd *fds, size_t count, int timeout) {
    std::vector<std::shared_ptr<Socket>> sockets;
    std::vector<WSAPOLLFD> pollfds;

    sockets.reserve(count);
    pollfds.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto socket = lookup(fds[i].fd);

        if (!socket) {
            return -1;
        }

        pollfds.push_back({socket->value, fds[i].events, 0});
        sockets.push_back(std::move(socket));
    }

    const int ready = result(WSAPoll(pollfds.data(), static_cast<ULONG>(count), timeout));

    for (size_t i = 0; i < count; ++i) {
        fds[i].revents = pollfds[i].revents;
    }

    return ready;
}

int adb_socketpair(int fds[2]) {
    Socket listener(create_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));

    if (listener.value == INVALID_SOCKET) {
        return -1;
    }

    auto target = loopback(0);
    socklen_t size = sizeof(target);
    const int exclusive = 1;

    if (result(::setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                            reinterpret_cast<const char *>(&exclusive), sizeof(exclusive))) < 0 ||
        result(bind(listener.value, reinterpret_cast<sockaddr *>(&target), size)) < 0 ||
        result(getsockname(listener.value, reinterpret_cast<sockaddr *>(&target), &size)) < 0 ||
        result(listen(listener.value, SOMAXCONN)) < 0) {
        return -1;
    }

    auto client = std::make_shared<Socket>(create_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));

    if (client->value == INVALID_SOCKET) {
        return -1;
    }

    auto source = loopback(0);
    size = sizeof(source);

    // Prevent another local socket from binding the client's endpoint before
    // we authenticate the accepted peer using its source address and port.
    if (result(::setsockopt(client->value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                            reinterpret_cast<const char *>(&exclusive), sizeof(exclusive))) < 0 ||
        result(bind(client->value, reinterpret_cast<sockaddr *>(&source), size)) < 0 ||
        result(getsockname(client->value, reinterpret_cast<sockaddr *>(&source), &size)) < 0) {
        return -1;
    }

    if (!make_nonblocking(listener.value) || !make_nonblocking(client->value)) {
        return -1;
    }

    if (connect(client->value, reinterpret_cast<sockaddr *>(&target), sizeof(target)) ==
                SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK) {
        return set_socket_error(WSAGetLastError());
    }

    WSAPOLLFD ready[] = {{listener.value, POLLIN, 0}, {client->value, POLLOUT, 0}};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);

    while (!(ready[0].revents & POLLIN)) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

        if (remaining.count() <= 0) {
            errno = ETIMEDOUT;

            return -1;
        }

        if (result(WSAPoll(ready, 2, static_cast<int>(remaining.count()))) < 0) {
            return -1;
        }

        // A ready client is no longer needed by this wait.
        if (ready[1].revents & POLLOUT) {
            ready[1].events = 0;
        }

        if (ready[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = ECONNREFUSED;

            return -1;
        }
    }

    sockaddr_in peer{};
    size = sizeof(peer);
    auto accepted = std::make_shared<Socket>(
            ::accept(listener.value, reinterpret_cast<sockaddr *>(&peer), &size));

    if (accepted->value == INVALID_SOCKET) {
        return set_socket_error(WSAGetLastError());
    }

    if (peer.sin_addr.s_addr != source.sin_addr.s_addr || peer.sin_port != source.sin_port) {
        errno = ECONNABORTED;

        return -1;
    }

    if (!SetHandleInformation(reinterpret_cast<HANDLE>(accepted->value), HANDLE_FLAG_INHERIT, 0)) {
        return -1;
    }

    unique_fd reader(insert(std::move(accepted)));
    unique_fd writer(insert(std::move(client)));

    if (reader.get() < 0 || writer.get() < 0) {
        return -1;
    }

    fds[0] = reader.release();
    fds[1] = writer.release();

    return 0;
}

char *adb_strerror(int error) {
    return strerror(error);
}

int adb_thread_setname(const std::string &name) {
    using SetDescription = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    static const auto set_description = reinterpret_cast<SetDescription>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
    std::wstring wide;

    if (!set_description || !android::base::UTF8ToWide(name, &wide)) {
        return -1;
    }

    return SUCCEEDED(set_description(GetCurrentThread(), wide.c_str())) ? 0 : -1;
}

bool adb_host_connect_loopback(borrowed_fd fd, uint16_t port, const std::atomic<bool> &stopping) {
    auto socket = lookup(fd.get());

    if (!socket) {
        return false;
    }

    auto target = loopback(port);

    if (connect(socket->value, reinterpret_cast<const sockaddr *>(&target), sizeof(target)) == 0) {
        return !stopping.load();
    }

    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);

    while (!stopping.load() && std::chrono::steady_clock::now() < deadline) {
        WSAPOLLFD wait{socket->value, POLLOUT, 0};
        const int ready = WSAPoll(&wait, 1, 50);

        if (ready < 0) {
            return false;
        }

        if (ready == 0) {
            continue;
        }

        int error = 0;
        socklen_t size = sizeof(error);

        return (wait.revents & POLLOUT) &&
               getsockopt(socket->value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                          &size) == 0 &&
               !error && !stopping.load();
    }

    return false;
}
