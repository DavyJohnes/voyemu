#include "android_port_lease.hpp"
#include "model.hpp"
#include <chrono>
#include <string>
#include <thread>

namespace voyemu {
namespace {
std::filesystem::path lease_path(unsigned console) {
    const auto directory = host_lease_directory();

    ensure_private_directory(directory);

    return directory / ("android-console-" + std::to_string(console) + ".lease");
}
} // namespace

AndroidPortLease::AndroidPortLease(unsigned console) : console_(console) {
    // A concurrent probe holds the lock shared for an instant; only a lasting holder refuses.
    const auto path = lease_path(console);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);

    for (;;) {
        lock_ = std::make_unique<os::FileLock>(path, true);

        if (lock_->held() || std::chrono::steady_clock::now() >= deadline) {
            return;
        }

        lock_.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool AndroidPortLease::held() const {
    return lock_->held();
}

unsigned AndroidPortLease::console() const {
    return console_;
}

bool android_port_leasable(unsigned console) {
    // A shared probe never excludes a competing probe.
    return os::FileLock(lease_path(console), false).held();
}
} // namespace voyemu
