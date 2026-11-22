#include "os/platform.hpp"
#include "os/process.hpp"
#include "session.hpp"
#include "session_internal.hpp"
#include "session_test_host.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
using namespace voyemu;

namespace {
int unix_qmp_fixture(const char *socket_path, const char *log_path) {
    std::signal(SIGPIPE, SIG_IGN);
    ::umask(0022);
    const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    assert(std::strlen(socket_path) < sizeof address.sun_path);
    std::strcpy(address.sun_path, socket_path);
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    struct stat mode{};
    assert(::lstat(socket_path, &mode) == 0 && (mode.st_mode & 0777) == 0755);
    assert(::listen(listener, 4) == 0);
    {
        std::ofstream ready(std::string(log_path) + ".ready");
        ready << "ready";
    }
    bool running = true;

    for (;;) {
        const int fd = ::accept(listener, nullptr, nullptr);
        assert(fd >= 0);

        if (std::string(log_path).ends_with("swap.log")) {
            std::filesystem::rename(socket_path, std::string(socket_path) + ".retained");
            std::ofstream replacement(socket_path);
            replacement << "replacement";
        }

        const std::string greeting = "{\"QMP\":{}}\n";

        if (::write(fd, greeting.data(), greeting.size()) !=
            static_cast<ssize_t>(greeting.size())) {
            ::close(fd);
            continue;
        }

        std::string line;
        char byte;

        while (::read(fd, &byte, 1) == 1) {
            if (byte != '\n') {
                line += byte;
                continue;
            }

            const auto request = Json::parse(line);
            line.clear();
            const auto command = request.at("execute").get<std::string>();
            {
                std::ofstream log(log_path, std::ios::app);
                log << command << '\n';
            }
            Json result = Json::object();

            if (command == "stop") {
                running = false;
            } else if (command == "cont") {
                running = true;
            } else if (command == "query-status") {
                result = {{"running", running}};
            } else {
                assert(command == "qmp_capabilities");
            }

            const auto reply = Json{{"return", result}, {"id", request.at("id")}}.dump() + "\n";

            if (::write(fd, reply.data(), reply.size()) != static_cast<ssize_t>(reply.size())) {
                break;
            }
        }

        ::close(fd);
    }
}
} // namespace

std::optional<int> persistence_qmp_fixture(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--qnx-persistence-qmp-fixture") {
        return unix_qmp_fixture(argv[2], argv[3]);
    }

    return std::nullopt;
}

void with_failing_disk_flushes(std::intptr_t handle, const std::function<void()> &action) {
    const int disk_fd = static_cast<int>(handle), saved = ::dup(disk_fd),
              null_fd = ::open("/dev/null", O_RDWR);

    assert(saved >= 0 && null_fd >= 0 && ::dup2(null_fd, disk_fd) == disk_fd);
    ::close(null_fd);
    action();
    assert(::dup2(saved, disk_fd) == disk_fd);
    ::close(saved);
    assert(::fcntl(disk_fd, F_SETFD, FD_CLOEXEC) == 0);
}

bool process_gone(std::int64_t pid) {
    return !os::process_exists(pid);
}

std::vector<std::byte> elevated_default_owner() {
    return {};
}

void restore_default_owner(std::vector<std::byte> &) {}

