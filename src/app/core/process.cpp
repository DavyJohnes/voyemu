#include "process.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/digest.hpp"
#include "os/process.hpp"
#ifdef _WIN32
#include "../platform/wine_native_backend_windows.hpp"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <iphlpapi.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace voyemu {
static void checked_string(const std::string &value) {
    if (value.find('\0') != std::string::npos) {
        throw VoyemuError("PROCESS_ARGUMENT", "embedded NUL");
    }
}

std::string windows_quote(const std::string &value) {
    checked_string(value);
    std::string result = "\"";
    std::size_t slashes = 0;

    for (auto character : value) {
        if (character == '\\') {
            ++slashes;
            continue;
        }

        if (character == '"') {
            result.append(slashes * 2 + 1, '\\');
            result += '"';
        } else {
            result.append(slashes, '\\');
            result += character;
        }

        slashes = 0;
    }

    result.append(slashes * 2, '\\');
    result += '"';

    return result;
}

std::string android_shell_quote(const std::string &value) {
    checked_string(value);

    if (value.find_first_of("\r\n") != std::string::npos) {
        throw VoyemuError("ANDROID_ARGUMENT", "newline rejected");
    }

    std::string output = "'";

    for (auto ch : value) {
        if (ch == '\'') {
            output += "'\\''";
        } else {
            output += ch;
        }
    }

    return output + "'";
}

std::string random_token() {
    std::array<unsigned char, 16> bytes{};

#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw VoyemuError("RANDOM", "system RNG failed");
    }
#else
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        throw VoyemuError("RANDOM", "system RNG failed");
    }

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        auto n = ::read(fd, bytes.data() + offset, bytes.size() - offset);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            ::close(fd);
            throw VoyemuError("RANDOM", "incomplete system RNG");
        }

        offset += n;
    }
    ::close(fd);
#endif
    std::ostringstream output;
    output << std::hex << std::setfill('0');

    for (auto byte : bytes) {
        output << std::setw(2) << unsigned(byte);
    }

    return output.str();
}

// Portable SHA-256 retains exact source pins while hashing large firmware in chunks.
std::string sha256_bytes(std::string_view bytes) {
    os::Sha256 hash;

    hash.update(bytes.data(), bytes.size());

    return os::hex_digest(hash.finish());
}

std::string sha256_file(const std::filesystem::path &path) {
    return sha256_file(path, {});
}

std::string sha256_file(const std::filesystem::path &path,
                        const std::function<void(std::uint64_t)> &advance) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) {
        throw VoyemuError("PIN_PATH", "regular nonsymlink input required");
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw VoyemuError("PIN_READ", "cannot open input");
    }

    std::array<char, 64 * 1024> buffer{};
    os::Sha256 hash;

    while (file) {
        interruption::check();
        file.read(buffer.data(), buffer.size());
        hash.update(buffer.data(), std::size_t(file.gcount()));

        if (advance && file.gcount() > 0) {
            advance(static_cast<std::uint64_t>(file.gcount()));
        }
    }

    if (!file.eof()) {
        throw VoyemuError("PIN_READ", "input read failed");
    }

    return os::hex_digest(hash.finish());
}

std::string sha256_file_range(const std::filesystem::path &path, std::uint64_t offset,
                              std::uint64_t bytes) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) {
        throw VoyemuError("PIN_PATH", "regular nonsymlink input required");
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw VoyemuError("PIN_READ", "cannot open input");
    }

    const auto size = std::filesystem::file_size(path);

    if (offset > size || bytes > size - offset) {
        throw VoyemuError("PIN_RANGE", "bounded file range required");
    }

    file.seekg(static_cast<std::streamoff>(offset));
    std::array<char, 64 * 1024> buffer{};
    os::Sha256 hash;

    while (bytes) {
        interruption::check();
        const auto count = std::min<std::uint64_t>(bytes, buffer.size());
        file.read(buffer.data(), count);

        if (std::uint64_t(file.gcount()) != count) {
            throw VoyemuError("PIN_READ", "incomplete file range");
        }

        bytes -= count;
        hash.update(buffer.data(), count);
    }

    if (!file) {
        throw VoyemuError("PIN_READ", "input range read failed");
    }

    return os::hex_digest(hash.finish());
}

