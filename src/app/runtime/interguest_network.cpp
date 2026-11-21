#include "interguest_network.hpp"
#include "error.hpp"
#include "os/child_watch.hpp"
#include "os/platform.hpp"
#include "os/process.hpp"
#include "os/socket.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <vector>

namespace voyemu {
namespace {
using Socket = os::SocketHandle;
constexpr Socket invalid = os::no_socket;
using os::close_socket;

// Whether a non-blocking connect is still in progress.
bool connecting() {
    return os::socket_connecting() || os::socket_would_block();
}

using Clock = std::chrono::steady_clock;

Clock::time_point deadline(int ms) {
    if (ms < 0 || ms > 30000) {
        throw VoyemuError("INTERGUEST_ARGUMENT", "timeout outside 0..30000 ms");
    }

    return Clock::now() + std::chrono::milliseconds(ms);
}

void nonblocking(Socket s) {
    if (!os::set_nonblocking_socket(s)) {
        throw VoyemuError("INTERGUEST_SOCKET", "nonblocking failed");
    }

    if (!os::suppress_socket_signal(s)) {
        throw VoyemuError("INTERGUEST_SOCKET", "signal policy failed");
    }
}

// Whether s became readable, or writable when write is set, before end; an error reported by the
// socket counts as ready, so the next call reads it.
bool ready(Socket s, bool write, Clock::time_point end) {
    for (;;) {
        const auto us = std::max<std::int64_t>(
                0,
                std::chrono::duration_cast<std::chrono::microseconds>(end - Clock::now()).count());
        const int result = os::wait_socket(s, write, us);

        if (result == -1 && os::socket_interrupted()) {
            continue;
        }

        if (result == -1) {
            throw VoyemuError("INTERGUEST_SOCKET", "select failed");
        }

        return result != 0;
    }
}

// The loopback port of a socket address.
unsigned port_of(const sockaddr_in &address) {
    return ntohs(address.sin_port);
}

// Whether a holder owns its row, throwing what for a foreign holder and the table failures.
bool owned(os::LoopbackHolder holder, const char *what) {
    using Holder = os::LoopbackHolder;

    switch (holder) {
    case Holder::owner:
        return true;
    case Holder::absent:
        return false;
    case Holder::foreign:
        throw VoyemuError("INTERGUEST_OWNER", what);
    case Holder::table_unavailable:
        throw VoyemuError("INTERGUEST_OWNER", "TCP ownership table unavailable");
    case Holder::table_read_failed:
        throw VoyemuError("INTERGUEST_OWNER", "TCP ownership read failed");
    case Holder::table_extent_invalid:
        throw VoyemuError("INTERGUEST_OWNER", "TCP ownership extent invalid");
    case Holder::table_count_invalid:
        break;
    }

    throw VoyemuError("INTERGUEST_OWNER", "TCP ownership count invalid");
}
} // namespace
struct InterguestNetwork::Impl {
    Socket listener = invalid, peer = invalid, pending = invalid;
    Status state;
    bool client = false;
    std::array<std::uint8_t, 4> header{};
    std::size_t header_used = 0, body_used = 0;
    std::vector<std::uint8_t> body;
    std::optional<os::SocketLibrary> library;
    std::unique_ptr<os::ChildWatch> process;

    bool process_alive() const {
        return process && process->alive();
    }

    bool connection_owned(Socket socket) {
        if (!process_alive()) {
            return false;
        }

        sockaddr_in local{}, remote{};
        os::SocketLength size = sizeof local;

        if (!os::socket_name(socket, reinterpret_cast<sockaddr *>(&local), &size) ||
            !os::peer_name(socket, reinterpret_cast<sockaddr *>(&remote), &size)) {
            return false;
        }

        if (local.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            remote.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || port_of(remote) != state.port) {
            throw VoyemuError("INTERGUEST_OWNER", "connected endpoint differs");
        }

        if (owned(os::loopback_connection_holder(process->pid(), state.port, port_of(local)),
                  "connected peer belongs to foreign process")) {
            return true;
        }

        // Before accept(), our own client row may be the only visible row. The same live owned
        // listener must still exist.
        return os::loopback_connection_holder(static_cast<std::uint64_t>(os::current_process()),
                                              port_of(local),
                                              state.port) == os::LoopbackHolder::owner &&
               listener_owned();
    }

