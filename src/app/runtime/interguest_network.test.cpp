#include "interguest_network.hpp"
#include "os/platform.hpp"
#include "os/process.hpp"
#include "os/socket.hpp"
#include "process.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Socket = voyemu::os::SocketHandle;
using voyemu::InterguestNetwork;
using voyemu::os::close_socket;
using voyemu::os::no_socket;

sockaddr_in loopback(unsigned port) {
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));

    return address;
}

Socket connect_peer(unsigned port) {
    const Socket s = voyemu::os::open_stream_socket(AF_INET);
    const auto address = loopback(port);

    assert(s != no_socket);
    assert(voyemu::os::connect_socket(s, reinterpret_cast<const sockaddr *>(&address),
                                      sizeof address));

    return s;
}

// A listener that no child of this test inherits; it and its peers do not block.
Socket listen_peer(unsigned port) {
    const Socket listener = voyemu::os::open_private_tcp_socket();
    const auto address = loopback(port);

    assert(listener != no_socket);
    assert(voyemu::os::bind_socket(listener, reinterpret_cast<const sockaddr *>(&address),
                                   sizeof address) &&
           voyemu::os::listen_socket(listener, 1));

    return listener;
}

// Waits up to 2 s until s can be read, or written when writing is set.
void await(Socket s, bool writing) {
    assert(voyemu::os::wait_socket(s, writing, 2000000) == 1);
}

Socket accept_peer(Socket listener) {
    await(listener, false);
    const Socket peer = voyemu::os::accept_socket(listener);
    assert(peer != no_socket);

    return peer;
}

void put(Socket s, std::span<const std::uint8_t> data) {
    while (!data.empty()) {
        await(s, true);
        const auto n = voyemu::os::send_socket(s, reinterpret_cast<const char *>(data.data()),
                                               data.size());
        assert(n > 0);
        data = data.subspan(static_cast<std::size_t>(n));
    }
}

void take(Socket s, std::span<std::uint8_t> data) {
    while (!data.empty()) {
        await(s, false);
        const auto n = voyemu::os::receive_socket(s, reinterpret_cast<char *>(data.data()),
                                                  data.size());
        assert(n > 0);
        data = data.subspan(static_cast<std::size_t>(n));
    }
}

template <class F> void fails(F f, const std::string &code) {
    try {
        f();
        assert(false);
    } catch (const std::runtime_error &e) {
        assert(std::string(e.what()).starts_with(code));
    }
}

std::vector<std::filesystem::path> child_logs;

// A child of this test running one of the modes below; its output goes to a log in child_logs.
voyemu::ChildProcess spawn_owned(std::initializer_list<std::string> arguments) {
    voyemu::ProcessSpec spec;

    spec.executable = voyemu::os::executable_path();
    spec.arguments = arguments;
    spec.output = std::filesystem::temp_directory_path() /
                  ("voyemu-interguest-network-" + std::to_string(voyemu::os::current_process()) +
                   "-" + std::to_string(child_logs.size()) + ".log");
    child_logs.push_back(spec.output);

    return voyemu::ChildProcess::launch(spec);
}

// Waits up to 3 s for child to exit and returns its exit code.
int finish_owned(voyemu::ChildProcess &child) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);

    while (child.alive()) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return child.exit_code();
}

