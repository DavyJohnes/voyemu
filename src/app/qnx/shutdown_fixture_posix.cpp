#include "error.hpp"
#include "model.hpp"
#include "shutdown_fixture.hpp"
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

int run(int argc, char **argv) {
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
