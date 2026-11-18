#include "interguest_network.hpp"
#include "error.hpp"
#include "os/net.hpp"
#include "os/process.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#endif
namespace voyemu {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;

void close_socket(Socket s) {
    if (s != invalid) {
        ::closesocket(s);
    }
}

bool interrupted() {
    return WSAGetLastError() == WSAEINTR;
}

bool again() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

bool connecting() {
    const auto error = WSAGetLastError();

    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
}
#else
using Socket = int;
constexpr Socket invalid = -1;

void close_socket(Socket s) {
    if (s != invalid) {
        ::close(s);
    }
}

bool interrupted() {
    return errno == EINTR;
}

bool again() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool connecting() {
    return errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK;
}
#endif
using Clock = std::chrono::steady_clock;

Clock::time_point deadline(int ms) {
    if (ms < 0 || ms > 30000) {
        throw VoyemuError("INTERGUEST_ARGUMENT", "timeout outside 0..30000 ms");
    }

    return Clock::now() + std::chrono::milliseconds(ms);
}

void nonblocking(Socket s) {
#ifdef _WIN32
    u_long one = 1;

    if (ioctlsocket(s, FIONBIO, &one)) {
        throw VoyemuError("INTERGUEST_SOCKET", "nonblocking failed");
    }
#else
    if (s >= FD_SETSIZE) {
        throw VoyemuError("INTERGUEST_SOCKET", "descriptor outside select bound");
    }

    const int flags = fcntl(s, F_GETFL, 0);

    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(s, F_SETFD, FD_CLOEXEC) < 0) {
        throw VoyemuError("INTERGUEST_SOCKET", "nonblocking failed");
    }

    if (!os::suppress_sigpipe(s)) {
        throw VoyemuError("INTERGUEST_SOCKET", "signal policy failed");
    }
#endif
}

bool ready(Socket s, bool write, Clock::time_point end, bool connect_error = false) {
    for (;;) {
        const auto us = std::max<std::int64_t>(
                0,
                std::chrono::duration_cast<std::chrono::microseconds>(end - Clock::now()).count());
        fd_set selected, errors;
        FD_ZERO(&selected);
        FD_SET(s, &selected);
        FD_ZERO(&errors);

        if (connect_error) {
            FD_SET(s, &errors);
        }

        timeval budget{};
        budget.tv_sec = static_cast<decltype(budget.tv_sec)>(us / 1000000);
        budget.tv_usec = static_cast<decltype(budget.tv_usec)>(us % 1000000);
        int result = ::select(static_cast<int>(s) + 1, write ? nullptr : &selected,
                              write ? &selected : nullptr, connect_error ? &errors : nullptr,
                              &budget);

        if (result < 0 && interrupted()) {
            continue;
        }

        if (result < 0) {
            throw VoyemuError("INTERGUEST_SOCKET", "select failed");
        }

        return result > 0;
    }
}
} // namespace
struct InterguestNetwork::Impl {
    Socket listener = invalid, peer = invalid, pending = invalid;
    Status state;
    bool client = false;
    std::array<std::uint8_t, 4> header{};
    std::size_t header_used = 0, body_used = 0;
    std::vector<std::uint8_t> body;
#ifdef _WIN32
    bool winsock = false;
    HANDLE process = nullptr;
    DWORD process_pid = 0;

    static bool tcp_table(TCP_TABLE_CLASS kind, std::vector<unsigned char> &data) {
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            ULONG bytes = 0;
            const auto first = GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, kind, 0);

            if (first == ERROR_NO_DATA) {
                return false;
            }

            if ((first != ERROR_INSUFFICIENT_BUFFER && first != NO_ERROR) ||
                bytes < sizeof(DWORD) || bytes > 1024 * 1024) {
                throw VoyemuError("INTERGUEST_OWNER", "TCP ownership table unavailable");
            }

            data.resize(bytes);
            const auto result = GetExtendedTcpTable(data.data(), &bytes, FALSE, AF_INET, kind, 0);

            if (result == ERROR_INSUFFICIENT_BUFFER) {
                continue;
            }