    bool listener_owned() {
        return process_alive() && owned(os::loopback_listener_holder(process->pid(), state.port),
                                        "foreign listener at selected endpoint");
    }

    bool accepted_owned(Socket socket) {
        if (!process_alive()) {
            return false;
        }

        sockaddr_in local{}, remote{};
        os::SocketLength size = sizeof local;

        if (!os::socket_name(socket, reinterpret_cast<sockaddr *>(&local), &size) ||
            !os::peer_name(socket, reinterpret_cast<sockaddr *>(&remote), &size)) {
            return false;
        }

        if (local.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            remote.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || port_of(local) != state.port) {
            return false;
        }

        // The accepted peer must hold the ESTABLISHED row for the dialed endpoint.
        return os::loopback_connection_holder(process->pid(), port_of(remote), port_of(local)) ==
               os::LoopbackHolder::owner;
    }

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

    impl_->library.emplace();

    if (impl_->library->error()) {
        impl_->library.reset();
        throw VoyemuError("INTERGUEST_SOCKET", "Winsock startup failed");
    }

    try {
        impl_->listener = os::open_stream_socket(AF_INET);

        if (impl_->listener == invalid) {
            throw VoyemuError("INTERGUEST_SOCKET", "creation failed");
        }

        nonblocking(impl_->listener);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (!os::bind_socket(impl_->listener, reinterpret_cast<sockaddr *>(&address),
                             sizeof address) ||
            !os::listen_socket(impl_->listener, 1)) {
            throw VoyemuError("INTERGUEST_SOCKET", "loopback bind failed");
        }

        os::SocketLength size = sizeof address;

        if (!os::socket_name(impl_->listener, reinterpret_cast<sockaddr *>(&address), &size)) {
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
    impl_->process.reset();
    impl_->library.reset();
}

bool InterguestNetwork::accept(int ms) {
    const auto end = deadline(ms);

    if (impl_->listener == invalid || impl_->peer != invalid) {
        throw VoyemuError("INTERGUEST_STATE", "accept requires unconnected listener");
    }

    while (ready(impl_->listener, false, end)) {
        const Socket peer = os::accept_socket(impl_->listener);

        if (peer == invalid) {
            if (os::socket_would_block() || os::socket_interrupted()) {
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

        // Windows binds no peer process to a listener: only its client direction verifies the peer.
        if (!os::host_windows && !impl_->accepted_owned(peer)) {
            close_socket(peer);
            continue;
        }

        impl_->peer = peer;
        impl_->state.connected = true;

        return true;
    }

    return false;
}

void InterguestNetwork::bind_peer(std::uint64_t pid) {
    // POSIX accepts an owned peer in both directions: the client verifies its peer, the server
    // verifies its dialer. Windows binds a peer only to a client.
    if (os::host_windows && !impl_->client) {
        throw VoyemuError("INTERGUEST_OWNER", "client process required");
    }

    auto process = std::make_unique<os::ChildWatch>();

    switch (process->open(pid)) {
    case os::ChildWatch::Opening::opened:
        break;
    case os::ChildWatch::Opening::invalid:
        throw VoyemuError("INTERGUEST_OWNER", "client process required");
    case os::ChildWatch::Opening::unavailable:
        throw VoyemuError("INTERGUEST_OWNER", "process unavailable");
    case os::ChildWatch::Opening::foreign:
        throw VoyemuError("INTERGUEST_OWNER", "process is not owned child");
    }

    impl_->drop();
    impl_->process = std::move(process);
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
        const Socket peer = os::open_stream_socket(AF_INET);

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

        if (!os::connect_socket(peer, reinterpret_cast<sockaddr *>(&address), sizeof address) &&
            !connecting()) {
            close_socket(peer);

            return false;
        }

        impl_->pending = peer;
    }

    if (!ready(impl_->pending, true, end)) {
        return false;
    }

    if (os::pending_socket_error(impl_->pending) != 0) {
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
            const auto n = os::receive_socket(impl_->peer, reinterpret_cast<char *>(target), left);

            if (n < 0 && (os::socket_would_block() || os::socket_interrupted())) {
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

            const auto count = os::send_socket(impl_->peer,
                                               reinterpret_cast<const char *>(wire.data() + used),
                                               wire.size() - used);

            if (count < 0 && (os::socket_would_block() || os::socket_interrupted())) {
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
