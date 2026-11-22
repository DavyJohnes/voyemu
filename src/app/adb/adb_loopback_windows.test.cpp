#include "adb_loopback_test_host.hpp"
#include <stdexcept>

std::size_t heap_bytes_in_use() {
    throw std::logic_error("the heap reading is a POSIX fixture");
}

InterruptedCall interrupted_call(const std::function<void()> &, const std::function<void()> &) {
    throw std::logic_error("the interrupting signal is a POSIX fixture");
}

std::pair<voyemu::os::SocketHandle, voyemu::os::SocketHandle> local_socket_pair() {
    throw std::logic_error("the Unix socket pair is a POSIX fixture");
}
