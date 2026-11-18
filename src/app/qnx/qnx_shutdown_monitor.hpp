#pragma once
#include "model.hpp"
#include <filesystem>
#include <functional>
#include <memory>
namespace voyemu {
// Owns a dedicated event QMP channel: a private unix socket on macOS, the owned
// loopback TCP endpoint on Windows. The endpoint is a path string or {host,port}.
class QnxShutdownMonitor {
public:
    QnxShutdownMonitor(Json endpoint, std::uint64_t generation);
    ~QnxShutdownMonitor();
    void connect_owned(std::int64_t pid, const std::function<void()> &verify_owner = {});
    void drain();
    bool guest_reset(std::int64_t pid, std::uint64_t generation) const;
    Json evidence() const;
    void finish_owned_exit(std::int64_t pid, std::uint64_t generation);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voyemu
