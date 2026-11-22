#pragma once
// The host parts of cli_flow.test.cpp: cli_flow_posix.test.cpp and cli_flow_windows.test.cpp.
#include "process.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Runs the public CLI with its standard output and error in separate private files, so structured
// stderr diagnostics are never response authority; Windows only.
voyemu::ProcessResult fixture_public_cli(const std::filesystem::path &executable,
                                         const std::vector<std::string> &arguments,
                                         const std::filesystem::path &root);

// Sends SIGTERM, SIGKILL or SIGPIPE to pid; whether it was delivered. POSIX only.
enum class FixtureSignal {
    terminate,
    kill,
    broken_pipe
};
bool signal_process(std::int64_t pid, FixtureSignal signal);
// The start identity and the arguments of pid that os/process.hpp reports; POSIX only.
std::optional<std::string> process_start(std::int64_t pid);
std::vector<std::string> process_arguments(std::int64_t pid);
// Whether path is a file of the effective user with mode 0600, without following a link; POSIX
// only.
bool owner_only_file(const std::filesystem::path &path);

// A process opened by pid for waiting on its exit, which a later process with a reused pid cannot
// pass for; Windows only.
class ProcessExit {
public:
    explicit ProcessExit(std::int64_t pid);
    ~ProcessExit();
    ProcessExit(const ProcessExit &) = delete;
    ProcessExit &operator=(const ProcessExit &) = delete;
    bool opened() const;
    bool exited_within(std::chrono::milliseconds timeout) const;

private:
    std::intptr_t handle_ = 0;
};
