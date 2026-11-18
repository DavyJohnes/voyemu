#include "net.hpp"
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace voyemu::os {
bool suppress_sigpipe(int socket) {
    int one = 1;

    return ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) == 0;
}

std::optional<std::int64_t> unix_peer_pid(int socket) {
    pid_t peer = 0;
    socklen_t size = sizeof peer;

    if (::getsockopt(socket, SOL_LOCAL, LOCAL_PEERPID, &peer, &size) != 0 || size != sizeof peer) {
        return std::nullopt;
    }

    return peer;
}

bool unix_peer_is_current_user(int socket) {
    uid_t uid;
    gid_t gid;

    return ::getpeereid(socket, &uid, &gid) == 0 && uid == ::geteuid();
}

void disable_socket_sigpipe(int socket) {
    int one = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
}

ssize_t send_without_sigpipe(int socket, const void *data, std::size_t size) {
    return ::send(socket, data, size, 0);
}

std::filesystem::path user_socket_directory() {
    const char *home = std::getenv("HOME");

    if (!home || !*home) {
        return {};
    }

    return std::filesystem::path(home) / "Library/Caches/voyemu/runtime";
}
} // namespace voyemu::os
