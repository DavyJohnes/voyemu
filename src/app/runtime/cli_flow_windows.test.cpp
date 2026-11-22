#include "cli_flow_test_host.hpp"
#include "model.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <stdexcept>
using namespace voyemu;

namespace {
void require(bool result, const std::string &message) {
    if (!result) {
        throw std::runtime_error(message);
    }
}
} // namespace

// Test-only channel separation: structured stderr diagnostics are never response authority.
ProcessResult fixture_public_cli(const std::filesystem::path &executable,
                                 const std::vector<std::string> &arguments,
                                 const std::filesystem::path &root) {
    struct Handle {
        HANDLE value = INVALID_HANDLE_VALUE;

        ~Handle() {
            if (value && value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    };
    const auto slot = root / ".local/build" / ("split-cli-" + random_token());

    ensure_private_directory(slot);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle output{CreateFileW((slot / "stdout.json").c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_DELETE, &security, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr)};
    Handle errors{CreateFileW((slot / "stderr.log").c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_DELETE, &security, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr)};
    Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(output.value != INVALID_HANDLE_VALUE && errors.value != INVALID_HANDLE_VALUE &&
                    input.value != INVALID_HANDLE_VALUE,
            "private CLI channels unavailable");
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(job.value && SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                                 &limits, sizeof(limits)),
            "private CLI job unavailable");
    std::string command = windows_quote(executable.string());

    for (const auto &argument : arguments) {
        command += " " + windows_quote(argument);
    }

    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.data(),
                                            static_cast<int>(command.size()), nullptr, 0);
    require(length > 0, "private CLI command encoding invalid");
    std::wstring wide(length, L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.data(),
                                static_cast<int>(command.size()), wide.data(), length) == length,
            "private CLI command encoding changed");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value;
    startup.hStdOutput = output.value;
    startup.hStdError = errors.value;
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.c_str(), wide.data(), nullptr, nullptr, TRUE,
                           CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, root.c_str(), &startup,
                           &process),
            "private CLI spawn failed");
    Handle child{process.hProcess}, thread{process.hThread};

    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 126);
        require(WaitForSingleObject(child.value, 3000) == WAIT_OBJECT_0,
                "unassigned own CLI cleanup unverified");
        throw std::runtime_error("private CLI job assignment failed");
    }

    require(ResumeThread(thread.value) != DWORD(-1), "private CLI resume failed");
    const auto waited = WaitForSingleObject(child.value, 30000);
    const bool timeout = waited == WAIT_TIMEOUT;

    if (waited != WAIT_OBJECT_0) {
        require(TerminateJobObject(job.value, 124) &&
                        WaitForSingleObject(child.value, 3000) == WAIT_OBJECT_0,
                "private CLI timeout cleanup unverified");
        require(timeout, "private CLI wait failed");
    }

    DWORD code = 0;
    require(GetExitCodeProcess(child.value, &code) && code != STILL_ACTIVE,
            "private CLI terminal state unavailable");
    CloseHandle(output.value);
    output.value = INVALID_HANDLE_VALUE;
    CloseHandle(errors.value);
    errors.value = INVALID_HANDLE_VALUE;

    if (timeout) {
        return {static_cast<int>(code), {}, true};
    }

    return {static_cast<int>(code), read_private_json(slot / "stdout.json", 512 * 1024).dump(),
            false};
}

bool signal_process(std::int64_t, FixtureSignal) {
    throw std::logic_error("process signals are a POSIX fixture");
}

std::optional<std::string> process_start(std::int64_t) {
    throw std::logic_error("process start identities are a POSIX fixture");
}

std::vector<std::string> process_arguments(std::int64_t) {
    throw std::logic_error("process arguments are a POSIX fixture");
}

bool owner_only_file(const std::filesystem::path &) {
    throw std::logic_error("file modes are a POSIX fixture");
}

ProcessExit::ProcessExit(std::int64_t pid)
    : handle_(reinterpret_cast<std::intptr_t>(
              OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid)))) {}

ProcessExit::~ProcessExit() {
    if (handle_) {
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
}

bool ProcessExit::opened() const {
    return handle_ != 0;
}

bool ProcessExit::exited_within(std::chrono::milliseconds timeout) const {
    return WaitForSingleObject(reinterpret_cast<HANDLE>(handle_),
                               static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
}
