// The POSIX backend of ChildProcess, process capture and the loopback port probe of process.hpp.
#include "core/process.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/process.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
namespace voyemu {
struct SanitizedProcessOutput {
    int read = -1, file = -1;
    std::atomic<bool> stop = false, complete = false, failed = false;
    std::thread reader;

    ~SanitizedProcessOutput() {
        stop = true;

        if (reader.joinable()) {
            reader.join();
        }

        if (read >= 0) {
            ::close(read);
        }

        if (file >= 0) {
            ::close(file);
        }
    }

    void run(ChildProcess::OutputSanitizer sanitizer, std::int64_t pid,
             std::size_t limit) noexcept {
        std::size_t written = 0;
        std::array<char, 16384> bytes{};
        const auto emit = [&](std::string_view raw, bool eof) {
            if (failed.load()) {
                return;
            }

            try {
                auto safe = sanitizer(raw, pid, eof);

                if (safe.size() > 32768 || safe.size() > limit - written) {
                    failed = true;

                    return;
                }

                std::size_t offset = 0;

                while (offset < safe.size()) {
                    auto count = ::write(file, safe.data() + offset, safe.size() - offset);

                    if (count < 0 && errno == EINTR) {
                        continue;
                    }

                    if (count <= 0) {
                        failed = true;

                        return;
                    }

                    offset += count;
                    written += count;
                }
            } catch (...) {
                failed = true;
            }
        };

        while (!stop.load()) {
            const auto count = ::read(read, bytes.data(), bytes.size());

            if (count > 0) {
                emit(std::string_view(bytes.data(), count), false);
            } else if (count == 0) {
                emit({}, true);
                complete = true;

                return;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                failed = true;

                return;
            } else {
                pollfd item{read, POLLIN, 0};
                ::poll(&item, 1, 10);
            }
        }
    }
};
struct ChildProcess::Impl {
    pid_t id = -1;
    std::unique_ptr<SanitizedProcessOutput> sanitized;
    bool finished = false;
    int exit_code = -1;
};

std::int64_t ChildProcess::pid() const {
    if (!impl_) {
        return 0;
    }

    return impl_->id;
}

std::string_view ChildProcess::pid_namespace() const {
    return "posix";
}

int ChildProcess::exit_code() const {
    return impl_ && impl_->finished ? impl_->exit_code : -1;
}

static pid_t launch_native(const ProcessSpec &spec, int output) {
    if (!spec.executable.is_absolute()) {
        throw VoyemuError("PROCESS_PATH", "absolute executable required");
    }

    std::vector<std::string> args{spec.executable.string()};

    for (const auto &arg : spec.arguments) {
        check_process_string(arg);
        args.push_back(arg);
    }

    std::vector<char *> argv;

    for (auto &arg : args) {
        argv.push_back(arg.data());
    }

    argv.push_back(nullptr);
    std::map<std::string, std::string> environment;

    for (auto p = environ; *p; ++p) {
        std::string row = *p;
        auto split = row.find('=');

        if (split != std::string::npos) {
            environment[row.substr(0, split)] = row.substr(split + 1);
        }
    }

    for (const auto &[key, value] : spec.environment) {
        check_process_string(key);
        check_process_string(value);

        if (key.empty() || key.find('=') != std::string::npos) {
            throw VoyemuError("PROCESS_ENV", "invalid key");
        }

        environment[key] = value;
    }

    std::vector<std::string> entries;

    for (const auto &[key, value] : environment) {
        entries.push_back(key + "=" + value);
    }

    std::vector<char *> envp;

    for (auto &entry : entries) {
        envp.push_back(entry.data());
    }

    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);

    if (output > STDERR_FILENO) {
        posix_spawn_file_actions_addclose(&actions, output);
    }

#if defined(__APPLE__) || defined(__GLIBC__)
    if (!spec.directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, spec.directory.c_str());
    }
#else
    if (!spec.directory.empty()) {
        posix_spawn_file_actions_destroy(&actions);
        throw VoyemuError("PROCESS_CWD", "unsupported host");
    }