            if (result == ERROR_NO_DATA) {
                return false;
            }

            if (result != NO_ERROR) {
                throw VoyemuError("INTERGUEST_OWNER", "TCP ownership read failed");
            }

            if (bytes > data.size() || bytes < sizeof(DWORD)) {
                throw VoyemuError("INTERGUEST_OWNER", "TCP ownership extent invalid");
            }

            const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(data.data());

            if (table->dwNumEntries > (bytes - sizeof(DWORD)) / sizeof(MIB_TCPROW_OWNER_PID)) {
                throw VoyemuError("INTERGUEST_OWNER", "TCP ownership count invalid");
            }

            return true;
        }

        return false;
    }

    bool connection_owned(Socket socket) {
        if (!process || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
            return false;
        }

        sockaddr_in local{}, remote{};
        int size = sizeof local;

        if (getsockname(socket, reinterpret_cast<sockaddr *>(&local), &size) ||
            getpeername(socket, reinterpret_cast<sockaddr *>(&remote), &size)) {
            return false;
        }

        if (local.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            remote.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            ntohs(remote.sin_port) != state.port) {
            throw VoyemuError("INTERGUEST_OWNER", "connected endpoint differs");
        }

        std::vector<unsigned char> data;

        if (!tcp_table(TCP_TABLE_OWNER_PID_CONNECTIONS, data)) {
            return false;
        }

        const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(data.data());
        bool outgoing = false;

        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto &row = table->table[i];

            if (row.dwLocalAddr != htonl(INADDR_LOOPBACK) ||
                row.dwRemoteAddr != htonl(INADDR_LOOPBACK) || row.dwState != MIB_TCP_STATE_ESTAB) {
                continue;
            }

            const auto source = ntohs(static_cast<u_short>(row.dwLocalPort)),
                       target = ntohs(static_cast<u_short>(row.dwRemotePort));

            if (source == ntohs(local.sin_port) && target == state.port &&
                row.dwOwningPid == GetCurrentProcessId()) {
                outgoing = true;
            }

            if (source != state.port || target != ntohs(local.sin_port)) {
                continue;
            }

            if (!row.dwOwningPid) {
                continue;
            }

            if (row.dwOwningPid != process_pid) {
                throw VoyemuError("INTERGUEST_OWNER", "connected peer belongs to foreign process");
            }

            return true;
        }

        // Before accept(), Windows may expose only our client row. The same live owned listener
        // must still exist.
        return outgoing && listener_owned();
    }

    bool listener_owned() {
        if (!process || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
            return false;
        }

        std::vector<unsigned char> data;

        if (!tcp_table(TCP_TABLE_OWNER_PID_LISTENER, data)) {
            return false;
        }

        const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(data.data());
        bool found = false;

        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto &row = table->table[i];

            if (ntohs(static_cast<u_short>(row.dwLocalPort)) != state.port) {
                continue;
            }

            if (row.dwOwningPid != process_pid || row.dwLocalAddr != htonl(INADDR_LOOPBACK)) {
                throw VoyemuError("INTERGUEST_OWNER", "foreign listener at selected endpoint");
            }

            found = true;
        }

        return found;
    }
