#pragma once
// The host parts of session hosting: cli_session_posix.cpp and cli_session_windows.cpp.
#include "cli_arguments.hpp"
#include "model.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace voyemu::cli {
voyemu::Json active_record(const Arguments &args);

// The exclusive lock of a session file, taken without waiting; throws SESSION_LOCK when held.
class OperationLock {
public:
    explicit OperationLock(const std::filesystem::path &path);
    ~OperationLock();
    OperationLock(const OperationLock &) = delete;
    OperationLock &operator=(const OperationLock &) = delete;

private:
    std::intptr_t handle_ = -1;
};

// Whether the supervisor of session id no longer serves its endpoint: a removed Unix socket on
// POSIX, an endpoint that no longer answers for the session on Windows.
bool session_endpoint_gone(const std::filesystem::path &endpoint, const std::string &id);
// Completes a stopped epoch under its locks: POSIX stops what a vanished supervisor left and
// waits for the session resources; true when the session is gone.
bool finish_stopped_epoch(const std::filesystem::path &directory, const std::string &id,
                          const std::filesystem::path &endpoint, bool gone,
                          std::chrono::steady_clock::time_point deadline);
// The terminal record of a stop that needs no supervisor, none when the supervisor must answer:
// POSIX stops a session whose locks are free, Windows recovers an owned session without endpoint.
std::optional<Json> stop_without_supervisor(const Arguments &args);

// The ownership proof of a host refresh, held while the refreshed profile is activated: the
// supervisor lock and the stopped owned epoch on POSIX, the StoppedAndroidLease on Windows.
class RefreshFence {
public:
    explicit RefreshFence(const Arguments &args);
    ~RefreshFence();
    RefreshFence(const RefreshFence &) = delete;
    RefreshFence &operator=(const RefreshFence &) = delete;
    void before_activation();
    void after_activation();

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace voyemu::cli
