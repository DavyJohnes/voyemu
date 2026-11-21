#include "android_console.hpp"
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
namespace voyemu::android_console_detail {
namespace {
using Handle = int;
using Length = socklen_t;
constexpr std::intptr_t invalid = -1;

void close(Handle fd) {
    ::close(fd);
}

bool interrupted() {
    return errno == EINTR;
}

bool would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

sockaddr_in loopback(unsigned port) {
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));

    return address;
}
} // namespace

Socket::Socket() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bool configured = fd_ != invalid && fcntl(fd_, F_SETFD, FD_CLOEXEC) == 0 &&
                      fcntl(fd_, F_SETFL, O_NONBLOCK) == 0;
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    configured = configured &&
                 setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#endif

    if (!configured) {
        if (fd_ != invalid) {
            close(fd_);
        }

        throw Disconnected{};
    }
}

Socket::~Socket() {
    close(fd_);
}

unsigned Socket::bind_inactive() {
    sockaddr_in address = loopback(0);

    Length size = sizeof(address);

    if (::bind(fd_, reinterpret_cast<const sockaddr *>(&address), size) ||
        getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size)) {
        throw Disconnected{};
    }

    return ntohs(address.sin_port);
}

void Socket::connect(unsigned port, Clock::time_point deadline) {
    const sockaddr_in address = loopback(port);

    if (!::connect(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address))) {
        return;
    }

    if (errno != EINPROGRESS && !interrupted()) {
        throw Disconnected{};
    }

    ready(true, deadline);
    int error = 0;
    Length size = sizeof(error);

    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) || error) {
        throw Disconnected{};
    }
}

void Socket::send(std::string_view text, Clock::time_point deadline) {
    while (!text.empty()) {
        ready(true, deadline);
        constexpr int flags =
#ifdef MSG_NOSIGNAL
                MSG_NOSIGNAL;
#else
                0;
#endif
        const auto count = ::send(fd_, text.data(), static_cast<int>(text.size()), flags);

        if (count < 0 && (interrupted() || would_block())) {
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
        const auto count = ::recv(fd_, bytes.data(), static_cast<int>(bytes.size()), 0);

        if (count < 0 && (interrupted() || would_block())) {
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

        pollfd selected{static_cast<Handle>(fd_), static_cast<short>(writing ? POLLOUT : POLLIN),
                        0};
        const int result = ::poll(&selected, 1, static_cast<int>((left + 999) / 1000));

        if (result > 0 && (selected.revents & selected.events)) {
            return;
        }

        if (result < 0 && interrupted()) {
            continue;
        }

        if (result == 0) {
            throw Timeout{};
        }

        throw Disconnected{};
    }
}

std::string token(const std::filesystem::path &home) {
    const auto path = home / ".emulator_console_auth_token";
    std::array<char, 257> data{};

    const auto file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (file < 0) {
        throw Disconnected{};
    }

    struct Close {
        int file;

        ~Close() {
            ::close(file);
        }
    } close{file};
    struct stat info{};

    if (fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_uid != geteuid()) {
        throw Disconnected{};
    }

    const auto bytes = ::read(file, data.data(), data.size());

    if (bytes <= 0) {
        throw Disconnected{};
    }

    if (bytes == 0 || static_cast<std::size_t>(bytes) >= data.size()) {
        throw Disconnected{};
    }

    std::string result(data.data(), static_cast<std::size_t>(bytes));

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
