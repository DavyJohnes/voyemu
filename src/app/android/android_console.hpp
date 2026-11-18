#pragma once
#include "error.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voyemu {
namespace android_console_detail {
using Clock = std::chrono::steady_clock;
struct Timeout : VoyemuError {
    Timeout() : VoyemuError("ANDROID_CONSOLE_TIMEOUT", "SDK console socket deadline expired") {}
};
struct Disconnected : VoyemuError {
    Disconnected() : VoyemuError("ANDROID_CONSOLE_IO", "SDK console socket failed") {}
};

class Socket {
#ifdef _WIN32
    struct Network {
        Network() {
            WSADATA data{};

            if (WSAStartup(MAKEWORD(2, 2), &data)) {
                throw Disconnected{};
            }
        }

        ~Network() {
            WSACleanup();
        }
    } network_;
    using Handle = SOCKET;
    using Length = int;
    static constexpr auto invalid = INVALID_SOCKET;

    static void close(Handle fd) {
        closesocket(fd);
    }

    static bool interrupted() {
        return WSAGetLastError() == WSAEINTR;
    }

    static bool would_block() {
        return WSAGetLastError() == WSAEWOULDBLOCK;
    }
#else
    using Handle = int;
    using Length = socklen_t;
    static constexpr auto invalid = -1;

    static void close(Handle fd) {
        ::close(fd);
    }

    static bool interrupted() {
        return errno == EINTR;
    }

    static bool would_block() {
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
#endif
    Handle fd_ = invalid;

public:
    Socket() {
#ifdef _WIN32
        fd_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
        u_long nonblocking = 1;
        const bool configured = fd_ != invalid && ioctlsocket(fd_, FIONBIO, &nonblocking) == 0;
#else
        fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        bool configured = fd_ != invalid && fcntl(fd_, F_SETFD, FD_CLOEXEC) == 0 &&
                          fcntl(fd_, F_SETFL, O_NONBLOCK) == 0;
#ifdef SO_NOSIGPIPE
        int enabled = 1;
        configured = configured &&
                     setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#endif
#endif

        if (!configured) {
            if (fd_ != invalid) {
                close(fd_);
            }

            throw Disconnected{};
        }
    }

    ~Socket() {
        close(fd_);
    }

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    unsigned bind_inactive() {
        sockaddr_in address = loopback(0);

#ifdef _WIN32
        int exclusive = 1;

        if (setsockopt(fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char *>(&exclusive), sizeof(exclusive))) {
            throw Disconnected{};
        }
#endif
        Length size = sizeof(address);

        if (::bind(fd_, reinterpret_cast<const sockaddr *>(&address), size) ||
            getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size)) {
            throw Disconnected{};
        }

        return ntohs(address.sin_port);
    }

    void connect(unsigned port, Clock::time_point deadline) {
        const sockaddr_in address = loopback(port);

        if (!::connect(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address))) {
            return;
        }

#ifdef _WIN32
        if (!would_block()) {
            throw Disconnected{};
        }
#else
        if (errno != EINPROGRESS && !interrupted()) {
            throw Disconnected{};
        }
#endif
        ready(true, deadline);
        int error = 0;
        Length size = sizeof(error);

        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) ||
            error) {
            throw Disconnected{};
        }
    }

    void send(std::string_view text, Clock::time_point deadline) {
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

    std::string receive(Clock::time_point deadline) {
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

private:
    static sockaddr_in loopback(unsigned port) {
        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(port));

        return address;
    }

    void ready(bool writing, Clock::time_point deadline) {
        for (;;) {
            const auto left =
                    std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now())
                            .count();

            if (left <= 0) {
                throw Timeout{};
            }

#ifdef _WIN32
            fd_set selected, failed;
            FD_ZERO(&selected);
            FD_SET(fd_, &selected);
            FD_ZERO(&failed);
            FD_SET(fd_, &failed);
            timeval budget{static_cast<long>(left / 1000000), static_cast<long>(left % 1000000)};
            const int result = ::select(0, writing ? nullptr : &selected,
                                        writing ? &selected : nullptr, &failed, &budget);

            if (result > 0 && !FD_ISSET(fd_, &failed)) {
                return;
            }
#else
            pollfd selected{fd_, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
            const int result = ::poll(&selected, 1, static_cast<int>((left + 999) / 1000));

            if (result > 0 && (selected.revents & selected.events)) {
                return;
            }
#endif

            if (result < 0 && interrupted()) {
                continue;
            }

            if (result == 0) {
                throw Timeout{};
            }

            throw Disconnected{};
        }
    }
};

