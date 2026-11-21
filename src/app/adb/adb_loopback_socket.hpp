#pragma once
// The host socket operations of the loopback connections of the embedded ADB;
// adb_loopback_posix.cpp and adb_loopback_windows.cpp implement them. Include after sysdeps.h.
#include <atomic>
#include <cstdint>

// Polls one socket, reporting EINTR to the caller instead of retrying.
int adb_loopback_poll(adb_pollfd *item, int timeout_ms);
// One non-blocking send or receive of at most size bytes without a broken-pipe signal.
ssize_t adb_loopback_transfer(borrowed_fd fd, uint8_t *data, size_t size, bool writing);
// A non-blocking TCP connection to 127.0.0.1:port; an invalid descriptor on failure or stop.
unique_fd adb_loopback_connect(uint16_t port, const std::atomic<bool> &stopping);
// Makes a write to a closed socket fail with EPIPE instead of a signal where sockets need it.
void adb_loopback_ignore_broken_pipe();