#endif
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    short flags = POSIX_SPAWN_SETPGROUP;
#ifdef POSIX_SPAWN_SETSID
    if (spec.detached) {
        flags = POSIX_SPAWN_SETSID;
    }
#endif
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    flags |= POSIX_SPAWN_SETSIGDEF;
    posix_spawnattr_setflags(&attributes, flags);
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t pid = -1;
    int result = posix_spawn(&pid, spec.executable.c_str(), &actions, &attributes, argv.data(),
                             envp.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);

    if (result != 0) {
        throw VoyemuError("PROCESS_SPAWN", std::string(std::strerror(result)));
    }

    return pid;
}

ChildProcess ChildProcess::launch(const ProcessSpec &spec) {
    ChildProcess child;

    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "Wine Windows transport required");
    }

    int output = ::open(spec.output.empty() ? "/dev/null" : spec.output.c_str(),
                        O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);

    if (output < 0) {
        throw VoyemuError("PROCESS_LOG", "cannot open output");
    }

    try {
        child.impl_ = std::make_unique<Impl>();
        child.impl_->id = launch_native(spec, output);
    } catch (...) {
        ::close(output);
        throw;
    }

    ::close(output);

    return child;
}

ChildProcess ChildProcess::launch_sanitized(const ProcessSpec &spec, OutputSanitizer sanitizer,
                                            std::size_t output_limit) {
    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "sanitized native capture unavailable");
    }

    if (!sanitizer || output_limit < 1 || output_limit > 4 * 1024 * 1024 || spec.output.empty() ||
        spec.detached) {
        throw VoyemuError("PROCESS_SANITIZER", "invalid capture bounds or lifetime");
    }

    auto capture = std::make_unique<SanitizedProcessOutput>();
    capture->file = ::open(spec.output.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

    if (capture->file < 0) {
        throw VoyemuError("PROCESS_LOG", "fresh sanitized output required");
    }

    struct stat identity{};

    if (::fstat(capture->file, &identity) || !S_ISREG(identity.st_mode) || identity.st_nlink != 1) {
        throw VoyemuError("PROCESS_LOG", "ordinary sanitized output required");
    }

    int descriptors[2];

    if (::pipe(descriptors)) {
        throw VoyemuError("PROCESS_PIPE", "cannot create sanitized capture");
    }

    capture->read = descriptors[0];
    const auto write = descriptors[1];

    if (::fcntl(capture->read, F_SETFD, FD_CLOEXEC) || ::fcntl(write, F_SETFD, FD_CLOEXEC) ||
        ::fcntl(capture->read, F_SETFL, O_NONBLOCK)) {
        ::close(write);
        throw VoyemuError("PROCESS_PIPE", "cannot isolate sanitized reader");
    }

    auto process = std::make_unique<Impl>();
    try {
        process->id = launch_native(spec, write);
    } catch (...) {
        ::close(write);
        throw;
    }

    ::close(write);
    ChildProcess child;
    child.impl_ = std::move(process);
    child.impl_->sanitized = std::move(capture);
    auto owned = child.impl_->sanitized.get();
    const auto pid = child.pid();
    owned->reader =
            std::thread([owned, sanitizer = std::move(sanitizer), pid, output_limit]() mutable {
                owned->run(std::move(sanitizer), pid, output_limit);
            });

    return child;
}

bool ChildProcess::alive() {
    if (!impl_ || impl_->finished) {
        return false;
    }

    if (impl_->id < 2) {
        return false;
    }

    int status = 0;
    auto result = ::waitpid(impl_->id, &status, WNOHANG);

    if (result == 0) {
        return true;
    }

    if (result < 0 && errno == EINTR) {
        return true;
    }

    if (result < 0) {
        throw VoyemuError("PROCESS_WAIT", "lost direct-child identity");
    }

    impl_->finished = true;
    impl_->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);

    return false;
}

