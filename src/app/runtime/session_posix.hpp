#pragma once
#include "session_internal.hpp"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

// Unix-domain socket helpers of the macOS and Linux parts of Session.
namespace voyemu::session_detail {
struct Fd {
    int value = -1;

    explicit Fd(int fd) : value(fd) {}

    ~Fd() {
        if (value >= 0) {
            ::close(value);
        }
    }
};

void wait_fd(int fd, short events, int timeout);
void send_text(int fd, const std::string &text, int timeout);
std::string receive_line(int fd, int timeout, std::string *remainder = nullptr,
                         std::size_t limit = maximum_message);
int unix_connect(const std::filesystem::path &endpoint, int timeout, bool owned);
} // namespace voyemu::session_detail
