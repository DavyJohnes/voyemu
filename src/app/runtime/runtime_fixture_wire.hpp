#pragma once
#include "error.hpp"
#include "os/socket.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <future>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fixture_wire {
using Handle = voyemu::os::SocketHandle;
constexpr Handle invalid = voyemu::os::no_socket;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct Closed : std::exception {};
struct Socket {
    explicit Socket(Handle value) : fd(value) {}

    Socket(const Socket &) = delete;

    ~Socket() {
        close();
    }

    void close() {
        if (fd == invalid) {
            return;
        }

        voyemu::os::close_socket(fd);
        fd = invalid;
    }

    Handle fd;
};

void nonblocking(Handle fd) {
    if (!voyemu::os::set_nonblocking_socket(fd)) {
        throw voyemu::VoyemuError("FIXTURE", "fixture nonblocking socket failed");
    }

    if (!voyemu::os::suppress_socket_signal(fd)) {
        throw voyemu::VoyemuError("FIXTURE", "fixture no-SIGPIPE failed");
    }
}

class Peer {
public:
    Peer(Handle fd, std::stop_token stop) : fd_(fd), stop_(stop) {}

    std::string read(std::size_t size, std::chrono::milliseconds budget = 5s) const {
        std::string bytes(size, '\0');

        transfer(bytes.data(), size, false, Clock::now() + budget);

        return bytes;
    }

    void write(std::string_view bytes) const {
        transfer(const_cast<char *>(bytes.data()), bytes.size(), true, Clock::now() + 5s);
    }

    std::string line() const {
        const auto end = Clock::now() + 2s;
        std::string value;

        while (value.size() < 512) {
            char byte = 0;
            transfer(&byte, 1, false, end);

            if (byte == '\n') {
                return value;
            }

            if (byte != '\r') {
                value += byte;
            }
        }

        throw Closed{};
    }

private:
    void transfer(char *data, std::size_t size, bool writing, Clock::time_point end) const {
        while (size) {
            if (stop_.stop_requested() || Clock::now() >= end) {
                throw Closed{};
            }

            const auto ready = voyemu::os::wait_socket(fd_, writing, 50000);

            if (ready == -1) {
                throw Closed{};
            }

            if (!ready) {
                continue;
            }

            const auto used = writing ? voyemu::os::send_socket(fd_, data, size)
                                      : voyemu::os::receive_socket(fd_, data, size);

            if (used <= 0) {
                throw Closed{};
            }

            data += used;
            size -= static_cast<std::size_t>(used);
        }
    }

    Handle fd_;
    std::stop_token stop_;
};

class Clients {
public:
    ~Clients() {
        for (auto &worker : workers_) {
            worker.thread.request_stop();
        }
    }

    template <class Handler> void accept(Handle listener, Handler handler) {
        std::erase_if(workers_, [](const Worker &worker) {
            return worker.done.wait_for(0ms) == std::future_status::ready;
        });

        for (unsigned i = 0; i < 64; ++i) {
            const auto fd = voyemu::os::accept_socket(listener);

            if (fd == invalid) {
                break;
            }

            if (workers_.size() >= 32) {
                Socket discarded(fd);
                continue;
            }

            std::promise<void> done;
            auto completion = done.get_future();
            workers_.push_back(
                    {std::move(completion), std::jthread([fd, handler, done = std::move(done)](
                                                                 std::stop_token stop) mutable {
                         Socket socket(fd);
                         try {
                             nonblocking(fd);
                             handler(Peer(fd, stop));
                         } catch (const Closed &) { /* Ignored: a closed peer ends the exchange. */
                         } catch (...) {
                             std::fputs("fixture peer rejected protocol\n", stderr);
                         }

                         done.set_value();
                     })});
        }
    }

private:
    struct Worker {
        std::future<void> done;
        std::jthread thread;
    };
    std::vector<Worker> workers_;
};

// Legacy checksum on CNXN, negotiated checksum omission thereafter. The peer
// advertises shell v2 and uses one retained transport for sequential commands.
constexpr std::uint32_t cnxn = 0x4e584e43, open = 0x4e45504f, okay = 0x59414b4f, wrte = 0x45545257,
                        clse = 0x45534c43;
constexpr std::uint32_t version = 0x01000001, maximum_payload = 1024 * 1024;

void word(std::string &data, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        data += static_cast<char>(value >> shift);
    }
}

