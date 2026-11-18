#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#ifndef _WIN32
#include <sys/types.h>
#endif

namespace voyemu::os {
#ifndef _WIN32
// Keeps writes to a socket whose peer has closed from raising SIGPIPE: per socket on macOS,
// process-wide on Linux, which has no socket option for it. Children reset SIGPIPE at spawn.
bool suppress_sigpipe(int socket);
// Process id of the peer of a connected Unix-domain socket.
std::optional<std::int64_t> unix_peer_pid(int socket);
// Whether the peer of a connected Unix-domain socket runs as the effective user of this process.
bool unix_peer_is_current_user(int socket);
// Sets the per-socket SIGPIPE suppression on macOS; Linux suppresses it per send instead.
void disable_socket_sigpipe(int socket);
// send() that never raises SIGPIPE on a socket that went through disable_socket_sigpipe.
ssize_t send_without_sigpipe(int socket, const void *data, std::size_t size);
// The short private runtime directory of the user for Unix-domain sockets when the temporary
// directory is too long: XDG_RUNTIME_DIR/voyemu on Linux, the Voyemu runtime cache of the home
// directory on macOS; empty without XDG_RUNTIME_DIR or HOME.
std::filesystem::path user_socket_directory();
#endif
} // namespace voyemu::os