bool ChildProcess::owns_tcp_listener(unsigned port) {
    if (port == 0 || port > 65535 || !alive()) {
        return false;
    }

    const auto local_ipv6 = [](const auto &address) {
        if (IN6_IS_ADDR_LOOPBACK(&address) || IN6_IS_ADDR_UNSPECIFIED(&address)) {
            return true;
        }

        constexpr unsigned char mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

        if (std::memcmp(&address, mapped_prefix, sizeof mapped_prefix) != 0) {
            return false;
        }

        std::uint32_t ipv4 = 0;
        std::memcpy(&ipv4, reinterpret_cast<const unsigned char *>(&address) + 12, sizeof ipv4);

        return ipv4 == htonl(INADDR_LOOPBACK) || ipv4 == htonl(INADDR_ANY);
    };

    for (const auto &socket : os::process_tcp_sockets(impl_->id)) {
        const auto &address = socket.local;

        if (socket.state != os::TcpSocket::State::listen || address.port != port) {
            continue;
        }

        const auto ipv4 = address.address4;
        const bool ipv4_local = address.ipv4 &&
                                (ipv4 == htonl(INADDR_LOOPBACK) || ipv4 == htonl(INADDR_ANY));
        const bool ipv6_local = address.ipv6 && local_ipv6(address.address6);

        if ((socket.family == AF_INET && ipv4_local) ||
            (socket.family == AF_INET6 && (ipv4_local || ipv6_local))) {
            return alive();
        }
    }

    return false;
}

void ChildProcess::terminate(int grace_ms) {
    if (impl_ && impl_->id < 2) {
        return;
    }

    if (!alive()) {
        return;
    }

    if (::kill(impl_->id, SIGTERM) != 0 && errno != ESRCH) {
        throw VoyemuError("PROCESS_STOP", "owned child refused SIGTERM");
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grace_ms);

    while (alive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (alive()) {
        ::kill(impl_->id, SIGKILL);
        int status = 0;

        while (::waitpid(impl_->id, &status, 0) < 0 && errno == EINTR) {}

        impl_->finished = true;
        impl_->exit_code = 137;
    }
}

void ChildProcess::detach() {
    if (impl_ && impl_->sanitized) {
        throw VoyemuError("PROCESS_SANITIZER", "captured child cannot detach");
    }

    impl_.reset();
}

ProcessResult execute_capture(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit,
                              int maximum_timeout_ms) {
    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "native capture requires owned ChildProcess");
    }

    if (timeout_ms < 1 || timeout_ms > maximum_timeout_ms || output_limit > 1024 * 1024) {
        throw VoyemuError("PROCESS_BOUNDS", "invalid capture bounds");
    }

    ProcessResult result{-1, {}};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int pipes[2];

    if (::pipe(pipes) != 0) {
        throw VoyemuError("PROCESS_PIPE", "cannot create capture");
    }

    ::fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    ::fcntl(pipes[0], F_SETFL, O_NONBLOCK);
    pid_t pid;
    try {
        pid = launch_native(spec, pipes[1]);
    } catch (...) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        throw;
    }

    ::close(pipes[1]);
    bool finished = false, eof = false;
    int status = 0;

    while (!finished || !eof) {
        char buffer[4096];
        auto count = ::read(pipes[0], buffer, sizeof buffer);

        if (count > 0) {
            if (result.output.size() + count > output_limit) {
                ::kill(pid, SIGKILL);

                while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

                ::close(pipes[0]);
                throw VoyemuError("PROCESS_EXTENT", "capture exceeded bound");
            }

            result.output.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            eof = true;
        }

        if (!finished) {
            auto wait = ::waitpid(pid, &status, WNOHANG);

            if (wait == pid) {
                finished = true;
            } else if (wait < 0 && errno != EINTR) {
                ::close(pipes[0]);
                throw VoyemuError("PROCESS_WAIT", "lost capture child");
            }
        }

        if (std::chrono::steady_clock::now() >= deadline && (!finished || !eof)) {
            result.timed_out = true;

            if (!finished) {
                ::kill(pid, SIGKILL);

                while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

                finished = true;
            }

            break;
        }

        if (count <= 0 && (!finished || !eof)) {
            pollfd pfd{pipes[0], POLLIN, 0};
            ::poll(&pfd, 1, 10);
        }
    }

    ::close(pipes[0]);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);

    return result;
}

