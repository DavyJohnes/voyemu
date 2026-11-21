#pragma once
// The parts of process.hpp that process.cpp shares with the host backends in os/child_process_*.
#include "os/process.hpp"
#include "os/socket.hpp"
#include "process.hpp"
#include <atomic>
#include <optional>
#include <thread>

namespace voyemu {
// The sanitized output of one child: a reader thread passes the pipe through the sanitizer into
// the private output file.
struct SanitizedProcessOutput {
    os::FileHandle read = os::no_file, file = os::no_file;
    std::atomic<bool> stop = false, complete = false, failed = false;
    std::thread reader;

    ~SanitizedProcessOutput();
    void run(ChildProcess::OutputSanitizer sanitizer, std::int64_t pid, std::size_t limit) noexcept;
};

// Starts spec with output as its standard output and error; the backend of the host defines it.
ChildProcess launch_child_process(const ProcessSpec &spec, std::intptr_t output);
// The process id of a started child and its exit code once finished; the host backend defines them.
std::int64_t child_pid(const ChildProcess::Impl &impl);
std::optional<int> child_exit(const ChildProcess::Impl &impl);
// Rejects an environment variable with an embedded NUL, an empty name or a name with '='.
void check_process_variable(const std::string &key, const std::string &value);
// Throws unless spec and the bounds describe a capture that execute_capture accepts.
void check_capture_request(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit,
                           int maximum_timeout_ms);
// Whether address is the IPv6 loopback or unspecified address, or one of their IPv4-mapped forms.
bool local_ipv6_address(const in6_addr &address);
} // namespace voyemu
