#include "cli_commands.hpp"
#include "cli_session_host.hpp"
#include <windows.h>

namespace voyemu::cli {
OperationLock::OperationLock(const std::filesystem::path &path) {
    voyemu::ensure_private_directory(path.parent_path());
    const HANDLE file = voyemu::detail::open_private_state_lock(path, GENERIC_READ | GENERIC_WRITE,
                                                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL);

    if (file == INVALID_HANDLE_VALUE) {
        throw voyemu::cli::LocalizedError("SESSION_LOCK", "error.profile_busy");
    }

    handle_ = reinterpret_cast<std::intptr_t>(file);
}

OperationLock::~OperationLock() {
    const auto file = reinterpret_cast<HANDLE>(handle_);

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
}

bool session_endpoint_gone(const std::filesystem::path &endpoint, const std::string &id) {
    try {
        auto result = voyemu::request(endpoint, {{"op", "status"}, {"session_id", id}}, 100);

        return result.value("session_id", std::string{}) != id;
    } catch (const std::exception &) {
        return true;
    }
}

bool finish_stopped_epoch(const std::filesystem::path &, const std::string &,
                          const std::filesystem::path &, bool gone,
                          std::chrono::steady_clock::time_point) {
    return gone;
}

std::optional<Json> stop_without_supervisor(const Arguments &args) {
    if (args.ownership_id.empty()) {
        return std::nullopt;
    }

    const auto active = active_record(args);

    if (!voyemu::api::StoppedAndroidLease::endpoint_absent(
                active.at("endpoint").get<std::string>())) {
        return std::nullopt;
    }

    voyemu::api::StoppedAndroidLease lease(args.root, args.profile, args.machine,
                                           active.at("session_id").get<std::string>(),
                                           args.ownership_id);
    auto terminal = lease.publish_terminal();
    terminal["ok"] = true;

    return terminal;
}

struct RefreshFence::State {
    voyemu::api::StoppedAndroidLease lease;

    explicit State(const Arguments &args)
        : lease(args.root, args.profile, args.machine, args.refresh_session, args.ownership_id) {}
};

RefreshFence::RefreshFence(const Arguments &args) {
    if (!args.exclusive || voyemu::os::executable_path() != args.root / "voyemu.exe") {
        throw VoyemuError("HOST_REFRESH_OWNER", "explicit exclusive current root program required");
    }

    state_ = std::make_unique<State>(args);

    if (!state_->lease.original_runtime().value("stopped", false)) {
        state_->lease.publish_terminal();
    }
}

RefreshFence::~RefreshFence() = default;

void RefreshFence::before_activation() {
    state_->lease.verify();
}

void RefreshFence::after_activation() {
    state_->lease.verify_host_absence();
}
} // namespace voyemu::cli
