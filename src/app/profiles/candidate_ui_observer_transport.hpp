#pragma once
#include "android_adb_route.hpp"
#include "error.hpp"
#include "os/net.hpp"
#include "ui_observer.hpp"
#include <charconv>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace voyemu {
// Machine preparation deliberately retains its pinned SDK ADB transport. Runtime
// observers use the embedded stream and never instantiate this adapter.
class CandidateUiObserverTransport {
public:
    CandidateUiObserverTransport(std::filesystem::path directory, Json specification,
                                 UiObserver::Adb adb)
        : directory_(std::move(directory)), specification_(std::move(specification)),
          adb_(std::move(adb)) {}

    ~CandidateUiObserverTransport() {
        if (!port_) {
            return;
        }

        try {
            const auto mappings = adb_({"forward", "--list"}, 2000);

            if (mappings.timed_out || mappings.exit_code) {
                return;
            }

            std::istringstream lines(mappings.output);
            const auto owner = android_adb_serial(specification_),
                       endpoint = "tcp:" + std::to_string(port_);
            std::string serial, local, remote;

            while (lines >> serial >> local >> remote) {
                if (serial == owner && local == endpoint && remote == remote_) {
                    adb_({"forward", "--remove", endpoint}, 2000);
                    break;
                }
            }
        } catch (...) { /* Ignored: candidate teardown still owns the preparation server. */
        }
    }

    CandidateUiObserverTransport(const CandidateUiObserverTransport &) = delete;

    UiObserverStream shell(const std::vector<std::string> &args) {
        auto process = android_adb_process(specification_, args);

        process.directory = directory_;
        process.output = directory_ / "ui-observer.log";
        process.environment["ANDROID_HOME"] =
                specification_.at("android_sdk_root").get<std::string>();
        process.environment["ANDROID_SDK_ROOT"] = process.environment.at("ANDROID_HOME");

        return UiObserverStream(std::make_shared<Shell>(ChildProcess::launch(process)));
    }

    UiObserverStream observer(std::string_view id, int timeout) {
        const auto remote = "localabstract:voyemu-ui-" + std::string(id);

        if (!port_) {
            const auto reply = adb_({"forward", "tcp:0", remote}, timeout);

            if (reply.timed_out) {
                throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer forwarding deadline elapsed");
            }

            if (reply.exit_code) {
                throw VoyemuError("TREE_PREPARE", "owned forwarding unavailable");
            }

            const auto length = reply.output.find_last_not_of("\r\n") + 1;
            unsigned port = 0;
            const auto end = reply.output.data() + length;
            const auto parsed = std::from_chars(reply.output.data(), end, port);

            if (parsed.ec != std::errc{} || parsed.ptr != end || port < 1024 || port > 65535) {
                throw VoyemuError("TREE_PREPARE", "invalid private forwarding endpoint");
            }

            port_ = port;
            remote_ = remote;
        }

        if (remote_ != remote) {
            throw VoyemuError("TREE_STALE", "preparation forward changed");
        }

        return UiObserverStream(std::make_shared<Socket>(port_));
    }

private:
    using Poll = UiObserverStream::Poll;
    struct Shell {
        ChildProcess process;

        Poll poll() {
            return {true,
                    {},
                    process.alive()
                            ? std::nullopt
                            : std::optional<ProcessResult>{{process.exit_code(), {}, false}}};
        }

        std::size_t write(std::string_view) {
            return 0;
        }
    };
    class Socket {
    public:
        explicit Socket(unsigned port) {
#ifdef _WIN32
            WSADATA network{};

            if (::WSAStartup(MAKEWORD(2, 2), &network) != 0) {
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "socket unavailable");
            }
            network_ = true;
#endif
            fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

            if (fd_ == invalid) {
                close();
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "socket unavailable");
            }

#ifdef _WIN32
            u_long nonblocking = 1;
            const bool configured = ::ioctlsocket(fd_, FIONBIO, &nonblocking) == 0;
#else
            os::suppress_sigpipe(fd_);
            const bool configured = ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL, 0) | O_NONBLOCK) ==
                                            0 &&
                                    ::fcntl(fd_, F_SETFD, FD_CLOEXEC) == 0;
#endif

            if (!configured) {
                close();
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "socket unavailable");
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(port));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            opened_ = ::connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
#ifdef _WIN32
            const bool pending = ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
            const bool pending = errno == EINPROGRESS;
#endif

            if (!opened_ && !pending) {
                close();
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "observer unavailable");
            }
        }

        ~Socket() {
            close();
        }

        Socket(const Socket &) = delete;

        Poll poll() {
            if (failed_) {
                return terminal();
            }

            if (!opened_) {
                fd_set writable, failed;
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET(fd_, &writable);
                FD_SET(fd_, &failed);
                timeval no_wait{};
#ifdef _WIN32
                const int width = 0;
                int length = sizeof(int);
#else
                const int width = fd_ + 1;
                socklen_t length = sizeof(int);
#endif
                const auto ready = ::select(width, nullptr, &writable, &failed, &no_wait);

                if (ready == 0) {
                    return {false, {}, std::nullopt};
                }

                int error = 0;

                if (ready < 0 || FD_ISSET(fd_, &failed) ||
                    ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                                 &length) != 0 ||
                    error) {
                    return terminal();
                }

                opened_ = true;
            }

            char bytes[65536];
            const auto count = ::recv(fd_, bytes, sizeof(bytes), 0);

            if (count < 0 && would_block()) {
                return {true, {}, std::nullopt};
            }

            if (count <= 0) {
                return terminal();
            }

            return {true, std::string(bytes, static_cast<std::size_t>(count)), std::nullopt};
        }

        std::size_t write(std::string_view bytes) {
            const auto count = ::send(fd_, bytes.data(), static_cast<int>(bytes.size()), 0);

            if (count > 0) {
                return static_cast<std::size_t>(count);
            }

            if (count == 0 || !would_block()) {
                failed_ = true;
            }

            return 0;
        }

    private:
        static bool would_block() {
#ifdef _WIN32
            const auto error = ::WSAGetLastError();

            return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
        }

        Poll terminal() const {
            return {opened_, {}, ProcessResult{1, {}, false}};
        }

        void close() {
#ifdef _WIN32
            if (fd_ != invalid) {
                ::closesocket(fd_);
            }

            if (network_) {
                ::WSACleanup();
            }
#else
            if (fd_ != invalid) {
                ::close(fd_);
            }
#endif
        }

#ifdef _WIN32
        static constexpr SOCKET invalid = INVALID_SOCKET;
        SOCKET fd_ = invalid;
        bool network_ = false;
#else
        static constexpr int invalid = -1;
        int fd_ = invalid;
#endif
        bool opened_ = false, failed_ = false;
    };
    std::filesystem::path directory_;
    Json specification_;
    UiObserver::Adb adb_;
    unsigned port_ = 0;
    std::string remote_;
};
} // namespace voyemu
