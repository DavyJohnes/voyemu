#pragma once
#include "android_adb_route.hpp"
#include "error.hpp"
#include "os/socket.hpp"
#include "ui_observer.hpp"
#include <charconv>
#include <sstream>

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
            if (library_.error()) {
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "socket unavailable");
            }

            fd_ = os::open_stream_socket(AF_INET);

            if (fd_ == os::no_socket) {
                close();
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "socket unavailable");
            }

            (void)os::suppress_socket_signal(fd_);

            if (!os::set_nonblocking_socket(fd_)) {
                close();
                throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "socket unavailable");
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(port));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            opened_ = os::connect_socket(fd_, reinterpret_cast<sockaddr *>(&address),
                                         sizeof(address));

            if (!opened_ && !os::socket_connecting()) {
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
                const auto ready = os::wait_socket(fd_, true, 0);

                if (ready == 0) {
                    return {false, {}, std::nullopt};
                }

                if (ready < 0 || os::pending_socket_error(fd_) != 0) {
                    return terminal();
                }

                opened_ = true;
            }

            char bytes[65536];
            const auto count = os::receive_socket(fd_, bytes, sizeof(bytes));

            if (count < 0 && would_block()) {
                return {true, {}, std::nullopt};
            }

            if (count <= 0) {
                return terminal();
            }

            return {true, std::string(bytes, static_cast<std::size_t>(count)), std::nullopt};
        }

        std::size_t write(std::string_view bytes) {
            const auto count = os::send_socket(fd_, bytes.data(), bytes.size());

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
            return os::socket_would_block() || os::socket_interrupted();
        }

        Poll terminal() const {
            return {opened_, {}, ProcessResult{1, {}, false}};
        }

        void close() {
            if (fd_ != os::no_socket) {
                os::close_socket(fd_);
            }
        }

        os::SocketLibrary library_;
        os::SocketHandle fd_ = os::no_socket;
        bool opened_ = false, failed_ = false;
    };
    std::filesystem::path directory_;
    Json specification_;
    UiObserver::Adb adb_;
    unsigned port_ = 0;
    std::string remote_;
};
} // namespace voyemu
