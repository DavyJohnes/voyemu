#include "os/child_watch.hpp"
#include "os/process.hpp"
#include <unistd.h>

namespace voyemu::os {
ChildWatch::~ChildWatch() = default;

ChildWatch::Opening ChildWatch::open(std::uint64_t pid) {
    if (pid == 0 || pid > 0x7fffffffULL) {
        return Opening::invalid;
    }

    if (!process_exists(static_cast<std::int64_t>(pid))) {
        return Opening::unavailable;
    }

    if (pid != static_cast<std::uint64_t>(::getpid()) &&
        parent_process(static_cast<std::int64_t>(pid)) != ::getpid()) {
        return Opening::foreign;
    }

    handle_ = static_cast<std::intptr_t>(pid);
    pid_ = pid;

    return Opening::opened;
}

bool ChildWatch::alive() const {
    return handle_ && process_exists(static_cast<std::int64_t>(handle_));
}
} // namespace voyemu::os
