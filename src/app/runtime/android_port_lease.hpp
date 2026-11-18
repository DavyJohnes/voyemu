#pragma once
#include "os/fs.hpp"
#include <filesystem>
#include <memory>

namespace voyemu {
// Directory of the leases that the Voyemu processes of this host user share.
std::filesystem::path host_lease_directory();

// Exclusive claim of one Android console tuple (console, console + 1, console + 30000) among the
// Voyemu processes of this host user. A free listener probe cannot claim a port before the SDK
// binds it; the owner holds the lease from launch until its SDK stops, and the OS releases it
// when the owner exits.
class AndroidPortLease {
public:
    explicit AndroidPortLease(unsigned console);
    bool held() const;
    unsigned console() const;

private:
    unsigned console_;
    std::unique_ptr<os::FileLock> lock_;
};
// Whether no other process holds the lease of console now; the ports remain to be probed.
bool android_port_leasable(unsigned console);
} // namespace voyemu
