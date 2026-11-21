#pragma once
#include <cstdint>

namespace voyemu::os {
// A child process of this process, or this process itself, watched by its id: Windows holds a
// process handle so that a reused id cannot pass for it, POSIX checks the live id.
class ChildWatch {
public:
    enum class Opening {
        opened,
        invalid,     // 0 or beyond the process ids of the host
        unavailable, // no live process
        foreign      // not a child of this process
    };
    ChildWatch() = default;
    ~ChildWatch();
    ChildWatch(const ChildWatch &) = delete;
    ChildWatch &operator=(const ChildWatch &) = delete;
    Opening open(std::uint64_t pid);
    bool alive() const;

    std::uint64_t pid() const {
        return pid_;
    }

private:
    std::intptr_t handle_ = 0; // the process HANDLE on Windows, the process id on POSIX
    std::uint64_t pid_ = 0;
};
} // namespace voyemu::os