int main(int argc, char **argv) try {
    const voyemu::os::SocketLibrary library;

    assert(!library.error());

    if (argc == 2 && std::string(argv[1]) == "--owned-wait") {
        std::this_thread::sleep_for(std::chrono::seconds(15));

        return 0;
    }

    if (argc == 3 && std::string(argv[1]) == "--connect") {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        (void)connect_peer(static_cast<unsigned>(std::stoul(argv[2])));
        std::this_thread::sleep_for(std::chrono::seconds(15));

        return 0;
    }

    if (argc == 3 && std::string(argv[1]) == "--owned-server") {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const Socket listener = listen_peer(static_cast<unsigned>(std::stoul(argv[2])));
        const Socket peer = accept_peer(listener);
        close_socket(listener);
        const std::array<std::uint8_t, 18> frame{0, 0, 0, 14};
        put(peer, frame);
        std::array<std::uint8_t, 18> ack{};
        take(peer, ack);
        close_socket(peer);

        return 0;
    }

    InterguestNetwork net;
    const unsigned port = net.start();
    assert(port > 0);
    assert(!net.accept(0));
    assert(!net.accept(1));
    fails(
            [&] {
                net.start();
            },
            "INTERGUEST_STATE");

    // Windows binds a peer process only to a client.
    if (!voyemu::os::host_windows) {
        net.bind_peer(static_cast<std::uint64_t>(voyemu::os::current_process()));
    }

    Socket peer = connect_peer(port);
    sockaddr_in endpoint{};
    voyemu::os::SocketLength endpoint_size = sizeof endpoint;
    assert(voyemu::os::peer_name(peer, reinterpret_cast<sockaddr *>(&endpoint), &endpoint_size));
    assert(endpoint.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    assert(net.accept(100));
    assert(net.status().connected);
    fails(
            [&] {
                net.accept(0);
            },
            "INTERGUEST_STATE");
    std::vector<std::uint8_t> frame(64);

    for (unsigned i = 0; i < frame.size(); ++i) {
        frame[i] = static_cast<std::uint8_t>(i);
    }

    std::vector<std::uint8_t> wire{0, 0, 0, 64};
    wire.insert(wire.end(), frame.begin(), frame.end());
    std::vector<std::uint8_t> received{99};
    put(peer, std::span(wire).first(2));
    assert(!net.receive(received, 1));
    assert(received == std::vector<std::uint8_t>{99});
    put(peer, std::span(wire).subspan(2, 15));
    assert(!net.receive(received, 1));
    put(peer, std::span(wire).subspan(17));
    assert(net.receive(received, 100));
    assert(received == frame);
    // Consecutive frames must not be collapsed or consume each other's bytes.
    put(peer, wire);
    put(peer, wire);
    assert(net.receive(received, 100));
    assert(net.receive(received, 100));
    assert(!net.receive(received, 1));
    assert(net.status().received == 3);
    net.send(frame, 100);
    std::vector<std::uint8_t> returned(wire.size());
    take(peer, returned);

    assert(returned == wire);
    assert(net.status().sent == 1);
    fails(
            [&] {
                net.send(std::vector<std::uint8_t>(2049), 100);
            },
            "INTERGUEST_FRAME");
    assert(net.status().connected);
    const std::array<std::uint8_t, 4> oversized{0, 0, 8, 1};
    put(peer, oversized);
    fails(
            [&] {
                net.receive(received, 100);
            },
            "INTERGUEST_FRAME");
    assert(!net.status().connected);
    close_socket(peer);
    peer = connect_peer(port);
    assert(net.accept(100));
    const std::array<std::uint8_t, 4> runt{0, 0, 0, 0};
    put(peer, runt);
    fails(
            [&] {
                net.receive(received, 100);
            },
            "INTERGUEST_FRAME");
    close_socket(peer);
    peer = connect_peer(port);
    assert(net.accept(100));
    put(peer, std::array<std::uint8_t, 2>{0, 0});
    close_socket(peer);
    fails(
            [&] {
                net.receive(received, 100);
            },
            "INTERGUEST_SOCKET");
    fails(
            [&] {
                net.accept(30001);
            },
            "INTERGUEST_ARGUMENT");
    net.stop();
    net.stop();
    assert(net.status().port == 0);
    fails(
            [&] {
                net.accept(0);
            },
            "INTERGUEST_STATE");
    net.start();
    assert(!net.status().connected && net.status().received == 0);
    net.stop();
    fails(
            [&] {
                net.connect(0);
            },
            "INTERGUEST_STATE");
    const auto client_port = net.start_client();
    assert(client_port > 0);
    fails(
            [&] {
                net.start();
            },
            "INTERGUEST_STATE");
    fails(
            [&] {
                net.accept(0);
            },
            "INTERGUEST_STATE");
    net.bind_peer(static_cast<std::uint64_t>(voyemu::os::current_process()));
    // Refused/pending connects must remain bounded and recover when QEMU starts listening.
    assert(!net.connect(1));
    const Socket listener = listen_peer(client_port);
    // A live owned child is still not authority for a different process listener: Windows sees
    // the foreign listener in the system table, POSIX finds no row of the child.
    auto owned_wait = spawn_owned({"--owned-wait"});
    net.bind_peer(static_cast<std::uint64_t>(owned_wait.pid()));
    bool rejected = false;
    try {
        rejected = !net.connect(1) && !voyemu::os::host_windows;
    } catch (const std::runtime_error &e) {
        rejected = std::string(e.what()).starts_with("INTERGUEST_OWNER");
    }

    assert(rejected && !net.status().connected);
    fails(
            [&] {
                net.bind_peer(1);
            },
            "INTERGUEST_OWNER");
    net.bind_peer(static_cast<std::uint64_t>(voyemu::os::current_process()));
    owned_wait.terminate(0);
    auto connect_client = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        while (!net.connect(1)) {
            assert(std::chrono::steady_clock::now() < end);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    connect_client();
    peer = accept_peer(listener);
    assert(net.status().connected);
    fails(
            [&] {
                net.connect(1);
            },
            "INTERGUEST_STATE");
    put(peer, std::span(wire).first(2));
    assert(!net.receive(received, 1));
    put(peer, std::span(wire).subspan(2));
    assert(net.receive(received, 100));
    assert(received == frame);
    net.send(frame, 100);
    returned.assign(wire.size(), 0);
    take(peer, returned);

    assert(returned == wire);
    close_socket(peer);
    fails(
            [&] {
                net.receive(received, 100);
            },
            "INTERGUEST_SOCKET");
    assert(!net.status().connected);
    connect_client();
    peer = accept_peer(listener);
    put(peer, wire);
    assert(net.receive(received, 100) && received == frame);
    net.stop();
    close_socket(peer);
    close_socket(listener);
    assert(net.status().port == 0);
    fails(
            [&] {
                net.connect(0);
            },
            "INTERGUEST_STATE");
    const auto child_port = net.start_client();
    auto owned_server = spawn_owned({"--owned-server", std::to_string(child_port)});
    net.bind_peer(static_cast<std::uint64_t>(owned_server.pid()));
    connect_client();
    assert(net.receive(received, 2000) && received.size() == 14);
    net.send(received, 2000);
    assert(finish_owned(owned_server) == 0);
    net.stop();

    // Server-side ownership: the accepted peer must be the owned child, not an arbitrary dialer.
    // Windows binds no peer process to a listener.
    if (!voyemu::os::host_windows) {
        {
            InterguestNetwork server;
            const unsigned owned_port = server.start();
            assert(owned_port > 0);
            auto dialer = spawn_owned({"--connect", std::to_string(owned_port)});
            server.bind_peer(static_cast<std::uint64_t>(dialer.pid()));
            assert(server.accept(2000) && server.status().connected);
            server.stop();
            dialer.terminate(0);
        }
        {
            InterguestNetwork guarded;
            const unsigned guarded_port = guarded.start();
            assert(guarded_port > 0);
            auto idler = spawn_owned({"--owned-wait"});
            guarded.bind_peer(static_cast<std::uint64_t>(idler.pid()));
            Socket foreign = connect_peer(guarded_port);
            assert(!guarded.accept(200) && !guarded.status().connected);
            close_socket(foreign);
            guarded.stop();
            idler.terminate(0);
        }
    }

    for (const auto &log : child_logs) {
        std::error_code ignored;
        std::filesystem::remove(log, ignored);
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