struct SanitizedProcessOutput {
#ifdef _WIN32
    HANDLE read = nullptr, file = INVALID_HANDLE_VALUE;
#else
    int read = -1, file = -1;
#endif
    std::atomic<bool> stop = false, complete = false, failed = false;
    std::thread reader;

    ~SanitizedProcessOutput() {
        stop = true;

        if (reader.joinable()) {
            reader.join();
        }

#ifdef _WIN32
        if (read) {
            CloseHandle(read);
        }

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
#else
        if (read >= 0) {
            ::close(read);
        }

        if (file >= 0) {
            ::close(file);
        }
#endif
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
#ifdef _WIN32
                    DWORD count = 0;

                    if (!WriteFile(file, safe.data() + offset,
                                   static_cast<DWORD>(safe.size() - offset), &count, nullptr) ||
                        !count) {
                        failed = true;

                        return;
                    }
#else
                    auto count = ::write(file, safe.data() + offset, safe.size() - offset);

                    if (count < 0 && errno == EINTR) {
                        continue;
                    }

                    if (count <= 0) {
                        failed = true;

                        return;
                    }
#endif
                    offset += count;
                    written += count;
                }
            } catch (...) {
                failed = true;
            }
        };

        while (!stop.load()) {
#ifdef _WIN32
            DWORD available = 0;

            if (!PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    emit({}, true);
                    complete = true;
                } else {
                    failed = true;
                }

                return;
            }

            if (!available) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            DWORD count = 0;

            if (!ReadFile(read, bytes.data(), std::min<DWORD>(available, bytes.size()), &count,
                          nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    emit({}, true);
                    complete = true;
                } else {
                    failed = true;
                }

                return;
            }

            if (count) {
                emit(std::string_view(bytes.data(), count), false);
            }
#else
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
#endif
        }
    }
};
struct ChildProcess::Impl {
#ifdef _WIN32
    HANDLE process = nullptr, job = nullptr;
    DWORD id = 0;
    std::unique_ptr<WineNativeBackendWindows> wine_native;
#else
    pid_t id = -1;
#endif
    std::unique_ptr<SanitizedProcessOutput> sanitized;
    bool finished = false;
    int exit_code = -1;
};
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

std::int64_t ChildProcess::pid() const {
    if (!impl_) {
        return 0;
    }

#ifdef _WIN32
    if (impl_->wine_native) {
        return impl_->wine_native->pid();
    }
#endif

    return impl_->id;
}

std::string_view ChildProcess::pid_namespace() const {
#ifdef _WIN32
    return impl_ && impl_->wine_native ? "macos-diagnostic" : "windows";
#else
    return "posix";
#endif
}

int ChildProcess::exit_code() const {
#ifdef _WIN32
    if (impl_ && impl_->wine_native) {
        return impl_->wine_native->exit_code();
    }
#endif

    return impl_ && impl_->finished ? impl_->exit_code : -1;
}

#ifdef _WIN32
static std::wstring wide(const std::string &value) {
    if (value.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);

    if (size <= 0) {
        throw VoyemuError("PROCESS_ARGUMENT", "invalid UTF8");
    }

    std::wstring output(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        output.data(), size);

    return output;
}

