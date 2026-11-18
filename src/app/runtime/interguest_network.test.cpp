#include "interguest_network.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <chrono>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;

void close_peer(Socket s) {
    closesocket(s);
}
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __APPLE__
#include <csignal>
#include <iostream>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif
using Socket = int;

void close_peer(Socket s) {
    close(s);
}
#endif
using voyemu::InterguestNetwork;

Socket connect_peer(unsigned port) {
    Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};

    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<unsigned short>(port));
    assert(connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0);

    return s;
}

void put(Socket s, std::span<const std::uint8_t> data) {
    while (!data.empty()) {
        const int n = ::send(s, reinterpret_cast<const char *>(data.data()),
                             static_cast<int>(data.size()), 0);
        assert(n > 0);
        data = data.subspan(n);
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

#ifdef __APPLE__
pid_t spawn_owned(std::initializer_list<std::string> arguments) {
    char path[4096];
    uint32_t size = sizeof path;

    assert(_NSGetExecutablePath(path, &size) == 0);
    std::vector<char *> argv;

    for (const auto &value : arguments) {
        argv.push_back(const_cast<char *>(value.c_str()));
    }

    argv.push_back(nullptr);
    posix_spawnattr_t attributes;
    assert(posix_spawnattr_init(&attributes) == 0);
    assert(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT) == 0);
    pid_t pid = 0;
    const int result = posix_spawn(&pid, path, nullptr, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    assert(result == 0 && pid > 0);

    return pid;
}

void terminate_owned(pid_t pid) {
    assert(kill(pid, SIGTERM) == 0);
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
}
#endif

int main(int argc, char **argv) try {
#ifdef _WIN32
    if (argc == 2 && std::string(argv[1]) == "--owned-wait") {
        Sleep(15000);

        return 0;
    }

    if (argc == 3 && std::string(argv[1]) == "--owned-server") {
        WSADATA startup{};
        assert(WSAStartup(MAKEWORD(2, 2), &startup) == 0);
        Sleep(500);
        Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(listener != INVALID_SOCKET);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(std::stoul(argv[2])));
        assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
               listen(listener, 1) == 0);
        Socket peer = accept(listener, nullptr, nullptr);
        assert(peer != INVALID_SOCKET);
        close_peer(listener);
        listener = INVALID_SOCKET;
        const std::array<std::uint8_t, 18> frame{0, 0, 0, 14};
        put(peer, frame);
        std::array<char, 18> ack{};
        std::size_t used = 0;

        while (used < ack.size()) {
            const auto count = recv(peer, ack.data() + used, static_cast<int>(ack.size() - used),
                                    0);
            assert(count > 0);
            used += count;
        }

        close_peer(peer);
        close_peer(listener);
        WSACleanup();

        return 0;
    }
#else
    if (argc == 2 && std::string(argv[1]) == "--owned-wait") {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (argc == 3 && std::string(argv[1]) == "--connect") {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Socket dialed = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(dialed != Socket(-1));
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        target.sin_port = htons(static_cast<unsigned short>(std::stoul(argv[2])));
        assert(connect(dialed, reinterpret_cast<sockaddr *>(&target), sizeof target) == 0);

        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (argc == 3 && std::string(argv[1]) == "--owned-server") {
        Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(listener != Socket(-1));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(std::stoul(argv[2])));
        assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
               listen(listener, 1) == 0);
        Socket peer = accept(listener, nullptr, nullptr);
        assert(peer != Socket(-1));
        close_peer(listener);
        const std::array<std::uint8_t, 18> frame{0, 0, 0, 14};
        put(peer, frame);
        std::array<char, 18> ack{};
        std::size_t used = 0;

        while (used < ack.size()) {
            const auto count = recv(peer, ack.data() + used, static_cast<int>(ack.size() - used),
                                    0);
            assert(count > 0);
            used += count;
        }

        close_peer(peer);

        return 0;
    }
#endif
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
#ifndef _WIN32
    net.bind_peer(::getpid());
#endif
    Socket peer = connect_peer(port);
    sockaddr_in endpoint{};
#ifdef _WIN32
    int endpoint_size = sizeof endpoint;
#else
    socklen_t endpoint_size = sizeof endpoint;
#endif
    assert(getpeername(peer, reinterpret_cast<sockaddr *>(&endpoint), &endpoint_size) == 0);
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
    std::size_t used = 0;

    while (used < returned.size()) {
        const int n = recv(peer, reinterpret_cast<char *>(returned.data() + used),
                           static_cast<int>(returned.size() - used), 0);
        assert(n > 0);
        used += n;
    }

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
    close_peer(peer);
    peer = connect_peer(port);
    assert(net.accept(100));
    const std::array<std::uint8_t, 4> runt{0, 0, 0, 0};
    put(peer, runt);
    fails(
            [&] {
                net.receive(received, 100);
            },
            "INTERGUEST_FRAME");
    close_peer(peer);
    peer = connect_peer(port);
    assert(net.accept(100));
    put(peer, std::array<std::uint8_t, 2>{0, 0});
    close_peer(peer);
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
#ifdef _WIN32
    net.bind_peer(GetCurrentProcessId());
#else
    net.bind_peer(::getpid());
#endif
    // Refused/pending connects must remain bounded and recover when QEMU starts listening.
    assert(!net.connect(1));
    Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != Socket(-1));
    sockaddr_in listen_address{};
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_address.sin_port = htons(static_cast<unsigned short>(client_port));
    assert(bind(listener, reinterpret_cast<sockaddr *>(&listen_address), sizeof listen_address) ==
           0);
    assert(listen(listener, 1) == 0);
