// The Windows backend of ChildProcess, process capture and the loopback port probe of process.hpp.
#include "../platform/wine_native_backend_windows.hpp"
#include "core/process.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/process.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
namespace voyemu {
struct SanitizedProcessOutput {
    HANDLE read = nullptr, file = INVALID_HANDLE_VALUE;
    std::atomic<bool> stop = false, complete = false, failed = false;
    std::thread reader;

    ~SanitizedProcessOutput() {
        stop = true;

        if (reader.joinable()) {
            reader.join();
        }

        if (read) {
            CloseHandle(read);
        }

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
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
                    DWORD count = 0;

                    if (!WriteFile(file, safe.data() + offset,
                                   static_cast<DWORD>(safe.size() - offset), &count, nullptr) ||
                        !count) {
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
        }
    }
};
struct ChildProcess::Impl {
    HANDLE process = nullptr, job = nullptr;
    DWORD id = 0;
    std::unique_ptr<WineNativeBackendWindows> wine_native;
    std::unique_ptr<SanitizedProcessOutput> sanitized;
    bool finished = false;
    int exit_code = -1;
};

std::int64_t ChildProcess::pid() const {
    if (!impl_) {
        return 0;
    }

    if (impl_->wine_native) {
        return impl_->wine_native->pid();
    }

    return impl_->id;
}

std::string_view ChildProcess::pid_namespace() const {
    return impl_ && impl_->wine_native ? "macos-diagnostic" : "windows";
}

int ChildProcess::exit_code() const {
    if (impl_ && impl_->wine_native) {
        return impl_->wine_native->exit_code();
    }

    return impl_ && impl_->finished ? impl_->exit_code : -1;
}

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
        check_process_string(key);
        check_process_string(value);

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

ChildProcess ChildProcess::launch(const ProcessSpec &spec) {
    ChildProcess child;

    if (spec.wine_native_diagnostic) {
        child.impl_ = std::make_unique<Impl>();
        child.impl_->wine_native = WineNativeBackendWindows::launch(spec);

        return child;
    }

    HANDLE file = os::child_output_handle(spec.output);

    try {
        child.impl_.reset(launch_native(spec, file));
    } catch (...) {
        CloseHandle(file);
        throw;
    }

    CloseHandle(file);

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
    if (impl_ && impl_->wine_native) {
        return impl_->wine_native->alive();
    }

    if (!impl_ || impl_->finished) {
        return false;
    }

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
}

bool ChildProcess::owns_tcp_listener(unsigned port) {
    if (impl_ && impl_->wine_native) {
        return impl_->wine_native->owns_tcp_listener(port);
    }

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

    return false;
}

void ChildProcess::terminate(int grace_ms) {
    if (impl_ && impl_->wine_native) {
        impl_->wine_native->terminate(grace_ms);

        return;
    }

    if (!alive()) {
        return;
    }

    // A retained kernel process handle cannot accidentally address a reused PID.
    if (!TerminateJobObject(impl_->job, 0)) {
        throw VoyemuError("PROCESS_STOP", "owned lifetime refused termination");
    }

    WaitForSingleObject(impl_->process, static_cast<DWORD>(std::max(grace_ms, 1000)));
    alive();
}

void ChildProcess::detach() {
    if (impl_ && impl_->sanitized) {
        throw VoyemuError("PROCESS_SANITIZER", "captured child cannot detach");
    }

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

        if (::bind(fd, address, address_size) != 0) {
            return fail("bind", WSAGetLastError());
        }

        if (::listen(fd, 1) != 0) {
            return fail("listen", WSAGetLastError());
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