std::uint32_t word(std::string_view data, std::size_t offset) {
    std::uint32_t value = 0;

    for (unsigned i = 0; i < 4; ++i) {
        value |= std::uint32_t(static_cast<unsigned char>(data[offset + i])) << (8 * i);
    }

    return value;
}

std::uint32_t checksum(std::string_view bytes) {
    return std::accumulate(bytes.begin(), bytes.end(), std::uint32_t{0},
                           [](auto sum, unsigned char value) {
                               return sum + value;
                           });
}

struct Packet {
    std::uint32_t command, local, remote, check;
    std::string body;
};

Packet packet(const Peer &peer, std::chrono::milliseconds budget = 5s) {
    const auto head = peer.read(24, budget);
    const auto command = word(head, 0), length = word(head, 12);

    if (word(head, 20) != (command ^ UINT32_MAX) || length > maximum_payload) {
        throw Closed{};
    }

    return {command, word(head, 4), word(head, 8), word(head, 16), peer.read(length)};
}

void packet(const Peer &peer, std::uint32_t command, std::uint32_t local, std::uint32_t remote,
            std::string_view payload = {}) {
    std::string data;

    for (const auto value : {command, local, remote, static_cast<std::uint32_t>(payload.size()),
                             command == cnxn ? checksum(payload) : 0u, command ^ UINT32_MAX}) {
        word(data, value);
    }

    data += payload;
    peer.write(data);
}

void shell_frame(std::string &out, char kind, std::string_view data) {
    out += kind;
    word(out, static_cast<std::uint32_t>(data.size()));
    out += data;
}

struct Reply {
    int code = 0;
    std::string text;
    bool restart = false;
};

template <class Run> void adb(const Peer &peer, Run run) {
    const auto hello = packet(peer, 500ms);

    if (hello.command != cnxn || hello.local != version || !hello.body.starts_with("host::") ||
        hello.check != checksum(hello.body)) {
        throw Closed{};
    }

    packet(peer, cnxn, version, maximum_payload,
           "device::ro.product.name=VoyemuFixture;features=shell_v2");

    for (std::uint32_t id = 1;; ++id) {
        auto request = packet(peer, 1h);

        // The host can consume a shell exit and close its stream before receiving
        // our CLSE. Late acknowledgments belong to the stream just completed.
        if (request.command == clse || request.command == okay) {
            continue;
        }

        if (request.command == wrte) {
            packet(peer, clse, request.remote, request.local);
            continue;
        }

        if (request.command != open || !request.local || request.remote || request.body.empty() ||
            request.body.back() != '\0') {
            throw Closed{};
        }

        request.body.pop_back();
        constexpr std::string_view shell = "shell,v2,raw:";
        const bool framed = request.body.starts_with(shell);

        if (!framed && request.body != "root:" && request.body != "reboot:") {
            packet(peer, clse, 0, request.local);
            continue;
        }

        packet(peer, okay, id, request.local);
        auto reply = run(framed ? std::string_view(request.body).substr(shell.size())
                                : std::string_view(request.body),
                         framed);
        auto wait = [&](std::uint32_t wanted) {
            for (;;) {
                const auto response = packet(peer);

                if (response.remote && response.remote < id) {
                    if (response.command == wrte) {
                        packet(peer, clse, response.remote, response.local);
                    }

                    if (response.command == wrte || response.command == clse ||
                        response.command == okay) {
                        continue;
                    }
                }

                if ((response.local != request.local &&
                     !(response.command == clse && !response.local)) ||
                    response.remote != id) {
                    throw Closed{};
                }

                if (response.command == wrte) {
                    packet(peer, okay, id, request.local);
                    continue;
                }

                if (response.command == clse && wanted != clse) {
                    packet(peer, clse, id, request.local);

                    return false;
                }

                if (response.command != wanted) {
                    throw Closed{};
                }

                return true;
            }
        };

        if (framed) {
            std::string output;

            if (!reply.text.empty()) {
                shell_frame(output, 1, reply.text);
            }

            const auto code = static_cast<char>(reply.code);
            shell_frame(output, 3, {&code, 1});
            reply.text = std::move(output);
        }

        if (!reply.text.empty()) {
            packet(peer, wrte, id, request.local, reply.text);

            if (!wait(okay)) {
                continue;
            }
        }

        packet(peer, clse, id, request.local);

        if (reply.restart) {
            wait(clse);

            return;
        }
    }
}
} // namespace fixture_wire