static ChildProcess::Impl *launch_native(const ProcessSpec &spec, HANDLE output) {
    std::string command = windows_quote(spec.executable.string());

    for (const auto &arg : spec.arguments) {
        command += " " + windows_quote(arg);
    }

    auto cmd = wide(command);
    std::map<std::wstring, std::wstring> variables;
    auto environment = GetEnvironmentStringsW();

    if (environment) {
        for (auto p = environment; *p; p += wcslen(p) + 1) {
            std::wstring row = p;
            auto split = row.find(L'=', row.starts_with(L'=') ? 1 : 0);

            if (split != std::wstring::npos) {
                variables[row.substr(0, split)] = row.substr(split + 1);
            }
        }

        FreeEnvironmentStringsW(environment);
    }

    for (const auto &[key, value] : spec.environment) {
        checked_string(key);
        checked_string(value);

        if (key.find('=') != std::string::npos) {
            throw VoyemuError("PROCESS_ENV", "invalid key");
        }

        variables[wide(key)] = wide(value);
    }

    std::vector<wchar_t> env;

    for (const auto &[key, value] : variables) {
        auto row = key + L'=' + value;
        env.insert(env.end(), row.begin(), row.end());
        env.push_back(0);
    }

    env.push_back(0);
    SECURITY_ATTRIBUTES input_security{sizeof input_security, nullptr, TRUE};
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &input_security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (input == INVALID_HANDLE_VALUE) {
        throw VoyemuError("PROCESS_INPUT", "cannot open owned null input");
    }

    struct StartupCleanup {
        HANDLE input;

        ~StartupCleanup() {
            CloseHandle(input);
        }
    } startup_cleanup{input};
    std::vector<HANDLE> handles{input, output};

    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);

    if (!attribute_bytes) {
        throw VoyemuError("PROCESS_HANDLE", "handle whitelist unavailable");
    }

    std::vector<std::byte> attribute_storage(attribute_bytes);
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;

        ~AttributeCleanup() {
            if (value) {
                DeleteProcThreadAttributeList(value);
            }
        }
    } attribute_cleanup;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = output;
    startup.StartupInfo.hStdError = output;
    startup.lpAttributeList =
            reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());

    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes)) {
        throw VoyemuError("PROCESS_HANDLE", "handle whitelist initialization failed");
    }

    attribute_cleanup.value = startup.lpAttributeList;

    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   static_cast<void *>(handles.data()),
                                   handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
        throw VoyemuError("PROCESS_HANDLE", "handle whitelist rejected");
    }

    PROCESS_INFORMATION info{};
    auto directory = spec.directory.wstring();
    HANDLE job = CreateJobObjectW(nullptr, nullptr);

    if (!job) {
        throw VoyemuError("PROCESS_JOB", "cannot create owned lifetime");
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        CloseHandle(job);
        throw VoyemuError("PROCESS_JOB", "cannot guard owned lifetime");
    }

    if (!CreateProcessW(spec.executable.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT |
                                CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED |
                                (spec.detached ? DETACHED_PROCESS : CREATE_NO_WINDOW),
                        env.data(), directory.empty() ? nullptr : directory.c_str(),
                        &startup.StartupInfo, &info)) {
        CloseHandle(job);
        throw VoyemuError("PROCESS_SPAWN", "CreateProcess failed");
    }

    if (!AssignProcessToJobObject(job, info.hProcess)) {
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
        CloseHandle(job);
        throw VoyemuError("PROCESS_JOB", "cannot claim process before run");
    }

    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    auto result = new ChildProcess::Impl;
    result->process = info.hProcess;
    result->job = job;
    result->id = info.dwProcessId;

    return result;
}
#else
static pid_t launch_native(const ProcessSpec &spec, int output) {
    if (!spec.executable.is_absolute()) {
        throw VoyemuError("PROCESS_PATH", "absolute executable required");
    }

    std::vector<std::string> args{spec.executable.string()};

    for (const auto &arg : spec.arguments) {
        checked_string(arg);
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
        checked_string(key);
        checked_string(value);

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
#endif

ChildProcess ChildProcess::launch(const ProcessSpec &spec) {
    ChildProcess child;

#ifdef _WIN32
    if (spec.wine_native_diagnostic) {
        child.impl_ = std::make_unique<Impl>();
        child.impl_->wine_native = WineNativeBackendWindows::launch(spec);

        return child;
    }
#else
    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "Wine Windows transport required");
    }
#endif
#ifdef _WIN32
    HANDLE file = os::child_output_handle(spec.output);

    try {
        child.impl_.reset(launch_native(spec, file));
    } catch (...) {
        CloseHandle(file);
        throw;
    }

    CloseHandle(file);
#else
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
#endif

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
#ifdef _WIN32
    capture->file = CreateFileW(spec.output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);

    if (capture->file == INVALID_HANDLE_VALUE) {
        throw VoyemuError("PROCESS_LOG", "fresh sanitized output required");
    }

    BY_HANDLE_FILE_INFORMATION identity{};

    if (!GetFileInformationByHandle(capture->file, &identity) || identity.nNumberOfLinks != 1 ||
        (identity.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        throw VoyemuError("PROCESS_LOG", "ordinary sanitized output required");
    }

    SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
    HANDLE write = nullptr;

    if (!CreatePipe(&capture->read, &write, &security, 16384)) {
        throw VoyemuError("PROCESS_PIPE", "cannot create sanitized capture");
    }

    if (!SetHandleInformation(capture->read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(write);
        throw VoyemuError("PROCESS_PIPE", "cannot isolate sanitized reader");
    }

    ChildProcess child;
    try {
        child.impl_.reset(launch_native(spec, write));
    } catch (...) {
        CloseHandle(write);
        throw;
    }

    CloseHandle(write);
#else
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
#endif
    child.impl_->sanitized = std::move(capture);
    auto owned = child.impl_->sanitized.get();
    const auto pid = child.pid();
    owned->reader =
            std::thread([owned, sanitizer = std::move(sanitizer), pid, output_limit]() mutable {
                owned->run(std::move(sanitizer), pid, output_limit);
            });

    return child;
}

bool ChildProcess::sanitized_output_complete() const {
    return impl_ && impl_->sanitized && impl_->sanitized->complete.load();
}

bool ChildProcess::sanitized_output_failed() const {
    return impl_ && impl_->sanitized && impl_->sanitized->failed.load();
}

bool ChildProcess::alive() {
#ifdef _WIN32
    if (impl_ && impl_->wine_native) {
        return impl_->wine_native->alive();
    }
#endif

    if (!impl_ || impl_->finished) {
        return false;
    }

#ifdef _WIN32
    DWORD code = 0;

    if (!GetExitCodeProcess(impl_->process, &code)) {
        throw VoyemuError("PROCESS_WAIT", "cannot query owned process");
    }

    if (code == STILL_ACTIVE) {
        return true;
    }

    impl_->exit_code = static_cast<int>(code);
    impl_->finished = true;
    CloseHandle(impl_->process);
    impl_->process = nullptr;

    if (impl_->job) {
        CloseHandle(impl_->job);
        impl_->job = nullptr;
    }

    return false;
#else
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
#endif
}

bool ChildProcess::owns_tcp_listener(unsigned port) {
#ifdef _WIN32
    if (impl_ && impl_->wine_native) {
        return impl_->wine_native->owns_tcp_listener(port);
    }
#endif

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
#ifdef _WIN32
    // Query metadata only: IsProcessInJob requires query-information access on supported runtimes.
    auto owned = [&](DWORD pid) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

        if (!process) {
            return false;
        }

        BOOL member = FALSE;
        const bool valid = IsProcessInJob(process, impl_->job, &member) && member;
        CloseHandle(process);

        return valid;
    };

    for (ULONG family : {AF_INET, AF_INET6}) {
        ULONG bytes = 0;
        auto result = GetExtendedTcpTable(nullptr, &bytes, FALSE, family,
                                          TCP_TABLE_OWNER_PID_LISTENER, 0);

        if (result != ERROR_INSUFFICIENT_BUFFER || bytes > 1024 * 1024) {
            continue;
        }

        std::vector<unsigned char> buffer(bytes);

        if (GetExtendedTcpTable(buffer.data(), &bytes, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER,
                                0) != NO_ERROR) {
            continue;
        }

        if (family == AF_INET) {
            auto table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buffer.data());

            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];

                if (row.dwState == MIB_TCP_STATE_LISTEN &&
                    ntohs(static_cast<u_short>(row.dwLocalPort)) == port &&
                    (row.dwLocalAddr == htonl(INADDR_LOOPBACK) ||
                     row.dwLocalAddr == htonl(INADDR_ANY)) &&
                    owned(row.dwOwningPid)) {
                    return alive();
                }
            }
        } else {
            auto table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID *>(buffer.data());

            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                IN6_ADDR local{};
                std::memcpy(&local, row.ucLocalAddr, sizeof local);

                if (row.dwState == MIB_TCP_STATE_LISTEN &&
                    ntohs(static_cast<u_short>(row.dwLocalPort)) == port && local_ipv6(local) &&
                    owned(row.dwOwningPid)) {
                    return alive();
                }
            }
        }
    }
