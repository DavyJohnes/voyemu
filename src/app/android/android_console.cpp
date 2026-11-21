#include "android_console.hpp"
#include "os/fs.hpp"

namespace voyemu::android_console_detail {
namespace {
sockaddr_in loopback(unsigned port) {
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));

    return address;
}
} // namespace

Socket::Socket() {
    if (network_.error()) {
        throw Disconnected{};
    }

    fd_ = os::open_private_tcp_socket();

    if (fd_ == os::no_socket) {
        throw Disconnected{};
    }
}

Socket::~Socket() {
    os::close_socket(fd_);
}

unsigned Socket::bind_inactive() {
    sockaddr_in address = loopback(0);

    if (!os::exclusive_address(fd_)) {
        throw Disconnected{};
    }

    os::SocketLength size = sizeof(address);

    if (!os::bind_socket(fd_, reinterpret_cast<const sockaddr *>(&address), size) ||
        !os::socket_name(fd_, reinterpret_cast<sockaddr *>(&address), &size)) {
        throw Disconnected{};
    }

    return ntohs(address.sin_port);
}

void Socket::connect(unsigned port, Clock::time_point deadline) {
    const sockaddr_in address = loopback(port);

    if (os::connect_socket(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address))) {
        return;
    }

    if (!os::socket_connecting() && !os::socket_interrupted()) {
        throw Disconnected{};
    }

    ready(true, deadline);

    if (os::pending_socket_error(fd_) != 0) {
        throw Disconnected{};
    }
}

void Socket::send(std::string_view text, Clock::time_point deadline) {
    while (!text.empty()) {
        ready(true, deadline);
        const auto count = os::send_socket(fd_, text.data(), text.size());

        if (count < 0 && (os::socket_interrupted() || os::socket_would_block())) {
            continue;
        }

        if (count <= 0) {
            throw Disconnected{};
        }

        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

std::string Socket::receive(Clock::time_point deadline) {
    std::array<char, 4096> bytes{};

    for (;;) {
        ready(false, deadline);
        const auto count = os::receive_socket(fd_, bytes.data(), bytes.size());

        if (count < 0 && (os::socket_interrupted() || os::socket_would_block())) {
            continue;
        }

        if (count <= 0) {
            throw Disconnected{};
        }

        return {bytes.data(), static_cast<std::size_t>(count)};
    }
}

void Socket::ready(bool writing, Clock::time_point deadline) {
    for (;;) {
        const auto left =
                std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now())
                        .count();

        if (left <= 0) {
            throw Timeout{};
        }

        const int result = os::wait_socket(fd_, writing, left);

        if (result > 0) {
            return;
        }

        if (result == -1 && os::socket_interrupted()) {
            continue;
        }

        if (result == 0) {
            throw Timeout{};
        }

        throw Disconnected{};
    }
}

std::string token(const std::filesystem::path &home) {
    constexpr std::size_t limit = 257;
    const auto data = os::read_private_file(home / ".emulator_console_auth_token", limit);

    if (!data || data->empty() || data->size() >= limit) {
        throw Disconnected{};
    }

    std::string result = *data;

    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }

    if (result.empty() || !std::all_of(result.begin(), result.end(), [](unsigned char ch) {
            return ch > 32 && ch < 127;
        })) {
        throw Disconnected{};
    }

    return result;
}
} // namespace voyemu::android_console_detail
