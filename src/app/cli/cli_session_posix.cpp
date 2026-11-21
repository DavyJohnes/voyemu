#include "cli_commands.hpp"
#include "cli_session_host.hpp"
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>

namespace voyemu::cli {
OperationLock::OperationLock(const std::filesystem::path &path) {
    voyemu::ensure_private_directory(path.parent_path());
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);

    if (fd < 0 || ::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (fd >= 0) {
            ::close(fd);
        }

        throw voyemu::cli::LocalizedError("SESSION_LOCK", "error.profile_busy");
    }

    handle_ = fd;
}

OperationLock::~OperationLock() {
    if (handle_ >= 0) {
        ::close(static_cast<int>(handle_));
    }
}

bool session_endpoint_gone(const std::filesystem::path &endpoint, const std::string &) {
    return !std::filesystem::exists(endpoint) && !std::filesystem::is_symlink(endpoint);
}

bool finish_stopped_epoch(const std::filesystem::path &directory, const std::string &id,
                          const std::filesystem::path &endpoint, bool gone,
                          std::chrono::steady_clock::time_point deadline) {
    if (!gone) {
        (void)voyemu::stop_orphaned_session(directory, id, endpoint.string());
    }

    voyemu::await_session_resources(directory, deadline);

    return true;
}

// The launch and supervisor locks of a machine are free only while no start or supervisor of it
// runs; the receipts of its active session then decide what a stop has left to do.
std::optional<Json> stop_without_supervisor(const Arguments &args) {
    const auto active = voyemu::native_current_active_directory(args.root, args.profile,
                                                                args.machine);

    if (!voyemu::ordinary_native_launch_file(active / (args.profile + ".json"))) {
        return std::nullopt;
    }

    std::optional<OperationLock> launch, supervisor;
    try {
        launch.emplace(active / (args.profile + ".launch.lock"));
        supervisor.emplace(active / (args.profile + ".supervisor.lock"));
    } catch (const std::exception &) {
        return std::nullopt; // A running start or supervisor answers on its endpoint.
    }

    const auto record = active_record(args);
    const auto id = record.at("session_id").get<std::string>();

    if (!hex_text(id, 32)) {
        throw VoyemuError("SESSION_IDENTITY", "invalid active epoch");
    }

    const auto directory = voyemu::native_session_directory(args.root, args.profile, args.machine,
                                                            id);

    if (record.value("directory", std::string{}) != directory.string()) {
        throw VoyemuError("SESSION_IDENTITY", "active session directory differs");
    }

    auto terminal = voyemu::stop_orphaned_session(directory, id,
                                                  record.at("endpoint").get<std::string>());
    terminal["ok"] = true;

    return terminal;
}

namespace {
void verify_refresh_owner(const Arguments &args) {
    auto token = [](const std::string &value) {
        return hex_text(value, 32);
    };

    if (!args.exclusive || !token(args.refresh_session) || !token(args.ownership_id)) {
        throw VoyemuError("HOST_REFRESH_OWNER", "explicit stopped session lease required");
    }

    const auto active = active_record(args);

    if (active.at("session_id") != args.refresh_session) {
        throw VoyemuError("HOST_REFRESH_OWNER", "active epoch differs");
    }

    const auto directory = voyemu::native_session_directory(args.root, args.profile, args.machine,
                                                            args.refresh_session);
    const auto owner = voyemu::read_private_json(directory / "automation-owner.json");
    const auto terminal = voyemu::read_private_json(directory / "runtime.json");

    if (owner.at("session_id") != args.refresh_session || owner.at("profile") != args.profile ||
        owner.at("ownership_id") != args.ownership_id ||
        terminal.at("session_id") != args.refresh_session || !terminal.value("stopped", false) ||
        terminal.at("endpoint") != active.at("endpoint") ||
        std::filesystem::exists(active.at("endpoint").get<std::string>())) {
        throw VoyemuError("HOST_REFRESH_OWNER", "stopped owned epoch required");
    }

    const auto shutdown = terminal.at("deliveries").at("android").at("shutdown");

    if (!shutdown.value("wrapper_terminal", false) || !shutdown.value("ports_released", false)) {
        throw VoyemuError("HOST_REFRESH_STOP", "verified Android shutdown required");
    }

    const auto launch = voyemu::read_private_json(
            voyemu::profile_launch_directory(args.root, args.profile, args.machine) /
            "android-launch.json");
    const auto port = launch.at("port").get<unsigned>(),
               grpc = launch.at("sdk_control").at("grpc_port").get<unsigned>();
    std::vector<int> sockets;
    struct Cleanup {
        std::vector<int> &sockets;

        ~Cleanup() {
            for (auto fd : sockets) {
                ::close(fd);
            }
        }
    } cleanup{sockets};

    for (auto value : {port, port + 1, grpc}) {
        if (value < 1024 || value > 65535) {
            throw VoyemuError("HOST_REFRESH_STOP", "invalid listener port");
        }

        const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);

        if (fd < 0) {
            throw VoyemuError("HOST_REFRESH_STOP", "listener probe failed");
        }

        sockets.push_back(fd);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(value);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            throw VoyemuError("HOST_REFRESH_STOP", "Android listener remains");
        }
    }
}
} // namespace

struct RefreshFence::State {
    OperationLock supervisor;

    explicit State(const Arguments &args)
        : supervisor(
                  voyemu::native_current_active_directory(args.root, args.profile, args.machine) /
                  (args.profile + ".supervisor.lock")) {
        verify_refresh_owner(args);
    }
};

RefreshFence::RefreshFence(const Arguments &args) : state_(std::make_unique<State>(args)) {}

RefreshFence::~RefreshFence() = default;

void RefreshFence::before_activation() {}

void RefreshFence::after_activation() {}
} // namespace voyemu::cli
