// The Windows backend of ChildProcess, process capture and the loopback port probe of process.hpp.
#include "../platform/wine_native_backend_windows.hpp"
#include "core/process.hpp"
#include "core/process_internal.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/process.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
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
struct ChildProcess::Impl {
    HANDLE process = nullptr, job = nullptr;
    DWORD id = 0;
    std::unique_ptr<WineNativeBackendWindows> wine_native;
    bool finished = false;
    int exit_code = -1;
};

std::string_view ChildProcess::pid_namespace() const {
    return impl_ && impl_->wine_native ? "macos-diagnostic" : "windows";
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
        check_process_variable(key, value);

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
        child.impl_.reset(new Impl());
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
                    ntohs(static_cast<u_short>(row.dwLocalPort)) == port &&
                    local_ipv6_address(local) && owned(row.dwOwningPid)) {
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
    if (sanitized_) {
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
    check_capture_request(spec, timeout_ms, output_limit, maximum_timeout_ms);

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

ChildProcess launch_child_process(const ProcessSpec &spec, std::intptr_t output) {
    ChildProcess child;

    child.impl_.reset(launch_native(spec, reinterpret_cast<HANDLE>(output)));

    return child;
}

std::int64_t child_pid(const ChildProcess::Impl &impl) {
    return impl.wine_native ? impl.wine_native->pid() : impl.id;
}

std::optional<int> child_exit(const ChildProcess::Impl &impl) {
    if (impl.wine_native) {
        return impl.wine_native->exit_code();
    }

    return impl.finished ? std::optional(impl.exit_code) : std::nullopt;
}

void ChildProcess::ImplDelete::operator()(Impl *impl) const {
    delete impl;
}

namespace os {
std::vector<std::pair<std::string, std::string>> environment_variables() {
    const auto utf8 = [](const std::wstring &value) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr,
                                             nullptr);
        std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                            size, nullptr, nullptr);

        return result;
    };
    std::vector<std::pair<std::string, std::string>> result;

    if (const auto environment = ::GetEnvironmentStringsW()) {
        for (const wchar_t *entry = environment; *entry; entry += std::wcslen(entry) + 1) {
            const std::wstring row(entry);
            // The hidden per-drive directories start with '=': C:=C:\work.
            const auto equal = row.find(L'=', row.starts_with(L'=') ? 1 : 0);

            if (equal != std::wstring::npos) {
                result.emplace_back(utf8(row.substr(0, equal)), utf8(row.substr(equal + 1)));
            }
        }

        ::FreeEnvironmentStringsW(environment);
    }

    return result;
}

void close_file(FileHandle file) {
    CloseHandle(reinterpret_cast<HANDLE>(file));
}

FileHandle create_capture_output(const std::filesystem::path &path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw VoyemuError("PROCESS_LOG", "fresh sanitized output required");
    }

    BY_HANDLE_FILE_INFORMATION identity{};

    if (!GetFileInformationByHandle(file, &identity) || identity.nNumberOfLinks != 1 ||
        (identity.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(file);
        throw VoyemuError("PROCESS_LOG", "ordinary sanitized output required");
    }

    return reinterpret_cast<FileHandle>(file);
}

void create_capture_pipe(FileHandle &read, FileHandle &write) {
    SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
    HANDLE reader = nullptr, writer = nullptr;

    if (!CreatePipe(&reader, &writer, &security, 16384)) {
        throw VoyemuError("PROCESS_PIPE", "cannot create sanitized capture");
    }

    read = reinterpret_cast<FileHandle>(reader);

    if (!SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(writer);
        throw VoyemuError("PROCESS_PIPE", "cannot isolate sanitized reader");
    }

    write = reinterpret_cast<FileHandle>(writer);
}

long long read_capture(FileHandle read, char *data, std::size_t size) {
    const auto pipe = reinterpret_cast<HANDLE>(read);
    DWORD available = 0;

    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -2;
    }

    if (!available) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        return -1;
    }

    DWORD count = 0;

    if (!ReadFile(pipe, data, std::min<DWORD>(available, static_cast<DWORD>(size)), &count,
                  nullptr)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -2;
    }

    return count ? static_cast<long long>(count) : -1;
}

long long write_capture(FileHandle file, const char *data, std::size_t size) {
    DWORD count = 0;

    if (!WriteFile(reinterpret_cast<HANDLE>(file), data, static_cast<DWORD>(size), &count,
                   nullptr) ||
        !count) {
        return -1;
    }

    return count;
}

void set_environment_variable(const std::string &name, const std::string &value) {
    _putenv_s(name.c_str(), value.c_str());
}
} // namespace os
} // namespace voyemu
