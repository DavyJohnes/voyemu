#pragma once
// The host parts of adb_loopback.test.cpp: adb_loopback_posix.test.cpp and
// adb_loopback_windows.test.cpp, with the heap reading of adb_loopback_linux.test.cpp and
// adb_loopback_macos.test.cpp.
#include "os/socket.hpp"
#include <cstddef>
#include <functional>
#include <utility>

// The heap bytes this process holds allocated: the malloc zone statistics on macOS, mallinfo2 on
// Linux; POSIX only.
std::size_t heap_bytes_in_use();

// The outcome of interrupted_call: the signals sent to its thread, the first pthread_kill and
// pthread_sigmask failures, 0 when none, and whether call returned before stop.
struct InterruptedCall {
    unsigned signals = 0;
    int signal_error = 0;
    int mask_error = 0;
    bool finished = false;
};
// Runs call on a thread whose blocking system calls a no-op SIGUSR1 interrupts every 20 ms for up
// to 6.2 s, waits up to 6.7 s for call to return, then runs stop and joins the thread; POSIX only.
InterruptedCall interrupted_call(const std::function<void()> &call,
                                 const std::function<void()> &stop);

// A connected pair of Unix stream sockets; POSIX only.
std::pair<voyemu::os::SocketHandle, voyemu::os::SocketHandle> local_socket_pair();
