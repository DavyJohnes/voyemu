#include "qnx_shutdown_channel.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>

namespace voyemu {
struct QnxShutdownChannel::State {
    unsigned port = 0;
    SOCKET socket = INVALID_SOCKET;
    bool valid = false, wsa = false;
};

QnxShutdownChannel::QnxShutdownChannel(const Json &endpoint) : state_(std::make_unique<State>()) {
    auto &s = *state_;

    s.valid = endpoint.is_object() && endpoint.value("host", std::string{}) == "127.0.0.1" &&
              endpoint.contains("port") && endpoint.at("port").is_number_unsigned() &&
              endpoint.at("port").get<std::uint64_t>() >= 1024 &&
              endpoint.at("port").get<std::uint64_t>() <= 65535;

    if (s.valid) {
        s.port = endpoint.at("port").get<unsigned>();
    }
}

QnxShutdownChannel::~QnxShutdownChannel() {
    close();

    if (state_->wsa) {
        WSACleanup();
    }
}

bool QnxShutdownChannel::valid() const {
    return state_->valid;
}

void QnxShutdownChannel::connect(std::int64_t, Deadline deadline) {
    auto &s = *state_;

    if (!s.wsa) {
        WSADATA data{};
        qnx_shutdown_channel_require(WSAStartup(MAKEWORD(2, 2), &data) == 0);
        s.wsa = true;
    }

    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    qnx_shutdown_channel_require(socket != INVALID_SOCKET);
    s.socket = socket;
    u_long nonblocking = 1;
    qnx_shutdown_channel_require(ioctlsocket(socket, FIONBIO, &nonblocking) == 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(s.port));
    const int connected = ::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof address);

    if (connected != 0) {
        qnx_shutdown_channel_require(WSAGetLastError() == WSAEWOULDBLOCK);
        wait(true, deadline);
        int error = 0;
        int bytes = sizeof error;
        qnx_shutdown_channel_require(::getsockopt(socket, SOL_SOCKET, SO_ERROR,
                                                  reinterpret_cast<char *>(&error), &bytes) == 0 &&
                                     error == 0);
    }
}

void QnxShutdownChannel::verify(const std::function<void()> &verify_owner) {
    if (verify_owner) {
        verify_owner();
    }
}

void QnxShutdownChannel::wait(bool writable, Deadline deadline) {
    for (;;) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                                       deadline - std::chrono::steady_clock::now())
                                       .count();
        qnx_shutdown_channel_require(remaining > 0);
        fd_set selected;
        FD_ZERO(&selected);
        FD_SET(state_->socket, &selected);
        timeval timeout{static_cast<long>(remaining / 1000),
                        static_cast<long>((remaining % 1000) * 1000)};
        const int result = ::select(0, writable ? nullptr : &selected,
                                    writable ? &selected : nullptr, nullptr, &timeout);

        if (result < 0 && WSAGetLastError() == WSAEINTR) {
            continue;
        }

        qnx_shutdown_channel_require(result > 0);

        return;
    }
}

int QnxShutdownChannel::receive(char *bytes, int count) {
    const auto result = ::recv(state_->socket, bytes, count, 0);

    if (result < 0) {
        const auto error = WSAGetLastError();

        if (error == WSAEINTR || error == WSAEWOULDBLOCK) {
            return -1;
        }
    }

    return static_cast<int>(result);
}

int QnxShutdownChannel::send(const char *bytes, int count) {
    const auto result = ::send(state_->socket, bytes, count, 0);

    if (result < 0) {
        const auto error = WSAGetLastError();

        if (error == WSAEINTR || error == WSAEWOULDBLOCK) {
            return -1;
        }
    }

    return static_cast<int>(result);
}

void QnxShutdownChannel::close() noexcept {
    if (state_ && state_->socket != INVALID_SOCKET) {
        ::closesocket(state_->socket);
        state_->socket = INVALID_SOCKET;
    }
}

void QnxShutdownChannel::finish() {
    close();
}
} // namespace voyemu
