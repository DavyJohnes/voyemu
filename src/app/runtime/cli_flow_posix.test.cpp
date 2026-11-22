#include "cli_flow_test_host.hpp"
#include "os/process.hpp"
#include <csignal>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

voyemu::ProcessResult fixture_public_cli(const std::filesystem::path &,
                                         const std::vector<std::string> &,
                                         const std::filesystem::path &) {
    throw std::logic_error("the split CLI channels are a Windows fixture");
}

bool signal_process(std::int64_t pid, FixtureSignal signal) {
    const int number = signal == FixtureSignal::terminate ? SIGTERM
                       : signal == FixtureSignal::kill    ? SIGKILL
                                                          : SIGPIPE;

    return ::kill(static_cast<pid_t>(pid), number) == 0;
}

std::optional<std::string> process_start(std::int64_t pid) {
    return voyemu::os::process_start_identity(pid);
}

std::vector<std::string> process_arguments(std::int64_t pid) {
    return voyemu::os::process_arguments(pid);
}

bool owner_only_file(const std::filesystem::path &path) {
    struct stat status{};

    return ::lstat(path.c_str(), &status) == 0 && status.st_uid == ::geteuid() &&
           (status.st_mode & 0777) == 0600;
}

ProcessExit::ProcessExit(std::int64_t) {
    throw std::logic_error("the process exit wait is a Windows fixture");
}

ProcessExit::~ProcessExit() = default;

bool ProcessExit::opened() const {
    return handle_ != 0;
}

bool ProcessExit::exited_within(std::chrono::milliseconds) const {
    return false;
}
