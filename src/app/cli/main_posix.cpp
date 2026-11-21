#include "main_host.hpp"
#include <csignal>
namespace voyemu::cli {
std::optional<int> host_command(int, char **) {
    return std::nullopt;
}

void ignore_broken_pipe() {
    std::signal(SIGPIPE, SIG_IGN);
}
} // namespace voyemu::cli
