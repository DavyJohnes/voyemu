#include "build_cli_test_host.hpp"
#include <csignal>

bool interrupt_child(std::int64_t pid) {
    return ::kill(static_cast<pid_t>(pid), SIGINT) == 0;
}