bool tcp_port_free(unsigned port, TcpPortProbe *diagnostic) {
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->port = port;
    }

    auto fail = [&](const char *stage, unsigned error) {
        if (diagnostic) {
            diagnostic->stage = stage;
            diagnostic->error = error;
        }

        return false;
    };

    if (port > 65535 || port == 0) {
        return fail("port", 0);
    }

    for (int family : {AF_INET, AF_INET6}) {
        if (diagnostic) {
            diagnostic->family = family;
        }

        sockaddr_storage storage{};
        sockaddr *address = reinterpret_cast<sockaddr *>(&storage);
        const auto address_size = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);

        if (family == AF_INET) {
            auto &local = *reinterpret_cast<sockaddr_in *>(&storage);
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = htons(static_cast<std::uint16_t>(port));
        } else {
            auto &local = *reinterpret_cast<sockaddr_in6 *>(&storage);
            local.sin6_family = AF_INET6;
            local.sin6_addr = in6addr_loopback;
            local.sin6_port = htons(static_cast<std::uint16_t>(port));
        }

        int fd = ::socket(family, SOCK_STREAM, 0);

        if (fd < 0) {
            return fail("socket", errno);
        }

        struct SocketCleanup {
            int fd;

            ~SocketCleanup() {
                ::close(fd);
            }
        } socket_cleanup{fd};
        // Reject live listeners before the SDK-reusable bind; wildcard REUSEADDR must not hide
        // them.
        int probe = ::socket(family, SOCK_STREAM, 0);

        if (probe < 0) {
            return fail("probe_socket", errno);
        }

        SocketCleanup probe_cleanup{probe};

        if (::fcntl(probe, F_SETFL, O_NONBLOCK) != 0) {
            return fail("probe_nonblocking", errno);
        }

        int connected = ::connect(probe, address, address_size);
        int error = connected == 0 ? 0 : errno;

        if (connected < 0 && error == EINPROGRESS) {
            pollfd item{probe, POLLOUT, 0};
            int waited;

            do {
                waited = ::poll(&item, 1, 100);
            } while (waited < 0 && errno == EINTR);

            socklen_t length = sizeof error;

            if (waited <= 0 || ::getsockopt(probe, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
                error = ETIMEDOUT;
            }
        }

        if (error != ECONNREFUSED) {
            return fail("connect", error);
        }

        // Completed SDK connections in TIME_WAIT are reusable; REUSEPORT would hide a live
        // listener.
        int enabled = 1;

        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) != 0) {
            return fail("reuse", errno);
        }

        if (::bind(fd, address, address_size) != 0) {
            return fail("bind", errno);
        }

        if (::listen(fd, 1) != 0) {
            return fail("listen", errno);
        }
    }

    if (diagnostic) {
        diagnostic->stage = "free";
    }

    return true;
}

ChildProcess::ChildProcess() = default;

ChildProcess::~ChildProcess() {
    if (impl_) {
        try {
            terminate(500);
        } catch (...) { /* Ignored: destructors must not throw. */
        }
    }
}

ChildProcess::ChildProcess(ChildProcess &&) noexcept = default;
ChildProcess &ChildProcess::operator=(ChildProcess &&other) noexcept {
    if (this != &other) {
        if (impl_) {
            try {
                terminate(500);
            } catch (...) { /* Ignored: the replaced child is stopped best-effort. */
            }
        }

        impl_ = std::move(other.impl_);
    }

    return *this;
}

bool ChildProcess::sanitized_output_complete() const {
    return impl_ && impl_->sanitized && impl_->sanitized->complete.load();
}

bool ChildProcess::sanitized_output_failed() const {
    return impl_ && impl_->sanitized && impl_->sanitized->failed.load();
}
} // namespace voyemu
