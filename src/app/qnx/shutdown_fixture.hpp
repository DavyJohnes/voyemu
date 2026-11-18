#pragma once
#include "error.hpp"
// Synthetic owned process used by the adjacent monitor and Session lifecycle tests; no VM.
#ifndef _WIN32
#include "model.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <source_location>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
namespace voyemu::shutdown_fixture {
inline void need(bool yes, const std::source_location where = std::source_location::current()) {
    if (!yes) {
        throw VoyemuError("FIXTURE",
                          "shutdown fixture failed line " + std::to_string(where.line()));
    }
}

inline int listen(const std::filesystem::path &path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);

    need(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto text = path.string();
    need(text.size() < sizeof address.sun_path);
    std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
    need(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
         ::listen(fd, 8) == 0);

    return fd;
}

inline void send(int fd, std::string_view text) {
    while (!text.empty()) {
        const auto count = ::write(fd, text.data(), text.size());
        need(count > 0);
        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

inline Json line(int fd) {
    std::string text;

    for (;;) {
        pollfd p{fd, POLLIN, 0};
        need(::poll(&p, 1, 5000) > 0);
        char c;
        need(::read(fd, &c, 1) == 1);

        if (c == '\n') {
            return Json::parse(text);
        }

        text += c;
        need(text.size() < 16384);
    }
}

inline std::string greeting(bool wrong = false) {
    return Json{{"QMP",
                 {{"version",
                   {{"qemu", {{"major", 11}, {"minor", 1}, {"micro", wrong ? 3 : 2}}}}}}}}
                   .dump() +
           "\n";
}

inline int run(int argc, char **argv) {
    need(argc >= 8);
    std::signal(SIGPIPE, SIG_IGN);
    ::umask(0022);
    const std::string scenario = argv[2];
    const std::filesystem::path state = argv[3], primary_path = argv[4], userdata = argv[5],
                                ram = argv[7];
    std::filesystem::path events = argv[6];

    if (events == "auto") {
        for (int i = 8; i + 1 < argc; ++i) {
            if (std::string_view(argv[i]) == "-qmp") {
                std::string arg = argv[i + 1];
                need(arg.starts_with("unix:"));
                arg.erase(0, 5);
                events = arg.substr(0, arg.find(','));
            }
        }
    }

    unsigned boot = 0;
    {
        std::ifstream old(state / "boots");
        old >> boot;
    }
    ++boot;
    {
        std::ofstream next(state / "boots");
        next << boot;
    }
    int primary = -1;

    if (primary_path != "-") {
        primary = listen(primary_path);
    }

    const int listener = listen(events);
    {
        std::ofstream ready(state / "ready");
        ready << boot;
    }
    const int event_fd = ::accept(listener, nullptr, nullptr);
    need(event_fd >= 0);

    if (scenario == "swapped") {
        std::filesystem::rename(events, events.string() + ".retained");
        std::ofstream other(events);
        other << "foreign replacement";
    }

    send(event_fd, greeting(scenario == "version"));
    const auto capabilities = line(event_fd);
    need(capabilities.at("execute") == "qmp_capabilities");
    Json acknowledgement{{"id", capabilities.at("id")}};
    acknowledgement[scenario == "capabilities" ? "error" : "return"] = Json::object();
    send(event_fd, acknowledgement.dump() + "\n");
    {
        std::ofstream mark(state / ("subscribed-" + std::to_string(boot)));
        mark << "subscribed";
    }
    int ram_fd = -1;
    std::uint32_t *words = nullptr;
    void *mapping = MAP_FAILED;

    if (ram != "-") {
        ram_fd = ::open(ram.c_str(), O_RDWR);
        need(ram_fd >= 0);
        char old[18]{};
        need(::pread(ram_fd, old, sizeof old, 0x200000) == sizeof old);
        {
            std::ofstream record(state / ("fresh-ram-" + std::to_string(boot)));
            record << (std::string(old, sizeof old) == std::string(sizeof old, '\0') ? "fresh"
                                                                                     : "retained");
        }
        const std::string marker = "old-generation-ram";
        need(::pwrite(ram_fd, marker.data(), marker.size(), 0x200000) ==
             static_cast<ssize_t>(marker.size()));
        mapping = ::mmap(nullptr, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, ram_fd, 0);
        need(mapping != MAP_FAILED);
        words = static_cast<std::uint32_t *>(mapping) + 1024;
        words[0] = 0x54454e51;
        words[1] = 1;
        const std::string ready = "QNET READY pid=42 paddr=0x40001000 length=4096 version=1\n";
        std::array<std::uint32_t, 8> ring{0xbeefeeee, 16352,
                                          static_cast<std::uint32_t>(ready.size())};
        need(::pwrite(ram_fd, ring.data(), 32, 0x10010000) == 32 &&
             ::pwrite(ram_fd, ready.data(), ready.size(), 0x10010020) ==
                     static_cast<ssize_t>(ready.size()));
    }

    bool running = false, continued = primary < 0;
    const auto control = [&] {
        const int fd = ::accept(primary, nullptr, nullptr);
        need(fd >= 0);
        send(fd, greeting());

        for (;;) {
            pollfd p{fd, POLLIN, 0};
            need(::poll(&p, 1, 5000) > 0);
            char peek;
            const auto count = ::recv(fd, &peek, 1, MSG_PEEK);

            if (count <= 0) {
                break;
            }

            const auto request = line(fd);
            const auto command = request.at("execute").get<std::string>();
            Json result = Json::object();

            if (command == "cont") {
                need(std::filesystem::exists(state / ("subscribed-" + std::to_string(boot))));
                continued = true;
                running = true;
            } else if (command == "stop") {
                running = false;
            } else if (command == "query-status") {
                result = {{"running", running}};
            } else {
                need(command == "qmp_capabilities");
            }

            Json reply{{"return", result}};

            if (request.contains("id")) {
                reply["id"] = request.at("id");
            }

            send(fd, reply.dump() + "\n");
        }

        ::close(fd);
    };

    if (primary >= 0) {
        control();
    }

    if (userdata != "-") {
        if (boot == 1) {
            std::fstream disk(userdata, std::ios::in | std::ios::out | std::ios::binary);
            disk.seekp(4096);
            disk << "guest-data";
            disk.flush();
            need(bool(disk));
        } else {
            std::ifstream disk(userdata, std::ios::binary);
            disk.seekg(4096);
            char bytes[10]{};
            disk.read(bytes, 10);
            need(disk.gcount() == 10);
            std::ofstream record(state / ("disk-readback-" + std::to_string(boot)));
            record.write(bytes, 10);
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (!std::filesystem::exists(state / ("release-" + std::to_string(boot)))) {
        need(continued && std::chrono::steady_clock::now() < deadline);

        if (words) {
            const auto utc = __atomic_load_n(words + 12, __ATOMIC_ACQUIRE);

            if (utc) {
                words[15] = words[10];
                words[14] = 0;
                __atomic_store_n(words + 13, utc, __ATOMIC_RELEASE);
            }

            const auto flush = __atomic_load_n(words + 784, __ATOMIC_ACQUIRE);

            if (flush) {
                words[786] = 0;
                __atomic_store_n(words + 785, flush, __ATOMIC_RELEASE);
            }
        }

        if (primary >= 0) {
            pollfd p{primary, POLLIN, 0};

            if (::poll(&p, 1, 1) > 0) {
                control();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::string reason = scenario;

    if (boot > 1 && scenario != "repeat") {
        reason = "guest-shutdown";
    }

    if (reason == "false-reset") {
        reason = "guest-reset";
    }

    if (reason == "repeat" || reason == "exit7" || reason == "fragmented" || reason == "swapped" ||
        reason == "version" || reason == "capabilities") {
        reason = "guest-reset";
    }

    const auto event = [&](std::string why, bool guest) {
        return Json{{"event", "SHUTDOWN"}, {"data", {{"guest", guest}, {"reason", why}}}}.dump() +
               "\n";
    };

    if (scenario == "malformed") {
        send(event_fd, "{\"event\":\"SHUTDOWN\",\"data\":{\"guest\":true,\"reason\":7}}\n");
    } else if (scenario == "truncated") {
        send(event_fd, "{\"event\":\"SHUTDOWN\",\"data\":");
    } else if (scenario == "conflict") {
        send(event_fd, event("guest-reset", true));
        send(event_fd, event("guest-shutdown", true));
    } else if (scenario != "none") {
        const auto message = event(reason,
                                   scenario != "false-reset" && !reason.starts_with("host-"));

        if (scenario == "fragmented") {
            send(event_fd, std::string_view(message).substr(0, 10));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            send(event_fd, std::string_view(message).substr(10));
        } else {
            send(event_fd, message);
        }
    }

    if (mapping != MAP_FAILED) {
        ::munmap(mapping, 16384);
    }

    if (ram_fd >= 0) {
        ::close(ram_fd);
    }

    ::close(event_fd);
    ::close(listener);

    if (primary >= 0) {
        ::close(primary);
    }

    return scenario == "exit7" ? 7 : 0;
}
} // namespace voyemu::shutdown_fixture
#else
// The Windows cluster QMP channel is loopback TCP, so the same synthetic event
// stream is served over TCP. argv[4] is the primary QMP port ("-" when absent),
// argv[6] is the requested event port or "auto" to parse the appended -qmp tcp:...
// argument. The primary control loop and the shared-RAM relay mirror macOS.
#include "model.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <thread>
namespace voyemu::shutdown_fixture {
inline void need(bool yes, const std::source_location where = std::source_location::current()) {
    if (!yes) {
        throw VoyemuError("FIXTURE",
                          "shutdown fixture failed line " + std::to_string(where.line()));
    }
}

inline std::string greeting(bool wrong = false) {
    return Json{{"QMP",
                 {{"version",
                   {{"qemu", {{"major", 11}, {"minor", 1}, {"micro", wrong ? 3 : 2}}}}}}}}
                   .dump() +
           "\n";
}

inline void send(SOCKET fd, std::string_view text) {
    while (!text.empty()) {
        const auto count = ::send(fd, text.data(), static_cast<int>(text.size()), 0);
        need(count > 0);
        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

inline Json line(SOCKET fd) {
    std::string text;

    for (;;) {
        char c = '\0';
        need(::recv(fd, &c, 1, 0) == 1);

        if (c == '\n') {
            return Json::parse(text);
        }

        text += c;
        need(text.size() < 16384);
    }
}

inline SOCKET listen_port(unsigned short port) {
    const SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    need(fd != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof one);
    need(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
         ::listen(fd, 8) == 0);

    return fd;
}

inline void write_at(HANDLE file, std::uint64_t offset, const void *data, std::size_t size) {
    LARGE_INTEGER at{};

    at.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;
    need(SetFilePointerEx(file, at, nullptr, FILE_BEGIN) &&
         WriteFile(file, data, static_cast<DWORD>(size), &count, nullptr) && count == size);
}

inline bool read_at(HANDLE file, std::uint64_t offset, void *data, std::size_t size) {
    LARGE_INTEGER at{};

    at.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;

    return SetFilePointerEx(file, at, nullptr, FILE_BEGIN) &&
           ReadFile(file, data, static_cast<DWORD>(size), &count, nullptr) &&
           count == static_cast<DWORD>(size);
}

inline int run(int argc, char **argv) {
    need(argc >= 8);
    const std::string scenario = argv[2];
    const std::filesystem::path state = argv[3];
    const std::string primary = argv[4];
    const std::filesystem::path userdata = argv[5];
    const std::filesystem::path ram = argv[7];
    std::string events = argv[6];

    if (events == "auto") {
        for (int i = 8; i + 1 < argc; ++i) {
            if (std::string_view(argv[i]) == "-qmp") {
                std::string arg = argv[i + 1];
                need(arg.starts_with("tcp:"));
                arg.erase(0, 4);
                arg = arg.substr(0, arg.find(','));
                events = arg.substr(arg.rfind(':') + 1);
            }
        }
    }

    unsigned boot = 0;
    {
        std::ifstream old(state / "boots");
        old >> boot;
    }
    ++boot;
    {
        std::ofstream next(state / "boots");
        next << boot;
    }
    WSADATA startup{};
    need(WSAStartup(MAKEWORD(2, 2), &startup) == 0);
    const bool has_primary = primary != "-";
    SOCKET primary_listener = INVALID_SOCKET;

    if (has_primary) {
        primary_listener = listen_port(static_cast<unsigned short>(std::stoul(primary)));
    }

    const SOCKET listener = listen_port(static_cast<unsigned short>(std::stoul(events)));
    {
        // Publish the bound event port whole: the owner reads it as soon as the file exists.
        sockaddr_in bound{};
        int length = sizeof bound;
        need(::getsockname(listener, reinterpret_cast<sockaddr *>(&bound), &length) == 0);
        {
            std::ofstream ready(state / "ready.pending");
            ready << ntohs(bound.sin_port);
        }
        std::filesystem::rename(state / "ready.pending", state / "ready");
    }
    const SOCKET event_fd = ::accept(listener, nullptr, nullptr);
    need(event_fd != INVALID_SOCKET);
    send(event_fd, greeting(scenario == "version"));
    const auto capabilities = line(event_fd);
    need(capabilities.at("execute") == "qmp_capabilities");
    Json acknowledgement{{"id", capabilities.at("id")}};
    acknowledgement[scenario == "capabilities" ? "error" : "return"] = Json::object();
    send(event_fd, acknowledgement.dump() + "\n");
    {
        std::ofstream mark(state / ("subscribed-" + std::to_string(boot)));
        mark << "subscribed";
    }
    HANDLE ram_handle = INVALID_HANDLE_VALUE, ram_mapping = nullptr;
    void *ram_view = nullptr;
    std::uint32_t *words = nullptr;

    if (ram != "-") {
        ram_handle = CreateFileW(ram.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        need(ram_handle != INVALID_HANDLE_VALUE);
        char old[18]{};
        need(read_at(ram_handle, 0x200000, old, sizeof old));
        {
            std::ofstream record(state / ("fresh-ram-" + std::to_string(boot)));
            record << (std::string(old, sizeof old) == std::string(sizeof old, '\0') ? "fresh"
                                                                                     : "retained");
        }
        const std::string marker = "old-generation-ram";
        write_at(ram_handle, 0x200000, marker.data(), marker.size());
        ram_mapping = CreateFileMappingW(ram_handle, nullptr, PAGE_READWRITE, 0, 16384, nullptr);
        need(ram_mapping != nullptr);
        ram_view = MapViewOfFile(ram_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 16384);
        need(ram_view != nullptr);
        words = static_cast<std::uint32_t *>(ram_view) + 1024;
        words[0] = 0x54454e51;
        words[1] = 1;
        const std::string ready = "QNET READY pid=42 paddr=0x40001000 length=4096 version=1\n";
        std::array<std::uint32_t, 8> ring{0xbeefeeee, 16352,
                                          static_cast<std::uint32_t>(ready.size())};
        write_at(ram_handle, 0x10010000, ring.data(), 32);
        write_at(ram_handle, 0x10010020, ready.data(), ready.size());
    }

    bool running = false, continued = !has_primary;
    const auto control = [&] {
        const SOCKET fd = ::accept(primary_listener, nullptr, nullptr);
        need(fd != INVALID_SOCKET);
        send(fd, greeting());

        for (;;) {
            fd_set selected;
            FD_ZERO(&selected);
            FD_SET(fd, &selected);
            timeval timeout{5, 0};
            need(::select(0, &selected, nullptr, nullptr, &timeout) > 0);
            char peek = '\0';
            const auto count = ::recv(fd, &peek, 1, MSG_PEEK);

            if (count <= 0) {
                break;
            }

            const auto request = line(fd);
            const auto command = request.at("execute").get<std::string>();
            Json result = Json::object();

            if (command == "cont") {
                need(std::filesystem::exists(state / ("subscribed-" + std::to_string(boot))));
                continued = true;
                running = true;
            } else if (command == "stop") {
                running = false;
            } else if (command == "query-status") {
                result = {{"running", running}};
            } else {
                need(command == "qmp_capabilities");
            }

            Json reply{{"return", result}};

            if (request.contains("id")) {
                reply["id"] = request.at("id");
            }

            send(fd, reply.dump() + "\n");
        }

        ::closesocket(fd);
    };

    if (has_primary) {
        control();
    }

    if (userdata != "-") {
        if (boot == 1) {
            std::fstream disk(userdata, std::ios::in | std::ios::out | std::ios::binary);
            disk.seekp(4096);
            disk << "guest-data";
            disk.flush();
            need(bool(disk));
        } else {
            std::ifstream disk(userdata, std::ios::binary);
            disk.seekg(4096);
            char bytes[10]{};
            disk.read(bytes, 10);
            need(disk.gcount() == 10);
            std::ofstream record(state / ("disk-readback-" + std::to_string(boot)));
            record.write(bytes, 10);
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (!std::filesystem::exists(state / ("release-" + std::to_string(boot)))) {
        need(continued && std::chrono::steady_clock::now() < deadline);

        if (words) {
            const auto utc = __atomic_load_n(words + 12, __ATOMIC_ACQUIRE);

            if (utc) {
                words[15] = words[10];
                words[14] = 0;
                __atomic_store_n(words + 13, utc, __ATOMIC_RELEASE);
            }

            const auto flush = __atomic_load_n(words + 784, __ATOMIC_ACQUIRE);

            if (flush) {
                words[786] = 0;
                __atomic_store_n(words + 785, flush, __ATOMIC_RELEASE);
            }
        }

        if (has_primary) {
            fd_set selected;
            FD_ZERO(&selected);
            FD_SET(primary_listener, &selected);
            timeval timeout{0, 1000};

            if (::select(0, &selected, nullptr, nullptr, &timeout) > 0) {
                control();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::string reason = scenario;

    if (boot > 1 && scenario != "repeat") {
        reason = "guest-shutdown";
    }

    if (reason == "false-reset") {
        reason = "guest-reset";
    }

    if (reason == "repeat" || reason == "exit7" || reason == "fragmented" || reason == "version" ||
        reason == "capabilities") {
        reason = "guest-reset";
    }

    const auto event = [&](std::string why, bool guest) {
        return Json{{"event", "SHUTDOWN"}, {"data", {{"guest", guest}, {"reason", why}}}}.dump() +
               "\n";
    };

    if (scenario == "malformed") {
        send(event_fd, "{\"event\":\"SHUTDOWN\",\"data\":{\"guest\":true,\"reason\":7}}\n");
    } else if (scenario == "truncated") {
        send(event_fd, "{\"event\":\"SHUTDOWN\",\"data\":");
    } else if (scenario == "conflict") {
        send(event_fd, event("guest-reset", true));
        send(event_fd, event("guest-shutdown", true));
    } else if (scenario != "none") {
        const auto message = event(reason,
                                   scenario != "false-reset" && !reason.starts_with("host-"));

        if (scenario == "fragmented") {
            send(event_fd, std::string_view(message).substr(0, 10));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            send(event_fd, std::string_view(message).substr(10));
        } else {
            send(event_fd, message);
        }
    }

    if (ram_view) {
        UnmapViewOfFile(ram_view);
    }

    if (ram_mapping) {
        CloseHandle(ram_mapping);
    }

    if (ram_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(ram_handle);
    }

    ::closesocket(event_fd);
    ::closesocket(listener);

    if (has_primary) {
        ::closesocket(primary_listener);
    }

    WSACleanup();

    return scenario == "exit7" ? 7 : 0;
}
} // namespace voyemu::shutdown_fixture
#endif