void project_socket_root_guard() {
    auto temp = std::filesystem::canonical(std::filesystem::temp_directory_path());
    auto root = temp / ("voyemu-socket-path-test-" + random_token());
    auto id = random_token();
    auto directory = root / std::string(100, 'x') / id;

    ensure_private_directory(directory);
    {
        Session session(directory, id);
        auto expected = temp / ("voyemu-" + std::to_string(::geteuid()));

        // macOS bounds sun_path at 104 bytes and falls back to its caches.
        if (os::platform_name == "macOS" &&
            (expected / (id + "-qmp.sock")).string().size() >= 104) {
            expected = std::filesystem::path(std::getenv("HOME")) / "Library/Caches/voyemu/runtime";
        }

        assert(session.endpoint() == expected / (id + ".sock"));
        assert(session.endpoint().string().size() < sizeof(sockaddr_un{}.sun_path));
        session.stop();
    }
    std::filesystem::remove_all(root);

    // macOS keeps no XDG_RUNTIME_DIR: a TMPDIR too long for a socket path falls back to its caches.
    if (os::platform_name == "macOS") {
        const auto suffix = "voyemu-" + std::to_string(::geteuid());

        if (temp.string().size() + suffix.size() < 60) {
            const auto boundary = temp /
                                  (random_token() + random_token())
                                          .substr(0, 60 - temp.string().size() - suffix.size());
            ensure_private_directory(boundary);
            const char *configured = std::getenv("TMPDIR");
            const std::optional<std::string> preserved =
                    configured ? std::optional<std::string>(configured) : std::nullopt;
            assert(::setenv("TMPDIR", boundary.c_str(), 1) == 0);
            const auto selected = native_socket_root();
            const auto restored = preserved ? ::setenv("TMPDIR", preserved->c_str(), 1)
                                            : ::unsetenv("TMPDIR");
            assert(restored == 0);
            assert((boundary / suffix / (id + "-qmp.sock")).string().size() == 104);
            assert(selected != boundary / suffix &&
                   (selected / (id + "-qmp.sock")).string().size() <
                           sizeof(sockaddr_un{}.sun_path));
            std::filesystem::remove(boundary);
        }
    } else {
        // A TMPDIR too long for a socket path falls back to XDG_RUNTIME_DIR.
        {
            const auto deep = temp / ("voyemu-socket-path-test-" + random_token()) /
                              std::string(100, 'y');
            const auto runtime = temp / ("r" + random_token().substr(0, 8));
            ensure_private_directory(deep);
            ensure_private_directory(runtime);
            std::map<std::string, std::optional<std::string>> preserved;

            for (const auto *name : {"TMPDIR", "XDG_RUNTIME_DIR"}) {
                const char *configured = std::getenv(name);
                preserved[name] = configured ? std::optional<std::string>(configured)
                                             : std::nullopt;
            }

            assert(::setenv("TMPDIR", deep.c_str(), 1) == 0 &&
                   ::setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1) == 0);
            const auto selected = native_socket_root();

            for (const auto &[name, value] : preserved) {
                assert(value ? ::setenv(name.c_str(), value->c_str(), 1) == 0
                             : ::unsetenv(name.c_str()) == 0);
            }

            assert(selected == runtime / "voyemu" && std::filesystem::is_directory(selected));
            std::filesystem::remove_all(deep.parent_path());
            std::filesystem::remove_all(runtime);
        }
    }

    // The runtime directory of ${RUNTIME} exists, private and owned, before QEMU binds its QMP
    // socket there, also when the control socket of a short session stays in the session; a
    // TMPDIR and user runtime directory too long for a socket path fall back to /tmp.
    {
        const auto suffix = "voyemu-" + std::to_string(::geteuid());
        const auto fresh = temp / ("s" + random_token().substr(0, 8));
        const auto deep = temp / ("voyemu-socket-path-test-" + random_token()) /
                          std::string(100, 'y');
        ensure_private_directory(fresh);
        ensure_private_directory(deep);
        std::map<std::string, std::optional<std::string>> preserved;

        for (const auto *name : {"TMPDIR", "XDG_RUNTIME_DIR", "HOME"}) {
            const char *configured = std::getenv(name);
            preserved[name] = configured ? std::optional<std::string>(configured) : std::nullopt;
        }

        assert(::setenv("TMPDIR", fresh.c_str(), 1) == 0);
        const auto qmp = std::filesystem::path(
                expand_session_path("${RUNTIME}/${TOKEN}-qmp.sock", directory, id));
        struct stat status{};
        const bool created = ::lstat(qmp.parent_path().c_str(), &status) == 0 &&
                             S_ISDIR(status.st_mode) && status.st_uid == ::geteuid() &&
                             (status.st_mode & 0777) == 0700;
        assert(::setenv("TMPDIR", deep.c_str(), 1) == 0 && ::unsetenv("XDG_RUNTIME_DIR") == 0 &&
               ::setenv("HOME", deep.c_str(), 1) == 0);
        std::filesystem::path fallback;
        std::string refusal;
        try {
            fallback = native_socket_root();
        } catch (const std::exception &error) {
            refusal = error.what();
        }

        for (const auto &[name, value] : preserved) {
            assert(value ? ::setenv(name.c_str(), value->c_str(), 1) == 0
                         : ::unsetenv(name.c_str()) == 0);
        }

        assert(created && qmp.filename() == id + "-qmp.sock" &&
               qmp.string().size() < sizeof(sockaddr_un{}.sun_path));

        if ((fresh / suffix / (id + "-qmp.sock")).string().size() <
            sizeof(sockaddr_un{}.sun_path)) {
            assert(qmp.parent_path() == fresh / suffix);
        }

        assert(refusal.empty() && fallback == std::filesystem::canonical("/tmp") / suffix &&
               std::filesystem::is_directory(fallback));
        std::filesystem::remove_all(fresh);
        std::filesystem::remove_all(deep.parent_path());
    }
    // A session path too long for a socket keeps ${RUNTIME} in the native socket root.
    {
        const std::string token(32, 'a');
        const auto long_session = temp / std::string(160, 'x');
        const auto qmp = expand_session_path("${RUNTIME}/${TOKEN}-qmp.sock", long_session, token);
        assert(qmp == (native_socket_root() / (token + "-qmp.sock")).string() &&
               qmp.size() < sizeof(sockaddr_un{}.sun_path) && qmp.find("${") == std::string::npos);
    }
}

