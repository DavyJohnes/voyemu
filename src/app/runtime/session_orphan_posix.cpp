#include "error.hpp"
#include "os/process.hpp"
#include "os/stat_time.hpp"
#include "session.hpp"
#include "session_internal.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>

// The stop of a POSIX session whose supervisor exited before its SDK; a Windows job ends with
// its supervisor.
namespace voyemu {
namespace {
using Clock = std::chrono::steady_clock;

bool carries(const std::vector<std::string> &arguments, const std::string &text) {
    for (const auto &argument : arguments) {
        if (argument.find(text) != std::string::npos) {
            return true;
        }
    }

    return false;
}

// A live process whose arguments name this session: its token or its directory.
bool session_process_alive(std::int64_t pid, const std::string &session_id,
                           const std::filesystem::path &directory) {
    if (pid <= 1 || !os::process_start_identity(pid)) {
        return false;
    }

    const auto arguments = os::process_arguments(pid);

    return arguments.empty() || carries(arguments, session_id) ||
           carries(arguments, directory.string());
}

std::int64_t pid_of(const Json &record, const char *key) {
    const auto value = record.is_object() ? record.value(key, Json(0)) : Json(0);

    return value.is_number_integer() ? value.get<std::int64_t>() : 0;
}

bool names_session(const std::vector<std::string> &arguments, const std::string &session_id,
                   const std::filesystem::path &directory) {
    const auto ram = ",mem-path=" + (directory / "qnx-ram.bin").string();

    for (const auto &argument : arguments) {
        if (argument == session_id || argument == directory.string() ||
            argument == "qemu.voyemu.token=" + session_id ||
            argument == (directory / "frame.bgra").string()) {
            return true;
        }

        const auto at = argument.find(ram);

        if (at != std::string::npos &&
            (at + ram.size() == argument.size() || argument[at + ram.size()] == ',')) {
            return true;
        }
    }

    return false;
}

bool recorded_process_alive(const Json &process) {
    const auto start = os::process_start_identity(pid_of(process, "pid"));

    return start && *start == process.value("start", std::string{});
}

void verify_process(const Json &process, const std::string &session_id,
                    const std::filesystem::path &directory) {
    const auto pid = pid_of(process, "pid");
    const auto arguments = os::process_arguments(pid);
    const auto executable = os::process_executable(pid);
    const auto nonce = process.value("nonce", std::uint64_t{0});
    bool nonce_matches = nonce == 0;

    for (std::size_t i = 1; i < arguments.size(); ++i) {
        if (arguments[i - 1] == "--worker-nonce" && arguments[i] == std::to_string(nonce)) {
            nonce_matches = true;
        }
    }

    if (process.value("start", std::string{}).empty() || executable.empty() ||
        executable.string() != process.value("executable", std::string{}) ||
        sha256_bytes(Json(arguments).dump()) != process.value("arguments_sha256", std::string{}) ||
        !names_session(arguments, session_id, directory) || !nonce_matches ||
        !recorded_process_alive(process)) {
        throw VoyemuError("SESSION_STOP_IDENTITY_UNKNOWN", "owned host process identity differs");
    }
}

void stop_recorded_process(const Json &process, const std::string &session_id,
                           const std::filesystem::path &directory) {
    for (const auto signal : {SIGTERM, SIGKILL}) {
        if (!recorded_process_alive(process)) {
            return;
        }

        verify_process(process, session_id, directory);
        const auto pid = pid_of(process, "pid");

        if (::kill(static_cast<pid_t>(pid), signal) != 0 && errno != ESRCH) {
            throw VoyemuError("SESSION_STOP_SIGNAL_UNKNOWN", "owned host process signal failed");
        }

        const auto deadline = Clock::now() + std::chrono::seconds(3);

        while (recorded_process_alive(process) && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    if (recorded_process_alive(process)) {
        throw VoyemuError("SESSION_STOP_TIMEOUT_UNKNOWN", "owned host process remains");
    }
}

Json socket_identity(const std::filesystem::path &endpoint) {
    struct stat status{};

    if (::lstat(endpoint.c_str(), &status) != 0) {
        if (errno == ENOENT) {
            return nullptr;
        }

        throw VoyemuError("SESSION_ENDPOINT_UNKNOWN", "cannot inspect control socket");
    }

    if (!S_ISSOCK(status.st_mode) || status.st_uid != ::geteuid() || (status.st_mode & 0077) ||
        status.st_nlink != 1) {
        throw VoyemuError("SESSION_ENDPOINT_OWNER", "private owned control socket required");
    }

    return {{"device", status.st_dev},
            {"inode", status.st_ino},
            {"uid", status.st_uid},
            {"mode", status.st_mode},
            {"ctime_seconds", os::changed_time(status).tv_sec},
            {"ctime_ns", os::changed_time(status).tv_nsec}};
}

void retire_endpoint(const std::filesystem::path &directory, const std::string &session_id,
                     const std::filesystem::path &endpoint) {
    const auto identity = socket_identity(endpoint);

    if (identity.is_null()) {
        return;
    }

    if (endpoint != session_detail::session_control_endpoint(directory, session_id)) {
        throw VoyemuError("SESSION_ENDPOINT_OWNER", "control socket path differs");
    }

    const auto receipt = read_private_json(directory / "control-socket.json", 4096);

    if (receipt.value("schema", 0) != 1 ||
        receipt.value("session_id", std::string{}) != session_id ||
        receipt.value("endpoint", std::string{}) != endpoint.string() ||
        receipt.value("identity", Json()) != identity) {
        throw VoyemuError("SESSION_ENDPOINT_OWNER", "control socket identity changed");
    }

    ensure_private_directory(endpoint.parent_path());
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto text = endpoint.string();

    if (text.size() >= sizeof address.sun_path) {
        throw VoyemuError("SESSION_ENDPOINT_OWNER", "control socket path too long");
    }

    std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        throw VoyemuError("SESSION_ENDPOINT_UNKNOWN", "cannot probe control socket");
    }

    if (::fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        ::close(fd);
        throw VoyemuError("SESSION_ENDPOINT_UNKNOWN", "cannot bound control socket probe");
    }

    const auto connected = ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address);
    const auto failure = errno;
    ::close(fd);

    if (connected == 0 || failure != ECONNREFUSED || socket_identity(endpoint) != identity) {
        throw VoyemuError("SESSION_ENDPOINT_UNKNOWN", "control socket absence not proven");
    }

    if (::unlink(endpoint.c_str()) != 0) {
        throw VoyemuError("SESSION_ENDPOINT_UNKNOWN", "cannot retire owned stale control socket");
    }
}
} // namespace

Json session_process_identity(std::int64_t pid) {
    const auto start = os::process_start_identity(pid);
    const auto executable = os::process_executable(pid);
    const auto arguments = os::process_arguments(pid);

    if (!start || executable.empty() || arguments.empty() ||
        os::process_start_identity(pid) != start) {
        throw VoyemuError("SESSION_PROCESS_IDENTITY_UNKNOWN",
                          "launched process identity unavailable");
    }

    return {{"pid", pid},
            {"start", *start},
            {"executable", executable.string()},
            {"arguments_sha256", sha256_bytes(Json(arguments).dump())}};
}

void record_session_process(const std::filesystem::path &directory, const std::string &session_id,
                            std::int64_t pid, std::uint64_t nonce,
                            const std::string &launch_intent) {
    const auto path = directory / "host-processes.json";
    auto receipt =
            std::filesystem::exists(path)
                    ? read_private_json(path, 65536)
                    : Json{{"schema", 1}, {"session_id", session_id}, {"processes", Json::array()}};

    if (receipt.value("schema", 0) != 1 || receipt.at("session_id") != session_id) {
        throw VoyemuError("SESSION_OWNER", "host process receipt epoch differs");
    }

    if (receipt.contains("pending_launch")
                ? launch_intent.empty() ||
                          receipt.at("pending_launch").value("intent", std::string{}) !=
                                  launch_intent
                : !launch_intent.empty()) {
        throw VoyemuError("SESSION_PROCESS_PENDING_UNKNOWN", "launch registration intent differs");
    }

    auto identity = session_process_identity(pid);
    identity["nonce"] = nonce;
    auto &processes = receipt["processes"];
    processes.erase(std::remove_if(processes.begin(), processes.end(),
                                   [](const auto &process) {
                                       return !recorded_process_alive(process);
                                   }),
                    processes.end());
    processes.push_back(std::move(identity));
    receipt.erase("pending_launch");
    write_private_json(path, receipt);
}

ChildProcess launch_session_process(const ProcessSpec &spec, const std::filesystem::path &directory,
                                    const std::string &session_id, std::uint64_t nonce) {
    const auto path = directory / "host-processes.json";
    auto receipt =
            std::filesystem::exists(path)
                    ? read_private_json(path, 65536)
                    : Json{{"schema", 1}, {"session_id", session_id}, {"processes", Json::array()}};

    if (receipt.value("schema", 0) != 1 || receipt.at("session_id") != session_id ||
        receipt.contains("pending_launch")) {
        throw VoyemuError("SESSION_PROCESS_PENDING_UNKNOWN", "previous host launch is unresolved");
    }

    const auto intent = random_token();
    receipt["pending_launch"] = {
            {"intent", intent}, {"executable", spec.executable.string()}, {"nonce", nonce}};
    write_private_json(path, receipt);
    ChildProcess child;

    try {
        child = ChildProcess::launch(spec);
    } catch (...) {
        // ChildProcess::launch throws before native spawn succeeds; no child handle exists.
        // Keep registration failures below out of this catch: their child may already exist.
        receipt.erase("pending_launch");
        write_private_json(path, receipt);
        throw;
    }

    record_session_process(directory, session_id, child.pid(), nonce, intent);

    return child;
}

void record_session_endpoint(const std::filesystem::path &directory, const std::string &session_id,
                             const std::filesystem::path &endpoint) {
    const auto identity = socket_identity(endpoint);

    if (identity.is_null()) {
        throw VoyemuError("SESSION_ENDPOINT_OWNER", "bound control socket required");
    }

    write_private_json(directory / "control-socket.json", {{"schema", 1},
                                                           {"session_id", session_id},
                                                           {"endpoint", endpoint.string()},
                                                           {"identity", identity}});
}

Json android_launch_identity(std::int64_t pid) {
    // An SDK that already exited has no identity to record; recovery then never signals its PID.
    const auto start = os::process_start_identity(pid);

    return start ? Json{{"sdk_start", *start}, {"sdk_identity", session_process_identity(pid)}}
                 : Json::object();
}

void refresh_android_launch_identity(const std::filesystem::path &directory,
                                     const std::string &session_id, std::int64_t pid,
                                     std::uint64_t generation,
                                     const std::filesystem::path &launcher,
                                     const std::filesystem::path &backend) {
    const auto path = directory / "session-owner.json";
    auto owner = read_private_json(path, 16384);
    auto &processes = owner["android_processes"];
    const auto identity = session_process_identity(pid);
    const auto arguments = os::process_arguments(pid);
    const auto start = identity.at("start").get<std::string>();

    if (owner.value("schema", 0) != 1 || owner.value("session_id", std::string{}) != session_id ||
        owner.value("origin", std::string{}) != "voyemu-native-supervisor" ||
        owner.value("directory", std::string{}) !=
                std::filesystem::absolute(directory).lexically_normal().string() ||
        owner.value("creator_pid", std::int64_t{0}) != os::current_process() ||
        processes.value("phase", std::string{}) != "recorded" ||
        processes.value("generation", std::uint64_t{0}) != generation ||
        pid_of(processes, "sdk_pid") != pid ||
        processes.value("sdk_start", std::string{}) != start ||
        !processes.contains("sdk_identity") || pid_of(processes.at("sdk_identity"), "pid") != pid ||
        processes.at("sdk_identity").value("start", std::string{}) != start ||
        std::find(arguments.begin(), arguments.end(), "qemu.voyemu.token=" + session_id) ==
                arguments.end() ||
        sha256_bytes(Json(arguments).dump()) !=
                identity.at("arguments_sha256").get<std::string>() ||
        os::process_start_identity(pid) != std::optional<std::string>(start)) {
        throw VoyemuError("ANDROID_PROCESS_IDENTITY_UNKNOWN",
                          "authenticated SDK launch receipt differs");
    }

    const auto &previous = processes.at("sdk_identity");

    if (previous == identity) {
        return;
    }

    std::error_code launcher_error, backend_error;
    const auto launched = std::filesystem::canonical(launcher, launcher_error);
    const auto target = std::filesystem::canonical(backend, backend_error);

    if (launcher_error || backend_error || launched == target ||
        previous.value("executable", std::string{}) != launched.string() ||
        identity.at("executable") != target.string()) {
        throw VoyemuError("ANDROID_PROCESS_IDENTITY_UNKNOWN",
                          "SDK exec is not the prepared launcher-to-backend transition");
    }

    processes["sdk_identity"] = identity;
    write_private_json(path, owner);
}

void await_session_resources(const std::filesystem::path &directory, Clock::time_point deadline) {
    for (;;) {
        bool released = true;

        for (const auto &path : {directory, directory / "android.log", directory / "qnx-ram.bin"}) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);

            if (path != directory && status.type() == std::filesystem::file_type::not_found) {
                continue;
            }

            if (error || std::filesystem::is_symlink(status) ||
                (path == directory ? !std::filesystem::is_directory(status)
                                   : !std::filesystem::is_regular_file(status)) ||
                os::file_use(path) != os::FileUse::unused) {
                released = false;
                break;
            }
        }

