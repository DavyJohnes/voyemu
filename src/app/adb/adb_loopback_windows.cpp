// AOSP sysdeps.h must precede the other ADB headers.
#include "sysdeps.h"

#include "adb_loopback_socket.hpp"
#include "adb_utils.h"

bool adb_host_connect_loopback(borrowed_fd fd, uint16_t port, const std::atomic<bool> &stopping);

int adb_loopback_poll(adb_pollfd *item, int timeout_ms) {
    return adb_poll(item, 1, timeout_ms);
}

ssize_t adb_loopback_transfer(borrowed_fd fd, uint8_t *data, size_t size, bool writing) {
    return writing ? adb_write(fd, data, static_cast<int>(size))
                   : adb_read(fd, data, static_cast<int>(size));
}

unique_fd adb_loopback_connect(uint16_t port, const std::atomic<bool> &stopping) {
    unique_fd fd(adb_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));

    if (!fd.ok() || !set_file_block_mode(fd, false)) {
        return unique_fd();
    }

    if (!adb_host_connect_loopback(fd, port, stopping)) {
        return unique_fd();
    }

    return fd;
}

void adb_loopback_ignore_broken_pipe() {}