void control_server_idle_clients(const std::filesystem::path &endpoint) {
    std::atomic<bool> stopped = false;
    ControlServer server(endpoint, [](const Json &) {
        return Json{{"ok", true}};
    });
    std::array<int, 16> clients{};

    for (auto &client : clients) {
        client = ::socket(AF_UNIX, SOCK_STREAM, 0);
        assert(client >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path = endpoint.string();
        assert(path.size() < sizeof address.sun_path);
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        assert(::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    }

    // Fill the kernel backlog before the accept thread runs: the overload client must still
    // connect.
    std::thread serving([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        server.serve(stopped);
    });
    // The seventeenth response proves all sixteen idle clients reached worker slots.
    Json busy;
    try {
        busy = request(endpoint, {{"op", "status"}}, 2000);
    } catch (...) {
        stopped = true;
        serving.join();

        for (const auto client : clients) {
            ::close(client);
        }

        throw;
    }

    assert(busy.at("ok") == false && busy.at("error").get<std::string>().find(
                                             "active connection limit") != std::string::npos);
    const auto start = std::chrono::steady_clock::now();
    stopped = true;
    serving.join();
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));

    for (const auto client : clients) {
        ::close(client);
    }
}

void control_server_early_busy_reply() {
    const auto endpoint = native_socket_root() / (random_token() + "-vbr.sock");
    const auto path = endpoint.string();
    const Json busy{{"ok", false}, {"error", "IPC_BUSY: active connection limit"}};
    auto exchange = [&](const std::string &response, bool accepted) {
        const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
        assert(listener >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        assert(path.size() < sizeof address.sun_path);
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
        assert(::chmod(endpoint.c_str(), 0600) == 0);
        assert(::listen(listener, 1) == 0);
        std::thread peer([&] {
            const int fd = ::accept(listener, nullptr, nullptr);
            assert(fd >= 0);
            char first;
            assert(::recv(fd, &first, 1, MSG_PEEK) == 1);
            assert(::send(fd, response.data(), response.size(), 0) ==
                   static_cast<ssize_t>(response.size()));
            ::close(fd);
        });
        // Peeking waits for connect completion without consuming request bytes.
        // The 120KB request exceeds the
        // supported macOS Unix socket buffer (8KB), forcing the early-close write failure.
        Json actual;
        std::exception_ptr failure;
        const auto start = std::chrono::steady_clock::now();
        try {
            actual = request(endpoint, {{"op", "status"}, {"payload", std::string(120000, 'x')}},
                             1000);
        } catch (...) {
            failure = std::current_exception();
        }

        peer.join();
        ::close(listener);
        std::filesystem::remove(endpoint);
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));

        if (accepted) {
            if (failure) {
                std::rethrow_exception(failure);
            }

            assert(actual == busy);
        } else {
            assert(failure); // Preserve the write failure, not a parse/read error or another
                             // response.
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception &error) {
                assert(std::string(error.what()).starts_with("IPC_WRITE_UNKNOWN:"));
            }
        }
    };

    for (unsigned i = 0; i < 8; ++i) {
        exchange(busy.dump() + "\n", true);
    }

    // Linux Unix-socket buffers accept the whole request, so only macOS forces the write failure.
    if (os::platform_name == "macOS") {
        exchange(Json{{"ok", true}}.dump() + "\n", false);
        exchange(Json{{"ok", false}, {"error", "other"}}.dump() + "\n", false);
        exchange(Json{{"ok", false},
                      {"error", "IPC_BUSY: active connection limit"},
                      {"extra",
                       true}}.dump() +
                         "\n",
                 false);
        exchange(busy.dump(), false);
        exchange("{broken}\n", false);
    }
}