#else
    std::uint64_t process_pid = 0;

    static bool alive(std::uint64_t pid) {
        return os::process_exists(static_cast<std::int64_t>(pid));
    }

    static bool listen_port(std::uint64_t pid, std::uint16_t port) {
        for (const auto &socket : os::process_tcp_sockets(static_cast<std::int64_t>(pid))) {
            if (socket.state != os::TcpSocket::State::listen || !socket.local.ipv4) {
                continue;
            }

            if (socket.local.port == port && socket.local.address4 == htonl(INADDR_LOOPBACK)) {
                return true;
            }
        }

        return false;
    }

    static bool established(std::uint64_t pid, std::uint16_t local_port,
                            std::uint16_t remote_port) {
        for (const auto &socket : os::process_tcp_sockets(static_cast<std::int64_t>(pid))) {
            if (socket.state != os::TcpSocket::State::established || !socket.local.ipv4) {
                continue;
            }

            if (socket.local.address4 != htonl(INADDR_LOOPBACK) ||
                socket.remote.address4 != htonl(INADDR_LOOPBACK)) {
                continue;
            }

            if (socket.local.port == local_port && socket.remote.port == remote_port) {
                return true;
            }
        }

        return false;
    }

    bool connection_owned(Socket socket) {
        if (!process_pid || !alive(process_pid)) {
            return false;
        }

        sockaddr_in local{}, remote{};
        socklen_t size = sizeof local;

        if (getsockname(socket, reinterpret_cast<sockaddr *>(&local), &size) ||
            getpeername(socket, reinterpret_cast<sockaddr *>(&remote), &size)) {
            return false;
        }

        if (local.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            remote.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            ntohs(remote.sin_port) != state.port) {
            throw VoyemuError("INTERGUEST_OWNER", "connected endpoint differs");
        }

        if (established(process_pid, static_cast<std::uint16_t>(state.port),
                        ntohs(local.sin_port))) {
            return true;
        }

        // Before accept(), our own client row may be the only visible row. The same live owned
        // listener must still exist.
        return established(static_cast<std::uint64_t>(::getpid()), ntohs(local.sin_port),
                           static_cast<std::uint16_t>(state.port)) &&
               listener_owned();
    }

    bool listener_owned() {
        if (!process_pid || !alive(process_pid)) {
            return false;
        }

        return listen_port(process_pid, static_cast<std::uint16_t>(state.port));
    }

    bool accepted_owned(Socket socket) {
        if (!process_pid || !alive(process_pid)) {
            return false;
        }

        sockaddr_in local{}, remote{};
        socklen_t size = sizeof local;

        if (getsockname(socket, reinterpret_cast<sockaddr *>(&local), &size) ||
            getpeername(socket, reinterpret_cast<sockaddr *>(&remote), &size)) {
            return false;
        }

        if (local.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            remote.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            ntohs(local.sin_port) != state.port) {
            return false;
        }

        // The accepted peer must hold the ESTABLISHED row for the dialed endpoint.
        return established(process_pid, ntohs(remote.sin_port), ntohs(local.sin_port));
    }
#endif

    void drop() noexcept {
        close_socket(peer);
        close_socket(pending);
        peer = pending = invalid;
        state.connected = false;
        header_used = body_used = 0;
        body.clear();
    }
};

InterguestNetwork::InterguestNetwork() : impl_(std::make_unique<Impl>()) {}

InterguestNetwork::~InterguestNetwork() {
    stop();
}

unsigned InterguestNetwork::start() {
    if (impl_->state.port) {
        throw VoyemuError("INTERGUEST_STATE", "transport already started");
    }

#ifdef _WIN32
    WSADATA w{};

    if (WSAStartup(MAKEWORD(2, 2), &w)) {
        throw VoyemuError("INTERGUEST_SOCKET", "Winsock startup failed");
    }
    impl_->winsock = true;
#endif
    try {
        impl_->listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (impl_->listener == invalid) {
            throw VoyemuError("INTERGUEST_SOCKET", "creation failed");
        }

        nonblocking(impl_->listener);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (::bind(impl_->listener, reinterpret_cast<sockaddr *>(&address), sizeof address) ||
            ::listen(impl_->listener, 1)) {
            throw VoyemuError("INTERGUEST_SOCKET", "loopback bind failed");
        }

#ifdef _WIN32
        int size = sizeof address;
#else
        socklen_t size = sizeof address;
#endif

        if (::getsockname(impl_->listener, reinterpret_cast<sockaddr *>(&address), &size)) {
            throw VoyemuError("INTERGUEST_SOCKET", "endpoint unavailable");
        }

        impl_->state = {ntohs(address.sin_port), false, 0, 0};

        return impl_->state.port;
    } catch (...) {
        stop();
        throw;
    }
}

unsigned InterguestNetwork::start_client() {
    const auto port = start();

    close_socket(impl_->listener);
    impl_->listener = invalid;
    impl_->client = true;

    return port;
}

