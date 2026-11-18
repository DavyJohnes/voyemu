#include "net.hpp"
#include <csignal>
#include <cstdlib>
#include <sys/socket.h>
#include <unistd.h>

namespace voyemu::os {
bool suppress_sigpipe(int) {
    static const bool ignored = std::signal(SIGPIPE, SIG_IGN) != SIG_ERR;

    return ignored;
}

std::optional<std::int64_t> unix_peer_pid(int socket) {
    ucred credentials{};
    socklen_t size = sizeof credentials;

    if (::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
        size != sizeof credentials) {
        return std::nullopt;
    }

    return credentials.pid;
}

bool unix_peer_is_current_user(int socket) {
    ucred credentials{};
    socklen_t length = sizeof credentials;

    return ::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 &&
           credentials.uid == ::geteuid();
}

void disable_socket_sigpipe(int) {}

ssize_t send_without_sigpipe(int socket, const void *data, std::size_t size) {
    return ::send(socket, data, size, MSG_NOSIGNAL);
}

std::filesystem::path user_socket_directory() {
    // XDG_RUNTIME_DIR is the short private runtime directory of the user.
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");

    if (runtime && std::filesystem::path(runtime).is_absolute()) {
        return std::filesystem::weakly_canonical(runtime) / "voyemu";
    }

    return {};
}
} // namespace voyemu::os