#ifdef _WIN32
    // A live owned child is still not authority for a different process listener.
    wchar_t executable[32768]{};
    assert(GetModuleFileNameW(nullptr, executable, 32768));
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --owned-wait";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION child{};
    assert(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &child));
    net.bind_peer(child.dwProcessId);
    fails(
            [&] {
                net.connect(1);
            },
            "INTERGUEST_OWNER");
    assert(!net.status().connected);
    net.bind_peer(GetCurrentProcessId());
    assert(TerminateProcess(child.hProcess, 0));
    assert(WaitForSingleObject(child.hProcess, 2000) == WAIT_OBJECT_0);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
#else
    // A live owned child is still not authority for a different process listener.
    const pid_t owned_wait = spawn_owned({argv[0], "--owned-wait"});
    net.bind_peer(static_cast<std::uint64_t>(owned_wait));
    bool rejected = false;
    try {
        rejected = !net.connect(1);
    } catch (const std::runtime_error &e) {
        rejected = std::string(e.what()).starts_with("INTERGUEST_OWNER");
    }

    assert(rejected && !net.status().connected);
    fails(
            [&] {
                net.bind_peer(1);
            },
            "INTERGUEST_OWNER");
    net.bind_peer(static_cast<std::uint64_t>(::getpid()));
    terminate_owned(owned_wait);
#endif
    auto connect_client = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        while (!net.connect(1)) {
            assert(std::chrono::steady_clock::now() < end);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    connect_client();
    peer = accept(listener, nullptr, nullptr);
    assert(peer != Socket(-1));
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
    used = 0;

    while (used < returned.size()) {
        const int n = recv(peer, reinterpret_cast<char *>(returned.data() + used),
                           static_cast<int>(returned.size() - used), 0);
        assert(n > 0);
        used += n;
    }

    assert(returned == wire);
    close_peer(peer);
    fails(
            [&] {
                net.receive(received, 100);
            },
            "INTERGUEST_SOCKET");
    assert(!net.status().connected);
    connect_client();
    peer = accept(listener, nullptr, nullptr);
    assert(peer != Socket(-1));
    put(peer, wire);
    assert(net.receive(received, 100) && received == frame);
    net.stop();
    close_peer(peer);
    close_peer(listener);
    assert(net.status().port == 0);
    fails(
            [&] {
                net.connect(0);
            },
            "INTERGUEST_STATE");
#ifdef _WIN32
    const auto child_port = net.start_client();
    command = L"\"" + std::wstring(executable) + L"\" --owned-server " +
              std::to_wstring(child_port);
    child = {};
    assert(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &child));
    net.bind_peer(child.dwProcessId);
    connect_client();
    assert(net.receive(received, 2000) && received.size() == 14);
    net.send(received, 2000);
    assert(WaitForSingleObject(child.hProcess, 3000) == WAIT_OBJECT_0);
    DWORD code = 1;
    assert(GetExitCodeProcess(child.hProcess, &code) && code == 0);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    net.stop();
#else
    const auto child_port = net.start_client();
    const pid_t owned_server = spawn_owned({argv[0], "--owned-server", std::to_string(child_port)});
    net.bind_peer(static_cast<std::uint64_t>(owned_server));
    connect_client();
    assert(net.receive(received, 2000) && received.size() == 14);
    net.send(received, 2000);
    int status = 0;
    assert(waitpid(owned_server, &status, 0) == owned_server && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0);
    net.stop();
#endif
#ifdef __APPLE__
    // Server-side ownership: the accepted peer must be the owned child, not an arbitrary dialer.
    {
        InterguestNetwork server;
        const unsigned owned_port = server.start();
        assert(owned_port > 0);
        const pid_t dialer = spawn_owned({argv[0], "--connect", std::to_string(owned_port)});
        server.bind_peer(static_cast<std::uint64_t>(dialer));
        assert(server.accept(2000) && server.status().connected);
        server.stop();
        terminate_owned(dialer);
    }
    {
        InterguestNetwork guarded;
        const unsigned guarded_port = guarded.start();
        assert(guarded_port > 0);
        const pid_t idler = spawn_owned({argv[0], "--owned-wait"});
        guarded.bind_peer(static_cast<std::uint64_t>(idler));
        Socket foreign = connect_peer(guarded_port);
        assert(!guarded.accept(200) && !guarded.status().connected);
        close_peer(foreign);
        guarded.stop();
        terminate_owned(idler);
    }
#endif
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