#else
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
#endif

    return false;
}

void ChildProcess::terminate(int grace_ms) {
#ifndef _WIN32
    if (impl_ && impl_->id < 2) {
        return;
    }
#endif
#ifdef _WIN32
    if (impl_ && impl_->wine_native) {
        impl_->wine_native->terminate(grace_ms);

        return;
    }
#endif

    if (!alive()) {
        return;
    }

#ifdef _WIN32
    // A retained kernel process handle cannot accidentally address a reused PID.
    if (!TerminateJobObject(impl_->job, 0)) {
        throw VoyemuError("PROCESS_STOP", "owned lifetime refused termination");
    }

    WaitForSingleObject(impl_->process, static_cast<DWORD>(std::max(grace_ms, 1000)));
    alive();
#else
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
#endif
}

void ChildProcess::detach() {
    if (impl_ && impl_->sanitized) {
        throw VoyemuError("PROCESS_SANITIZER", "captured child cannot detach");
    }

#ifdef _WIN32
    if (impl_ && impl_->wine_native) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "owned native process cannot detach");
    }

    if (impl_) {
        if (impl_->job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits,
                                    sizeof limits);
            CloseHandle(impl_->job);
        }

        if (impl_->process) {
            CloseHandle(impl_->process);
        }
    }
#endif
    impl_.reset();
}