inline std::string token(const std::filesystem::path &home) {
    const auto path = home / ".emulator_console_auth_token";
    std::array<char, 257> data{};

#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw Disconnected{};
    }

    struct Close {
        HANDLE file;

        ~Close() {
            CloseHandle(file);
        }
    } close{file};
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD bytes = 0;

    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &bytes, nullptr)) {
        throw Disconnected{};
    }
#else
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
#endif

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
} // namespace android_console_detail

// The SDK still tries to notify a host ADB server. A bound, non-listening local
// socket makes that notification fail without allowing an external server to take its port.
class AndroidSdkAdbSink {
    std::optional<android_console_detail::Socket> socket_;
    unsigned port_ = 0;

public:
    AndroidSdkAdbSink() {
        try {
            socket_.emplace();
            port_ = socket_->bind_inactive();
        } catch (const android_console_detail::Disconnected &) {
            throw VoyemuError("ANDROID_ADB_OWNER", "SDK notification port unavailable");
        }
    }

    unsigned port() const {
        return port_;
    }
};

inline ProcessResult android_console_command(unsigned port, const std::filesystem::path &home,
                                             const std::vector<std::string> &arguments,
                                             int timeout_ms, std::size_t limit,
                                             const std::function<void()> &fence) {
    using namespace android_console_detail;
    const bool kill = arguments == std::vector<std::string>{"emu", "kill"};

    if (!kill && arguments != std::vector<std::string>{"emu", "avd", "name"} &&
        arguments != std::vector<std::string>{"emu", "avd", "snapshot", "save", "voyemu-last"} &&
        arguments != std::vector<std::string>{"emu", "multidisplay", "add", "1", "1920", "721",
                                              "160", "0"} &&
        arguments != std::vector<std::string>{"emu", "multidisplay", "add", "1", "1920", "720",
                                              "160", "0"}) {
        throw VoyemuError("ANDROID_CONSOLE_COMMAND", "fixed owned SDK command required");
    }

    if (port < 5554 || port > 5682 || port % 2 || port == 5562 || !fence) {
        throw VoyemuError("ANDROID_CONSOLE_OWNER", "owned SDK console required");
    }

    if (timeout_ms < 1) {
        return {1, {}, true};
    }

    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    try {
        Socket socket;
        fence();
        socket.connect(port, deadline);
        fence();
        std::string carry;
        auto receive_reply = [&](std::size_t bound, bool allow_kill) {
            ProcessResult reply{1, {}};

            for (;;) {
                const auto end = carry.find('\n');
                const auto available = end == std::string::npos ? carry.size() : end + 1;

                if (available > bound - reply.output.size()) {
                    throw Disconnected{};
                }

                if (end == std::string::npos) {
                    carry += socket.receive(deadline);
                    continue;
                }

                auto line = carry.substr(0, end);
                carry.erase(0, end + 1);

                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                reply.output += line + '\n';

                if (line == "OK" || (allow_kill && line.starts_with("OK:"))) {
                    reply.exit_code = 0;

                    return reply;
                }

                if (line == "KO" || line.starts_with("KO:")) {
                    return reply;
                }
            }
        };
        const auto greeting = receive_reply(4096, false);

        if (greeting.exit_code ||
            !greeting.output.starts_with("Android Console: Authentication required\n")) {
            return {1, "ANDROID_CONSOLE_AUTH: authenticated SDK greeting required"};
        }

        socket.send("auth " + token(home) + "\n", deadline);

        if (receive_reply(4096, false).exit_code) {
            return {1, "ANDROID_CONSOLE_AUTH: session token rejected"};
        }

        std::string command;

        for (std::size_t i = 1; i < arguments.size(); ++i) {
            if (i > 1) {
                command += ' ';
            }

            command += arguments[i];
        }

        fence();
        socket.send(command + "\n", deadline);

        return receive_reply(limit, kill);
    } catch (const Timeout &) {
        return {1, {}, true};
    } catch (const Disconnected &) {
        return {1, "ANDROID_CONSOLE_IO: owned SDK console unavailable"};
    }
}
} // namespace voyemu