        if (released) {
            return;
        }

        if (Clock::now() >= deadline) {
            throw VoyemuError("SESSION_STOP_RESOURCES_UNKNOWN",
                              "session cwd, Android log or QNX RAM remains held or uninspectable");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

Json stop_orphaned_session(const std::filesystem::path &directory, const std::string &session_id,
                           const std::string &endpoint) {
    const auto owner = read_private_json(directory / "session-owner.json", 16384);

    if (owner.value("schema", 0) != 1 || owner.value("session_id", std::string{}) != session_id ||
        owner.value("origin", std::string{}) != "voyemu-native-supervisor" ||
        std::filesystem::path(owner.value("directory", std::string{})) !=
                std::filesystem::absolute(directory).lexically_normal()) {
        throw VoyemuError("SESSION_OWNER", "session receipt differs");
    }

    const auto runtime_path = directory / "runtime.json";
    auto runtime =
            std::filesystem::exists(runtime_path)
                    ? read_private_json(runtime_path, 1024 * 1024)
                    : Json{{"schema", 1}, {"session_id", session_id}, {"endpoint", endpoint}};

    if (runtime.value("session_id", std::string{}) != session_id ||
        runtime.value("endpoint", std::string{}) != endpoint) {
        throw VoyemuError("SESSION_OWNER", "runtime epoch differs");
    }

    const auto host_path = directory / "host-processes.json";
    auto hosts = Json::array();

    if (std::filesystem::exists(host_path)) {
        const auto receipt = read_private_json(host_path, 65536);

        if (receipt.value("schema", 0) != 1 || receipt.at("session_id") != session_id ||
            !receipt.at("processes").is_array()) {
            throw VoyemuError("SESSION_OWNER", "host process receipt differs");
        }

        if (receipt.contains("pending_launch")) {
            throw VoyemuError("SESSION_PROCESS_PENDING_UNKNOWN",
                              "host launch registration was interrupted");
        }

        hosts = receipt.at("processes");
    }

    const auto processes = owner.value("android_processes", Json::object());

    if (processes.value("phase", std::string{}) == "launch_pending") {
        throw VoyemuError("ANDROID_STOP_IDENTITY_UNKNOWN",
                          "SDK or window launch registration was interrupted");
    }

    // Preflight every live identity before stopping any lifetime.
    for (const auto &process : hosts) {
        if (recorded_process_alive(process)) {
            verify_process(process, session_id, directory);
        }
    }

    // Registered QNX/window lifetimes can be stopped; an unregistered live receipt stays unknown.
    const auto deliveries = runtime.value("deliveries", Json::object());

    for (const auto pid : {pid_of(owner, "qnx_pid"), pid_of(runtime, "qnx_pid"),
                           pid_of(deliveries.value("frontend", Json::object()), "pid")}) {
        if (session_process_alive(pid, session_id, directory)) {
            const auto found = std::find_if(hosts.begin(), hosts.end(), [&](const auto &process) {
                return pid_of(process, "pid") == pid && recorded_process_alive(process);
            });

            if (found == hosts.end()) {
                throw VoyemuError("SESSION_STOP_UNKNOWN",
                                  "remaining host process has no launch identity");
            }
        }
    }

    for (const auto &process : hosts) {
        stop_recorded_process(process, session_id, directory);
    }

    Json sdk{{"receipt", !processes.empty()}, {"signal", nullptr}};

    if (!processes.empty()) {
        const auto pid = pid_of(processes, "sdk_pid");
        const auto start = processes.value("sdk_start", std::string{});
        const auto owned = [&] {
            const auto now = os::process_start_identity(pid);

            return now && *now == start;
        };

        // A receipt without start identity cannot tell the SDK from a later process.
        if (processes.value("phase", std::string{}) != "recorded" || start.empty()) {
            if (pid <= 1 || os::process_start_identity(pid)) {
                throw VoyemuError("ANDROID_STOP_IDENTITY_UNKNOWN",
                                  "SDK launch receipt without start identity; session not "
                                  "marked stopped");
            }
        } else if (owned()) {
            const auto arguments = os::process_arguments(pid);

            if (std::find(arguments.begin(), arguments.end(), "qemu.voyemu.token=" + session_id) ==
                arguments.end()) {
                throw VoyemuError("ANDROID_STOP_IDENTITY_UNKNOWN",
                                  "SDK process does not carry the session token; session not "
                                  "marked stopped");
            }

            for (const int signal : {SIGTERM, SIGKILL}) {
                if (!owned()) {
                    break;
                }

                if (processes.contains("sdk_identity")) {
                    const auto &identity = processes.at("sdk_identity");

                    if (pid_of(identity, "pid") != pid ||
                        identity.value("start", std::string{}) != start) {
                        throw VoyemuError("ANDROID_STOP_IDENTITY_UNKNOWN",
                                          "SDK identity receipt differs");
                    }

                    verify_process(processes.at("sdk_identity"), session_id, directory);
                } else {
                    const auto current = os::process_arguments(pid);

                    if (std::find(current.begin(), current.end(),
                                  "qemu.voyemu.token=" + session_id) == current.end()) {
                        throw VoyemuError("ANDROID_STOP_IDENTITY_UNKNOWN",
                                          "SDK session token changed");
                    }
                }

                sdk["signal"] = signal == SIGTERM ? "SIGTERM" : "SIGKILL";
                ::kill(static_cast<pid_t>(pid), signal);
                const auto deadline = Clock::now() + std::chrono::seconds(10);

                while (owned() && Clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }

            if (owned()) {
                throw VoyemuError("ANDROID_STOP_TIMEOUT_UNKNOWN",
                                  "owned SDK process outlived termination; session not marked "
                                  "stopped");
            }
        }

        sdk["pid"] = pid;
        sdk["start"] = start;
        sdk["terminal"] = true;
    }

    await_session_resources(directory, Clock::now() + std::chrono::seconds(10));

    if (processes.contains("console_port") && processes.contains("adbd_port")) {
        const bool released = tcp_port_free(processes.at("console_port").get<unsigned>()) &&
                              tcp_port_free(processes.at("adbd_port").get<unsigned>());
        sdk["ports_released"] = released;

        if (!released) {
            throw VoyemuError("ANDROID_STOP_PORT_UNKNOWN",
                              "recorded Android console/adbd port release could not be proven");
        }
    }

    retire_endpoint(directory, session_id, endpoint);

    auto terminal = runtime;
    auto &terminal_deliveries = terminal["deliveries"];

    if (terminal_deliveries.contains("qnx")) {
        auto &qnx = terminal_deliveries["qnx"];
        const auto status = qnx.value("status", std::string{});
        const bool launched = pid_of(owner, "qnx_pid") > 1 || pid_of(runtime, "qnx_pid") > 1 ||
                              pid_of(qnx, "pid") > 1;

        if (launched && (status == "booting" || status == "graphics_running")) {
            qnx["status"] = "host_terminal";
        }
    }

    if (terminal_deliveries.contains("frontend")) {
        auto &frontend = terminal_deliveries["frontend"];
        const bool launched = pid_of(frontend, "pid") > 1 || pid_of(processes, "frontend_pid") > 1;

        if (launched && frontend.value("status", std::string{}) == "started") {
            frontend["status"] = "host_terminal";
        }
    }

    if (runtime.value("stopped", false)) {
        if (terminal != runtime) {
            write_private_json(runtime_path, terminal);
        }

        return terminal;
    }

    terminal["stopped"] = true;
    auto &android = terminal["deliveries"]["android"];
    android["status"] = "host_terminal";
    android["powered"] = false;
    android["shutdown"] = {{"mode", "host_recovery"},
                           {"wrapper_terminal", true},
                           {"guest_shutdown_unverified", true}};
    terminal["host_recovery"] = {{"schema", 1},
                                 {"session_id", session_id},
                                 {"supervisor", "exited"},
                                 {"sdk", sdk},
                                 {"guest_shutdown_unverified", true}};
    write_private_json(runtime_path, terminal);

    return terminal;
}
} // namespace voyemu
