#include "core/process.hpp"
#include "os/socket.hpp"
#include <windows.h>
#include <cstring>
#include <iphlpapi.h>
#include <vector>

namespace voyemu::os {
namespace {
SOCKET native(SocketHandle socket) {
    return static_cast<SOCKET>(socket);
}

// Whether a listener table row holds port in the LISTEN state, recording it in diagnostic.
template <typename Row, typename Address>
bool listening(const Row &row, int family, unsigned port, const Address &address,
               TcpPortProbe *diagnostic) {
    if (row.dwState != MIB_TCP_STATE_LISTEN ||
        ntohs(static_cast<u_short>(row.dwLocalPort)) != port) {
        return false;
    }

    if (diagnostic) {
        diagnostic->listener_pid = row.dwOwningPid;
        ::inet_ntop(family, &address, diagnostic->listener_address.data(),
                    diagnostic->listener_address.size());
    }

    return true;
}
} // namespace

SocketLibrary::SocketLibrary() {
    WSADATA data{};

    error_ = static_cast<unsigned>(WSAStartup(MAKEWORD(2, 2), &data));
}

SocketLibrary::~SocketLibrary() {
    if (!error_) {
        WSACleanup();
    }
}

unsigned socket_error() {
    return static_cast<unsigned>(WSAGetLastError());
}

bool socket_interrupted() {
    return WSAGetLastError() == WSAEINTR;
}

bool socket_would_block() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

bool socket_connecting() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

SocketHandle open_stream_socket(int family) {
    const SOCKET socket = ::socket(family, SOCK_STREAM, IPPROTO_TCP);

    return socket == INVALID_SOCKET ? no_socket : static_cast<SocketHandle>(socket);
}

SocketHandle open_private_tcp_socket() {
    const SOCKET socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                                     WSA_FLAG_NO_HANDLE_INHERIT);
    u_long nonblocking = 1;

    if (socket == INVALID_SOCKET) {
        return no_socket;
    }

    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
        closesocket(socket);

        return no_socket;
    }

    return static_cast<SocketHandle>(socket);
}

void close_socket(SocketHandle socket) {
    closesocket(native(socket));
}

bool bind_socket(SocketHandle socket, const sockaddr *address, SocketLength size) {
    return ::bind(native(socket), address, size) == 0;
}

bool connect_socket(SocketHandle socket, const sockaddr *address, SocketLength size) {
    return ::connect(native(socket), address, size) == 0;
}

bool socket_name(SocketHandle socket, sockaddr *address, SocketLength *size) {
    return ::getsockname(native(socket), address, size) == 0;
}

int pending_socket_error(SocketHandle socket) {
    int error = 0;
    int size = sizeof(error);

    return ::getsockopt(native(socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                        &size)
                   ? -1
                   : error;
}

long long send_socket(SocketHandle socket, const char *data, std::size_t size) {
    return ::send(native(socket), data, static_cast<int>(size), 0);
}

long long receive_socket(SocketHandle socket, char *data, std::size_t size) {
    return ::recv(native(socket), data, static_cast<int>(size), 0);
}

bool listen_socket(SocketHandle socket, int backlog) {
    return ::listen(native(socket), backlog) == 0;
}

bool reuse_address(SocketHandle) {
    return true;
}

bool exclusive_address(SocketHandle socket) {
    int exclusive = 1;

    return setsockopt(native(socket), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                      reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) == 0;
}

int wait_socket(SocketHandle socket, bool writing, long long timeout_us) {
    fd_set selected, failed;

    FD_ZERO(&selected);
    FD_SET(native(socket), &selected);
    FD_ZERO(&failed);
    FD_SET(native(socket), &failed);
    timeval budget{static_cast<long>(timeout_us / 1000000),
                   static_cast<long>(timeout_us % 1000000)};
    const int result = ::select(0, writing ? nullptr : &selected, writing ? &selected : nullptr,
                                &failed, &budget);

    if (result > 0) {
        return FD_ISSET(native(socket), &failed) ? -2 : 1;
    }

    return result;
}

std::optional<SocketRefusal> loopback_listener(int family, const sockaddr *, SocketLength,
                                               unsigned port, TcpPortProbe *diagnostic) {
    // Wine and Windows reusable sockets may allow overlapping binds; inspect live listeners first.
    ULONG bytes = 0;
    auto table_result = GetExtendedTcpTable(nullptr, &bytes, FALSE, family,
                                            TCP_TABLE_OWNER_PID_LISTENER, 0);

    if (table_result != ERROR_INSUFFICIENT_BUFFER || bytes > 1024 * 1024) {
        return SocketRefusal{"table_size", static_cast<unsigned>(table_result)};
    }

    std::vector<unsigned char> table_bytes(bytes);
    const auto table_read = GetExtendedTcpTable(table_bytes.data(), &bytes, FALSE, family,
                                                TCP_TABLE_OWNER_PID_LISTENER, 0);

    if (table_read != NO_ERROR) {
        return SocketRefusal{"table_read", static_cast<unsigned>(table_read)};
    }

    if (family == AF_INET) {
        const auto table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(table_bytes.data());

        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            in_addr local{};
            local.s_addr = table->table[i].dwLocalAddr;

            if (listening(table->table[i], family, port, local, diagnostic)) {
                return SocketRefusal{"listener", 0};
            }
        }
    } else {
        const auto table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID *>(table_bytes.data());

        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            IN6_ADDR local{};
            std::memcpy(&local, table->table[i].ucLocalAddr, sizeof local);

            if (listening(table->table[i], family, port, local, diagnostic)) {
                return SocketRefusal{"listener", 0};
            }
        }
    }

    return std::nullopt;
}
} // namespace voyemu::os
