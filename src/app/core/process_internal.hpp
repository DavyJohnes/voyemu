#pragma once
// The parts of process.hpp that process.cpp shares with the host backends in os/child_process_*.
#include "os/process.hpp"
#include "os/socket.hpp"
#include "process.hpp"
#include <atomic>
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
// Throws unless spec and the bounds describe a capture that execute_capture accepts.
void check_capture_request(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit,
                           int maximum_timeout_ms);
// Whether address is the IPv6 loopback or unspecified address, or one of their IPv4-mapped forms.
bool local_ipv6_address(const in6_addr &address);
} // namespace voyemu
