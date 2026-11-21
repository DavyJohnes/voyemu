#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#ifndef _WIN32
#include <netinet/in.h>
#endif

namespace voyemu::os {
// Canonical path of the running executable; throws PROCESS_PATH when the host cannot name it.
std::filesystem::path executable_path();
// Identifier of the running process.
std::int64_t current_process();

// Whether any process holds path open; unknown when the host cannot inspect open files.
enum class FileUse {
    unused,
    held,
    unknown
};
FileUse file_use(const std::filesystem::path &path);
// The variables of the process environment as UTF-8 name and value pairs, in environment order.
std::vector<std::pair<std::string, std::string>> environment_variables();
// A file or pipe of the host: a descriptor on POSIX, a HANDLE on Windows; no_file when absent.
using FileHandle = std::intptr_t;
inline constexpr FileHandle no_file = -1;
void close_file(FileHandle file);
// A new private output file at path for writing, refusing an existing name, a link and a second
// hard link; throws PROCESS_LOG.
FileHandle create_capture_output(const std::filesystem::path &path);
// A pipe whose write end a child inherits and whose read end stays private and non-blocking; throws
// PROCESS_PIPE.
void create_capture_pipe(FileHandle &read, FileHandle &write);
// Reads at most size bytes from a capture pipe: the byte count, 0 at end of file, -1 when nothing
// arrived within a short wait, -2 on failure.
long long read_capture(FileHandle read, char *data, std::size_t size);
// Writes at most size bytes, retrying interruptions: the positive byte count, or -1 on failure.
long long write_capture(FileHandle file, const char *data, std::size_t size);

#ifdef _WIN32
// Inheritable HANDLE positioned at the end of path, created when absent, for a child's standard
// output and error. It carries FILE_WRITE_DATA because MSYS and Cygwin children treat an
// append-only handle as read-only. Throws PROCESS_LOG when path cannot be opened.
void *child_output_handle(const std::filesystem::path &path);
#else
// The process environment block; Windows callers use the wide environment API.
char **environment();

// Whether pid names a live process visible to the caller.
bool process_exists(std::int64_t pid);
// Parent of pid, or none when the process is not visible.
std::optional<std::int64_t> parent_process(std::int64_t pid);
// Start of pid that tells it apart from a later process with the same id; none when no live
// process has that id.
std::optional<std::string> process_start_identity(std::int64_t pid);
// Executable reported by the kernel for pid; empty when it cannot be inspected.
std::filesystem::path process_executable(std::int64_t pid);
// Command-line arguments of pid, program first; empty when they are not visible.
std::vector<std::string> process_arguments(std::int64_t pid);

struct TcpEndpoint {
    bool ipv4 = false, ipv6 = false;
    std::uint32_t address4 = 0; // network byte order, valid when ipv4
    in6_addr address6{};        // valid when ipv6
    std::uint16_t port = 0;     // host byte order
};
struct TcpSocket {
    enum class State {
        listen,
        established,
        other
    };
    int family = 0; // AF_INET or AF_INET6 of the socket itself
    State state = State::other;
    TcpEndpoint local, remote;
};
// TCP stream sockets held open by pid; empty when none or when the host cannot inspect them.
std::vector<TcpSocket> process_tcp_sockets(std::int64_t pid);
#endif
} // namespace voyemu::os