static ProcessResult execute_capture(const ProcessSpec &spec, int timeout_ms,
                                     std::size_t output_limit, int maximum_timeout_ms) {
    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "native capture requires owned ChildProcess");
    }

    if (timeout_ms < 1 || timeout_ms > maximum_timeout_ms || output_limit > 1024 * 1024) {
        throw VoyemuError("PROCESS_BOUNDS", "invalid capture bounds");
    }

    ProcessResult result{-1, {}};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;

    if (!CreatePipe(&read, &write, &security, 0)) {
        throw VoyemuError("PROCESS_PIPE", "cannot create capture");
    }

    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    std::unique_ptr<ChildProcess::Impl> child;
    try {
        child.reset(launch_native(spec, write));
    } catch (...) {
        CloseHandle(read);
        CloseHandle(write);
        throw;
    }

    CloseHandle(write);

    while (true) {
        DWORD available = 0;
        PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr);

        if (available) {
            char buffer[4096];
            DWORD count = 0;
            ReadFile(read, buffer, std::min<DWORD>(available, sizeof buffer), &count, nullptr);

            if (result.output.size() + count > output_limit) {
                TerminateJobObject(child->job, 1);
                CloseHandle(child->process);
                CloseHandle(child->job);
                CloseHandle(read);
                throw VoyemuError("PROCESS_EXTENT", "capture exceeded bound");
            }

            result.output.append(buffer, count);
            continue;
        }

        DWORD code = 0;
        GetExitCodeProcess(child->process, &code);

        if (code != STILL_ACTIVE) {
            result.exit_code = static_cast<int>(code);
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            TerminateJobObject(child->job, 1);
            WaitForSingleObject(child->process, 1000);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!result.timed_out) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        SetInformationJobObject(child->job, JobObjectExtendedLimitInformation, &limits,
                                sizeof limits);
    }

    CloseHandle(child->process);
    CloseHandle(child->job);
    CloseHandle(read);