void InterguestNetwork::stop() noexcept {
    impl_->drop();
    close_socket(impl_->listener);
    impl_->listener = invalid;
    impl_->state.port = 0;
    impl_->client = false;
#ifdef _WIN32
    if (impl_->process) {
        CloseHandle(impl_->process);
    }

    impl_->process = nullptr;
    impl_->process_pid = 0;

    if (impl_->winsock) {
        WSACleanup();
        impl_->winsock = false;
    }
#else
    impl_->process_pid = 0;
#endif
}

bool InterguestNetwork::accept(int ms) {
    const auto end = deadline(ms);

    if (impl_->listener == invalid || impl_->peer != invalid) {
        throw VoyemuError("INTERGUEST_STATE", "accept requires unconnected listener");
    }

    while (ready(impl_->listener, false, end)) {
        const Socket peer = ::accept(impl_->listener, nullptr, nullptr);

        if (peer == invalid) {
            if (again() || interrupted()) {
                continue;
            }

            throw VoyemuError("INTERGUEST_SOCKET", "accept failed");
        }

        try {
            nonblocking(peer);
        } catch (...) {
            close_socket(peer);
            throw;
        }

#ifndef _WIN32
        if (!impl_->accepted_owned(peer)) {
            close_socket(peer);
            continue;
        }
#endif
        impl_->peer = peer;
        impl_->state.connected = true;

        return true;
    }

    return false;
}

void InterguestNetwork::bind_peer(std::uint64_t pid) {
#ifdef _WIN32
    if (!impl_->client || pid == 0 || pid > MAXDWORD) {
        throw VoyemuError("INTERGUEST_OWNER", "client process required");
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                 static_cast<DWORD>(pid));

    if (!process || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        if (process) {
            CloseHandle(process);
        }

        throw VoyemuError("INTERGUEST_OWNER", "process unavailable");
    }

    bool child = pid == GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 row{};
        row.dwSize = sizeof row;

        if (Process32First(snapshot, &row)) {
            do {
                if (row.th32ProcessID == pid && row.th32ParentProcessID == GetCurrentProcessId()) {
                    child = true;
                }
            } while (Process32Next(snapshot, &row));
        }

        CloseHandle(snapshot);
    }

    if (!child) {
        CloseHandle(process);
        throw VoyemuError("INTERGUEST_OWNER", "process is not owned child");
    }

    impl_->drop();

    if (impl_->process) {
        CloseHandle(impl_->process);
    }

    impl_->process = process;
    impl_->process_pid = static_cast<DWORD>(pid);
#else
    // Both directions accept an owned peer: the client verifies its peer, the server verifies its
    // dialer.
    if (pid == 0 || pid > 0x7fffffffULL) {
        throw VoyemuError("INTERGUEST_OWNER", "client process required");
    }

    if (!Impl::alive(pid)) {
        throw VoyemuError("INTERGUEST_OWNER", "process unavailable");
    }

    bool child = pid == static_cast<std::uint64_t>(::getpid());

    if (!child) {
        if (os::parent_process(static_cast<std::int64_t>(pid)) == ::getpid()) {
            child = true;
        }
    }

    if (!child) {
        throw VoyemuError("INTERGUEST_OWNER", "process is not owned child");
    }

    impl_->drop();
    impl_->process_pid = pid;
#endif
}

