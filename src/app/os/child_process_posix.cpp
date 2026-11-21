// The POSIX backend of ChildProcess, process capture and the loopback port probe of process.hpp.
#include "core/process.hpp"
#include "core/process_internal.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/process.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
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
struct ChildProcess::Impl {
    pid_t id = -1;
    bool finished = false;
    int exit_code = -1;
};

std::string_view ChildProcess::pid_namespace() const {
    return "posix";
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
        check_process_variable(key, value);

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
        child.impl_.reset(new Impl());
        child.impl_->id = launch_native(spec, output);
    } catch (...) {
        ::close(output);
        throw;
    }

    ::close(output);

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

    for (const auto &socket : os::process_tcp_sockets(impl_->id)) {
        const auto &address = socket.local;

        if (socket.state != os::TcpSocket::State::listen || address.port != port) {
            continue;
        }

        const auto ipv4 = address.address4;
        const bool ipv4_local = address.ipv4 &&
                                (ipv4 == htonl(INADDR_LOOPBACK) || ipv4 == htonl(INADDR_ANY));
        const bool ipv6_local = address.ipv6 && local_ipv6_address(address.address6);

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
    if (sanitized_) {
        throw VoyemuError("PROCESS_SANITIZER", "captured child cannot detach");
    }

    impl_.reset();
}

ProcessResult execute_capture(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit,
                              int maximum_timeout_ms) {
    check_capture_request(spec, timeout_ms, output_limit, maximum_timeout_ms);

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

ChildProcess launch_child_process(const ProcessSpec &spec, std::intptr_t output) {
    ChildProcess child;

    child.impl_.reset(new ChildProcess::Impl());
    child.impl_->id = launch_native(spec, static_cast<int>(output));

    return child;
}

std::int64_t child_pid(const ChildProcess::Impl &impl) {
    return impl.id;
}

std::optional<int> child_exit(const ChildProcess::Impl &impl) {
    return impl.finished ? std::optional(impl.exit_code) : std::nullopt;
}

void ChildProcess::ImplDelete::operator()(Impl *impl) const {
    delete impl;
}

namespace os {
std::vector<std::pair<std::string, std::string>> environment_variables() {
    std::vector<std::pair<std::string, std::string>> result;

    for (char **entry = environment(); *entry; ++entry) {
        const std::string_view row(*entry);
        const auto equal = row.find('=');

        if (equal != std::string_view::npos) {
            result.emplace_back(row.substr(0, equal), row.substr(equal + 1));
        }
    }

    return result;
}

void close_file(FileHandle file) {
    ::close(static_cast<int>(file));
}

FileHandle create_capture_output(const std::filesystem::path &path) {
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                            0600);

    if (file < 0) {
        throw VoyemuError("PROCESS_LOG", "fresh sanitized output required");
    }

    struct stat identity{};

    if (::fstat(file, &identity) || !S_ISREG(identity.st_mode) || identity.st_nlink != 1) {
        ::close(file);
        throw VoyemuError("PROCESS_LOG", "ordinary sanitized output required");
    }

    return file;
}

void create_capture_pipe(FileHandle &read, FileHandle &write) {
    int descriptors[2];

    if (::pipe(descriptors)) {
        throw VoyemuError("PROCESS_PIPE", "cannot create sanitized capture");
    }

    read = descriptors[0];
    write = descriptors[1];

    if (::fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) ||
        ::fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) ||
        ::fcntl(descriptors[0], F_SETFL, O_NONBLOCK)) {
        ::close(descriptors[1]);
        write = no_file;
        throw VoyemuError("PROCESS_PIPE", "cannot isolate sanitized reader");
    }
}

long long read_capture(FileHandle read, char *data, std::size_t size) {
    const auto count = ::read(static_cast<int>(read), data, size);

    if (count >= 0) {
        return count;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return -2;
    }

    pollfd item{static_cast<int>(read), POLLIN, 0};
    ::poll(&item, 1, 10);

    return -1;
}

long long write_capture(FileHandle file, const char *data, std::size_t size) {
    for (;;) {
        const auto count = ::write(static_cast<int>(file), data, size);

        if (count < 0 && errno == EINTR) {
            continue;
        }

        return count <= 0 ? -1 : count;
    }
}

bool set_environment_variable(const std::string &name, const std::string &value) {
    return (value.empty() ? ::unsetenv(name.c_str()) : ::setenv(name.c_str(), value.c_str(), 1)) ==
           0;
}

void binary_standard_output() {}
} // namespace os
} // namespace voyemu