#else
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
#endif

    return result;
}

ProcessResult execute(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit) {
    return execute_capture(spec, timeout_ms, output_limit, 600000);
}

ProcessResult execute_artifact_preparation(const ProcessSpec &spec, int timeout_ms,
                                           std::size_t output_limit) {
    return execute_capture(spec, timeout_ms, output_limit, 3600000);
}

bool tcp_port_free(unsigned port) {
    return tcp_port_free(port, nullptr);
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

#ifdef _WIN32
    WSADATA data{};
    const auto startup = WSAStartup(MAKEWORD(2, 2), &data);

    if (startup != 0) {
        return fail("startup", startup);
    }

    struct NetworkCleanup {
        ~NetworkCleanup() {
            WSACleanup();
        }
    } network_cleanup;
#endif

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

#ifdef _WIN32
        // Wine and Windows reusable sockets may allow overlapping binds; inspect live listeners
        // first.
        ULONG bytes = 0;
        auto table_result = GetExtendedTcpTable(nullptr, &bytes, FALSE, family,
                                                TCP_TABLE_OWNER_PID_LISTENER, 0);

        if (table_result != ERROR_INSUFFICIENT_BUFFER || bytes > 1024 * 1024) {
            return fail("table_size", table_result);
        }

        std::vector<unsigned char> table_bytes(bytes);
        const auto table_read = GetExtendedTcpTable(table_bytes.data(), &bytes, FALSE, family,
                                                    TCP_TABLE_OWNER_PID_LISTENER, 0);

        if (table_read != NO_ERROR) {
            return fail("table_read", table_read);
        }

        if (family == AF_INET) {
            const auto table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(table_bytes.data());

            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                if (table->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                    ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) {
                    if (diagnostic) {
                        diagnostic->listener_pid = table->table[i].dwOwningPid;
                        in_addr local{};
                        local.s_addr = table->table[i].dwLocalAddr;
                        ::inet_ntop(family, &local, diagnostic->listener_address.data(),
                                    diagnostic->listener_address.size());
                    }

                    return fail("listener", 0);
                }
            }
        } else {
            const auto table =
                    reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID *>(table_bytes.data());

            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                if (table->table[i].dwState == MIB_TCP_STATE_LISTEN &&
                    ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) {
                    if (diagnostic) {
                        diagnostic->listener_pid = table->table[i].dwOwningPid;
                        IN6_ADDR local{};
                        std::memcpy(&local, table->table[i].ucLocalAddr, sizeof local);
                        ::inet_ntop(family, &local, diagnostic->listener_address.data(),
                                    diagnostic->listener_address.size());
                    }

                    return fail("listener", 0);
                }
            }
        }

        SOCKET fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == INVALID_SOCKET) {
            return fail("socket", WSAGetLastError());
        }

        struct SocketCleanup {
            SOCKET fd;

            ~SocketCleanup() {
                closesocket(fd);
            }
        } socket_cleanup{fd};
#else
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
#endif

        if (::bind(fd, address, address_size) != 0) {
#ifdef _WIN32
            return fail("bind", WSAGetLastError());
#else
            return fail("bind", errno);
#endif
        }

        if (::listen(fd, 1) != 0) {
#ifdef _WIN32
            return fail("listen", WSAGetLastError());
#else
            return fail("listen", errno);
#endif
        }
    }

    if (diagnostic) {
        diagnostic->stage = "free";
    }

    return true;
}
} // namespace voyemu
