// AOSP sysdeps.h must precede the other ADB headers.
#include "sysdeps.h"

#include "adb_loopback_socket.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// AOSP adb_poll retries EINTR internally with a fresh relative timeout. Keep
// interruption visible so every retry checks the absolute deadline of the caller.
#undef poll

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

int adb_loopback_poll(adb_pollfd *item, int timeout_ms) {
    return ::poll(item, 1, timeout_ms);
}

ssize_t adb_loopback_transfer(borrowed_fd fd, uint8_t *data, size_t size, bool writing) {
#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif

    return writing ? send(fd.get(), data, size, send_flags) : recv(fd.get(), data, size, 0);
}

unique_fd adb_loopback_connect(uint16_t port, const std::atomic<bool> &stopping) {
    unique_fd fd(socket(AF_INET, SOCK_STREAM, 0));

    if (fd.get() < 0 || fcntl(fd.get(), F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(fd.get(), F_SETFL, O_NONBLOCK) < 0) {
        return unique_fd();
    }

#ifdef SO_NOSIGPIPE
    int yes = 1;

    if (adb_setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) < 0) {
        return unique_fd();
    }
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    int result = connect(fd.get(), reinterpret_cast<const sockaddr *>(&address), sizeof(address));

    if (result < 0 && errno != EINPROGRESS && errno != EINTR) {
        return unique_fd();
    }

    if (result < 0) {
        const auto deadline = Clock::now() + 500ms;
        bool connected = false;

        while (!stopping.load() && Clock::now() < deadline) {
            adb_pollfd wait{fd.get(), POLLOUT, 0};
            int ready = ::poll(&wait, 1, 50);

            if (ready < 0 && errno == EINTR) {
                continue;
            }

            if (ready < 0) {
                return unique_fd();
            }

            if (!ready) {
                continue;
            }

            int error = 0;
            socklen_t size = sizeof(error);

            if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) {
                return unique_fd();
            }

            connected = true;
            break;
        }

        if (!connected) {
            return unique_fd();
        }
    }

    return fd;
}

void adb_loopback_ignore_broken_pipe() {
#ifndef SO_NOSIGPIPE
    // Like the upstream adb client: sockets without SO_NOSIGPIPE report EPIPE instead of a signal.
    signal(SIGPIPE, SIG_IGN);
#endif
}
