#pragma once
#include "process.hpp"
namespace voyemu::runtime {
// Ends a provider child with every descendant: a POSIX process group that stays unreaped until it
// is killed (provider_reaper_posix.cpp), the kill-on-close job of ChildProcess on Windows
// (provider_reaper_windows.cpp). A child that did not finish dies with the object.
class ProviderReaper {
public:
    explicit ProviderReaper(ChildProcess &child);
    ~ProviderReaper();
    ProviderReaper(const ProviderReaper &) = delete;
    ProviderReaper &operator=(const ProviderReaper &) = delete;
    // Whether the child exited; its exit code is read after finish().
    bool exited();
    // Ends the descendants of the exited child and collects its exit code.
    void finish();

private:
    ChildProcess &child_;
    std::int64_t pid_;
    bool active_ = true;
};
} // namespace voyemu::runtime