bool InterguestNetwork::connect(int ms) {
    const auto end = deadline(ms);

    if (!impl_->client || !impl_->state.port || impl_->peer != invalid) {
        throw VoyemuError("INTERGUEST_STATE", "connect requires unconnected client");
    }

    if (impl_->pending == invalid && !impl_->listener_owned()) {
        return false;
    }

    if (impl_->pending == invalid) {
        const Socket peer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (peer == invalid) {
            throw VoyemuError("INTERGUEST_SOCKET", "client creation failed");
        }

        try {
            nonblocking(peer);
        } catch (...) {
            close_socket(peer);
            throw;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(impl_->state.port));

        if (::connect(peer, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 &&
            !connecting()) {
            close_socket(peer);

            return false;
        }

        impl_->pending = peer;
    }

    if (!ready(impl_->pending, true, end, true)) {
        return false;
    }

    int error = 0;
#ifdef _WIN32
    int size = sizeof error;
#else
    socklen_t size = sizeof error;
#endif

    if (getsockopt(impl_->pending, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) ||
        error) {
        close_socket(impl_->pending);
        impl_->pending = invalid;

        return false;
    }

    if (!impl_->connection_owned(impl_->pending)) {
        return false;
    }

    impl_->peer = impl_->pending;
    impl_->pending = invalid;
    impl_->state.connected = true;

    return true;
}

bool InterguestNetwork::receive(std::vector<std::uint8_t> &frame, int ms) {
    const auto end = deadline(ms);

    if (impl_->peer == invalid) {
        throw VoyemuError("INTERGUEST_STATE", "no connected peer");
    }

    try {
        for (;;) {
            if (impl_->header_used == 4 && impl_->body.empty()) {
                const auto &h = impl_->header;
                const std::uint32_t n = (std::uint32_t(h[0]) << 24) | (std::uint32_t(h[1]) << 16) |
                                        (std::uint32_t(h[2]) << 8) | h[3];

                if (n < 14 || n > maximum_frame) {
                    throw VoyemuError("INTERGUEST_FRAME", "invalid Ethernet extent");
                }

                impl_->body.resize(n);
            }

            if (!impl_->body.empty() && impl_->body_used == impl_->body.size()) {
                frame = std::move(impl_->body);
                impl_->body.clear();
                impl_->header_used = impl_->body_used = 0;
                ++impl_->state.received;

                return true;
            }

            if (!ready(impl_->peer, false, end)) {
                return false;
            }

            const bool header = impl_->header_used < 4;
            auto *target = header ? impl_->header.data() + impl_->header_used
                                  : impl_->body.data() + impl_->body_used;
            const auto left = header ? 4 - impl_->header_used
                                     : impl_->body.size() - impl_->body_used;
            const int n = ::recv(impl_->peer, reinterpret_cast<char *>(target),
                                 static_cast<int>(left), 0);

            if (n < 0 && (again() || interrupted())) {
                continue;
            }

            if (n <= 0) {
                throw VoyemuError("INTERGUEST_SOCKET", "peer disconnected");
            }

            if (header) {
                impl_->header_used += n;
            } else {
                impl_->body_used += n;
            }
        }
    } catch (...) {
        impl_->drop();
        throw;
    }
}

void InterguestNetwork::send(std::span<const std::uint8_t> frame, int ms) {
    const auto end = deadline(ms);

    if (frame.size() < 14 || frame.size() > maximum_frame) {
        throw VoyemuError("INTERGUEST_FRAME", "invalid Ethernet extent");
    }

    if (impl_->peer == invalid) {
        throw VoyemuError("INTERGUEST_STATE", "no connected peer");
    }

    std::vector<std::uint8_t> wire(4 + frame.size());
    const auto n = static_cast<std::uint32_t>(frame.size());
    wire[0] = n >> 24;
    wire[1] = n >> 16;
    wire[2] = n >> 8;
    wire[3] = n;
    std::copy(frame.begin(), frame.end(), wire.begin() + 4);
    try {
        std::size_t used = 0;

        while (used < wire.size()) {
            if (!ready(impl_->peer, true, end)) {
                throw VoyemuError("INTERGUEST_TIMEOUT", "send deadline elapsed");
            }

            const int count = ::send(impl_->peer,
                                     reinterpret_cast<const char *>(wire.data() + used),
                                     static_cast<int>(wire.size() - used), 0);

            if (count < 0 && (again() || interrupted())) {
                continue;
            }

            if (count <= 0) {
                throw VoyemuError("INTERGUEST_SOCKET", "send disconnected");
            }

            used += count;
        }

        ++impl_->state.sent;
    } catch (...) {
        impl_->drop();
        throw;
    }
}

InterguestNetwork::Status InterguestNetwork::status() const noexcept {
    return impl_->state;
}
} // namespace voyemu
