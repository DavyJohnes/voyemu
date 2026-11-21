#pragma once
#include "error.hpp"
#include "os/socket.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace voyemu {
namespace android_console_detail {
using Clock = std::chrono::steady_clock;
struct Timeout : VoyemuError {
    Timeout() : VoyemuError("ANDROID_CONSOLE_TIMEOUT", "SDK console socket deadline expired") {}
};
struct Disconnected : VoyemuError {
    Disconnected() : VoyemuError("ANDROID_CONSOLE_IO", "SDK console socket failed") {}
};

// A non-blocking loopback TCP socket of the SDK console.
class Socket {
public:
    Socket();
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    // Binds an ephemeral loopback port without listening and returns it.
    unsigned bind_inactive();
    void connect(unsigned port, Clock::time_point deadline);
    void send(std::string_view text, Clock::time_point deadline);
    std::string receive(Clock::time_point deadline);

private:
    void ready(bool writing, Clock::time_point deadline);
    os::SocketLibrary network_;
    os::SocketHandle fd_ = os::no_socket;
};

// The SDK console authentication token of home, read from an ordinary single-link file.
std::string token(const std::filesystem::path &home);
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
