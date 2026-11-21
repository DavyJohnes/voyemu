#pragma once
// The stream sockets of the host behind one interface: Winsock on Windows, BSD sockets elsewhere.
#include <cstddef>
#include <cstdint>
#include <optional>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace voyemu {
struct TcpPortProbe;
}

namespace voyemu::os {
// A socket of the host; no_socket when absent.
using SocketHandle = std::intptr_t;
inline constexpr SocketHandle no_socket = -1;
#ifdef _WIN32
using SocketLength = int;
#else
using SocketLength = socklen_t;
#endif

// Keeps the socket library of the host started while it lives: Winsock on Windows, nothing
// elsewhere. error() is the start failure, 0 when started.
class SocketLibrary {
public:
    SocketLibrary();
    ~SocketLibrary();
    SocketLibrary(const SocketLibrary &) = delete;
    SocketLibrary &operator=(const SocketLibrary &) = delete;

    unsigned error() const {
        return error_;
    }

private:
    unsigned error_ = 0;
};

// The last socket error of this thread: errno or WSAGetLastError().
unsigned socket_error();
// Whether the last socket call failed because it was interrupted, would block, or is still
// connecting.
bool socket_interrupted();
bool socket_would_block();
bool socket_connecting();
// A stream socket of family; no_socket on failure.
SocketHandle open_stream_socket(int family);
// A non-blocking loopback TCP socket that the children of this process do not inherit and that
// reports a closed peer as an error, not a signal; no_socket on failure.
SocketHandle open_private_tcp_socket();
void close_socket(SocketHandle socket);
// Closes socket when the scope ends.
struct SocketCloser {
    SocketHandle socket = no_socket;

    ~SocketCloser() {
        if (socket != no_socket) {
            close_socket(socket);
        }
    }
};

bool bind_socket(SocketHandle socket, const sockaddr *address, SocketLength size);
bool connect_socket(SocketHandle socket, const sockaddr *address, SocketLength size);
bool socket_name(SocketHandle socket, sockaddr *address, SocketLength *size);
// The pending error of a socket after a non-blocking connect, -1 when it cannot be read.
int pending_socket_error(SocketHandle socket);
// One send or receive of at most size bytes without a broken-pipe signal: the byte count, 0 at the
// end of the stream, -1 on failure.
long long send_socket(SocketHandle socket, const char *data, std::size_t size);
long long receive_socket(SocketHandle socket, char *data, std::size_t size);
bool listen_socket(SocketHandle socket, int backlog);
// Lets the bind reuse an address in TIME_WAIT where the host needs it; Windows needs no option.
bool reuse_address(SocketHandle socket);
// Binds only this socket to its address, excluding other sockets of the host where it supports
// that (SO_EXCLUSIVEADDRUSE on Windows).
bool exclusive_address(SocketHandle socket);
// Waits up to timeout_us for socket to become writable or readable: positive when ready, 0 on
// timeout, -1 when the wait failed (socket_interrupted tells an interruption), -2 when the socket
// reports an error.
int wait_socket(SocketHandle socket, bool writing, long long timeout_us);

// The refusal of a loopback port that a live listener may hold.
struct SocketRefusal {
    const char *stage;
    unsigned error;
};
// Why the loopback address of port may have a live listener, none when it has none: Windows reads
// the TCP listener table, recording its process and address in diagnostic, and POSIX connects to
// it, because a reusable bind would succeed beside a listener on both.
std::optional<SocketRefusal> loopback_listener(int family, const sockaddr *address,
                                               SocketLength size, unsigned port,
                                               TcpPortProbe *diagnostic);
} // namespace voyemu::os
