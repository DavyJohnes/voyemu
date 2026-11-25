#include "android_port_lease.hpp"
#include "error.hpp"
#include "machine_host_ports.hpp"
#include "os/process.hpp"
#include "session.hpp"
#include "session_internal.hpp"
#include "session_process_identity.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace voyemu;
using Clock = std::chrono::steady_clock;

void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class Ready> bool settles(Ready ready) {
    const auto deadline = Clock::now() + std::chrono::seconds(20);

    while (!ready()) {
        if (Clock::now() >= deadline) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return true;
}

// An SDK stand-in whose launcher exits at once, so that, like the SDK of an exited supervisor,
// it is nobody's child here. It carries the session token argument the supervisor passes.
struct Orphan {
    std::int64_t pid = 0;
    std::string start;
};

Orphan orphan(const std::filesystem::path &self, const std::filesystem::path &directory,
              const std::string &token) {
    const auto receipt = directory / ("orphan-" + random_token() + ".json");
    ProcessSpec spec;

    spec.executable = self;
    spec.arguments = {"--launcher", receipt.string(), token};
    spec.output = directory / "launcher.log";
    auto launcher = ChildProcess::launch(spec);
    check(settles([&] {
              return !launcher.alive();
          }) && launcher.exit_code() == 0,
          "orphan launcher failed");
    const auto pid = read_private_json(receipt).at("pid").get<std::int64_t>();
    const auto start = os::process_start_identity(pid);

    if (!start) {
        throw std::runtime_error("orphan start identity unavailable");
    }

    return {pid, *start};
}

bool alive(const Orphan &process) {
    return os::process_start_identity(process.pid) == process.start;
}

void reap(const Orphan &process) {
    if (alive(process)) {
        ::kill(static_cast<pid_t>(process.pid), SIGKILL);
    }
}

struct Reap {
    Orphan process;

    ~Reap() {
        reap(process);
    }
};

std::filesystem::path bind_endpoint(const std::filesystem::path &directory, const std::string &id,
                                    int *listener = nullptr) {
    const auto endpoint = session_detail::session_control_endpoint(directory, id);

    ensure_private_directory(endpoint.parent_path());
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    check(fd >= 0, "fixture socket failed");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto text = endpoint.string();
    check(text.size() < sizeof address.sun_path, "fixture socket path too long");
    std::copy(text.begin(), text.end(), address.sun_path);
    check(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
                  ::chmod(endpoint.c_str(), 0600) == 0,
          "fixture socket bind failed");
    record_session_endpoint(directory, id, endpoint);
    auto runtime = read_private_json(directory / "runtime.json");
    runtime["endpoint"] = endpoint.string();
    runtime["stopped"] = true;
    write_private_json(directory / "runtime.json", runtime);

    if (listener) {
        check(::listen(fd, 1) == 0, "fixture socket listen failed");
        *listener = fd;
    } else {
        ::close(fd);
    }

    return endpoint;
}

// A session directory as an exited supervisor leaves it: receipts, no control endpoint.
std::filesystem::path session(const std::filesystem::path &root, const std::string &id,
                              const Json &android_processes, std::int64_t qnx_pid = 0) {
    const auto directory = root / id;

    ensure_private_directory(directory);
    Json owner{{"schema", 1},
               {"session_id", id},
               {"directory", directory.string()},
               {"origin", "voyemu-native-supervisor"},
               {"creator_pid", 999999999},
               {"qnx_pid", qnx_pid}};

    if (!android_processes.is_null()) {
        owner["android_processes"] = android_processes;
    }

    write_private_json(directory / "session-owner.json", owner);
    write_private_json(directory / "runtime.json",
                       {{"schema", 1},
                        {"session_id", id},
                        {"endpoint", (directory / "control.sock").string()},
                        {"android_pid",
                         android_processes.is_object() ? android_processes.at("sdk_pid") : Json(0)},
                        {"qnx_pid", qnx_pid},
                        {"deliveries",
                         {{"android", {{"status", "booting"}}}, {"qnx", {{"status", "stopped"}}}}},
                        {"stopped", false}});

    return directory;
}

Json recorded(const Orphan &process, bool identity, unsigned console) {
    Json receipt{{"generation", 1},          {"phase", "recorded"},
                 {"sdk_pid", process.pid},   {"console_port", console},
                 {"adbd_port", console + 1}, {"grpc_port", console + 30000}};

    if (identity) {
        receipt.update(android_launch_identity(process.pid));
    }

    return receipt;
}

std::string refusal(const std::filesystem::path &directory, const std::string &id) {
    try {
        stop_orphaned_session(directory, id, (directory / "control.sock").string());
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

void launch_identity_waits_for_same_process_publication() {
    constexpr std::int64_t pid = 123;
    const std::vector<std::string> command{"/test/worker", "--hold"};
    const auto refuses = [&](const session_detail::ProcessIdentityReader &observations,
                             const std::optional<std::string> &expected_start = std::nullopt) {
        try {
            (void)session_detail::read_launch_process_identity(pid, observations, expected_start);
        } catch (const VoyemuError &error) {
            return std::string_view(error.what()).starts_with("SESSION_PROCESS_IDENTITY_UNKNOWN:");
        }

        return false;
    };
    unsigned sample = 0;
    const session_detail::ProcessIdentityReader delayed{
            [&](std::int64_t owner) -> std::optional<std::string> {
                check(owner == pid, "sampled a different process");

                return "boot:42";
            },
            [&](std::int64_t) -> std::filesystem::path {
                return ++sample == 1 ? "" : "/test/worker";
            },
            [&](std::int64_t) {
                return sample == 2 ? std::vector<std::string>{} : command;
            }};
    const auto receipt = session_detail::read_launch_process_identity(pid, delayed);

    check(receipt.at("pid") == pid && receipt.at("start") == "boot:42" &&
                  receipt.at("executable") == "/test/worker" &&
                  receipt.at("arguments_sha256") == sha256_bytes(R"(["/test/worker","--hold"])"),
          "launch did not record the first complete identity of the original process");

    auto unavailable = delayed;
    unavailable.arguments = [](std::int64_t) {
        return std::vector<std::string>{};
    };
    check(refuses(unavailable), "launch accepted a persistently incomplete identity");
    unavailable.start = [](std::int64_t) {
        return std::optional<std::string>{};
    };
    unavailable.executable = [](std::int64_t) -> std::filesystem::path {
        throw std::runtime_error("launch read the image without the expected birth");
    };
    check(refuses(unavailable), "launch accepted a missing initial birth");
    unavailable.start = delayed.start;
    check(refuses(unavailable, std::optional<std::string>{"boot:43"}),
          "launch accepted a different birth than the caller observed");

    // A fresh complete reading cannot restore ownership after this birth has disappeared.
    for (const auto &lost : {std::optional<std::string>{}, std::optional<std::string>{"boot:43"}}) {
        unsigned observations = 0;
        const session_detail::ProcessIdentityReader unstable{
                [&](std::int64_t) -> std::optional<std::string> {
                    return observations == 1 ? lost : std::optional<std::string>{"boot:42"};
                },
                [&](std::int64_t) -> std::filesystem::path {
                    return ++observations == 1 ? "" : "/test/worker";
                },
                [&](std::int64_t) {
                    return command;
                }};

        check(refuses(unstable) && observations == 1,
              "launch accepted a missing or changed birth identity");
    }
}

void exited_sdk_has_no_identity() {
    ProcessSpec specification;

    specification.executable = "/bin/sh";
    specification.arguments = {"-c", "exit 0"};
    auto sdk = ChildProcess::launch(specification);
    const auto pid = sdk.pid();

    check(settles([&] {
              return !sdk.alive();
          }) && sdk.exit_code() == 0,
          "SDK fixture did not exit");
    check(android_launch_identity(pid).empty(),
          "an SDK that exited before the first observation published an identity");
}
} // namespace

int main(int argc, char **argv) try {
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--launch-identity-only")) {
        launch_identity_waits_for_same_process_publication();
        exited_sdk_has_no_identity();

        if (argc == 2) {
            return 0;
        }
    }

    if (argc == 4 && std::string_view(argv[1]) == "--foreign-listener") {
        const auto listener = ::socket(AF_INET, SOCK_STREAM, 0);
        check(listener >= 0, "foreign listener socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(std::stoul(argv[2])));
        check(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
                      ::listen(listener, 1) == 0,
              "foreign listener bind failed");
        write_private_json(argv[3], {{"ready", true}});
        std::this_thread::sleep_for(std::chrono::seconds(30));
        ::close(listener);

        return 0;
    }

    if (argc == 3 && std::string_view(argv[1]) == "--resource-holder") {
        write_private_json(std::filesystem::current_path() / "holder-ready.json",
                           {{"ready", true}});
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[2])));

        return 0;
    }

    if (argc == 4 && std::string(argv[1]) == "--launcher") {
        ProcessSpec leaf;
        leaf.executable = std::filesystem::canonical(argv[0]);
        leaf.arguments = {"--sdk", "-avd", "orphan", "-prop",
                          std::string("qemu.voyemu.token=") + argv[3]};
        leaf.output = std::filesystem::path(argv[2]).parent_path() / "sdk.log";
        auto child = ChildProcess::launch(leaf);
        write_private_json(argv[2], {{"pid", child.pid()}});
        child.detach();

        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--sdk") {
        std::this_thread::sleep_for(std::chrono::seconds(120));

        return 0;
    }

    const auto self = std::filesystem::canonical(argv[0]);
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("session-orphan-" + random_token());
    ensure_private_directory(root);
    const auto console = select_machine_console_port(
            0,
            [](unsigned port) {
                return tcp_port_free(port);
            },
            android_port_leasable);
    const AndroidPortLease port_lease(console);
    check(port_lease.held(), "test console lease failed");
    const auto foreign_id = random_token();
    const auto foreign = orphan(self, root, foreign_id);
    const Reap foreign_cleanup{foreign};

    {
        // A short-lived SDK descendant can outlive its parent while holding the session cwd/log.
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        ProcessSpec spec;
        spec.executable = self;
        spec.arguments = {"--resource-holder", "600"};
        spec.directory = directory;
        spec.output = directory / "android.log";
        auto holder = ChildProcess::launch(spec);
        check(settles([&] {
                  return std::filesystem::exists(directory / "holder-ready.json");
              }),
              "resource holder did not start");
        check(os::file_use(directory) == os::FileUse::held, "session cwd holder was not detected");
        std::string failure;
        try {
            await_session_resources(directory, Clock::now() + std::chrono::milliseconds(40));
        } catch (const VoyemuError &error) {
            failure = error.what();
        }

        check(failure.starts_with("SESSION_STOP_RESOURCES_UNKNOWN:") && holder.alive() &&
                      !read_private_json(directory / "runtime.json").value("stopped", true),
              "held resource deadline signaled a holder or fabricated terminal state");
        const auto terminal = stop_orphaned_session(directory, id,
                                                    (directory / "control.sock").string());
        check(terminal.at("stopped") == true && !holder.alive(),
              "orphan stop completed before the SDK descendant released its session resources");
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto target = directory / "foreign-resource.txt";
        { std::ofstream(target) << "untouched"; }
        const auto before = sha256_file(target);
        std::filesystem::create_symlink(target, directory / "android.log");
        bool refused = false;
        try {
            await_session_resources(directory, Clock::now() + std::chrono::milliseconds(40));
        } catch (const VoyemuError &error) {
            refused = std::string(error.what()).starts_with("SESSION_STOP_RESOURCES_UNKNOWN:");
        }

        check(refused && std::filesystem::is_symlink(directory / "android.log") &&
                      sha256_file(target) == before &&
                      !read_private_json(directory / "runtime.json").value("stopped", true),
              "uninspectable session resource was accepted or changed");
        std::filesystem::remove(directory / "android.log");
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto foreign = root / "external-log";
        { std::ofstream(foreign) << "foreign retained bytes"; }
        const auto before = sha256_file(foreign);
        std::filesystem::create_symlink(foreign, directory / "android.log");
        std::string failure;
        try {
            await_session_resources(directory, Clock::now() + std::chrono::milliseconds(40));
        } catch (const VoyemuError &error) {
            failure = error.what();
        }

        check(failure.starts_with("SESSION_STOP_RESOURCES_UNKNOWN:") &&
                      sha256_file(foreign) == before &&
                      std::filesystem::is_symlink(directory / "android.log"),
              "unknown resource path was accepted or changed");
    }

    {
        // A listener can reoccupy the old SDK port; it never authorizes a foreign signal.
        const auto id = random_token();
        const auto sdk = orphan(self, root, id);
        const Reap sdk_cleanup{sdk};
        const auto directory = session(root, id, recorded(sdk, true, console));
        reap(sdk);
        check(settles([&] {
                  return !alive(sdk);
              }),
              "test SDK did not exit");
        const auto endpoint = bind_endpoint(directory, id);
        auto runtime = read_private_json(directory / "runtime.json");
        runtime["stopped"] = false;
        write_private_json(directory / "runtime.json", runtime);
        const auto ready = root / "foreign-listener-ready.json";
        ProcessSpec spec;
        spec.executable = self;
        spec.arguments = {"--foreign-listener", std::to_string(console), ready.string()};
        spec.directory = root;
        spec.output = root / "foreign-listener.log";
        auto listener = ChildProcess::launch(spec);
        check(settles([&] {
                  return std::filesystem::exists(ready);
              }),
              "foreign listener did not start");
        std::string failure;
        try {
            (void)stop_orphaned_session(directory, id, endpoint.string());
        } catch (const VoyemuError &error) {
            failure = error.what();
        }

        check(failure.starts_with("ANDROID_STOP_PORT_UNKNOWN:") && listener.alive() &&
                      !tcp_port_free(console) && std::filesystem::exists(endpoint) &&
                      read_private_json(directory / "runtime.json") == runtime,
              "foreign listener was stopped, ignored, or retired the previous epoch");
        listener.terminate(1000);
        check(stop_orphaned_session(directory, id, endpoint.string()).at("stopped") == true &&
                      !std::filesystem::exists(endpoint),
              "ordinary orphan stop did not recover after the foreign port was released");
    }
    {
        // The owned SDK of an exited supervisor stops through the regular stop, and a repeated
        // stop reports the same terminal epoch.
        const auto id = random_token();
        const auto sdk = orphan(self, root, id);
        const auto directory = session(root, id, recorded(sdk, true, console));
        const auto terminal = stop_orphaned_session(directory, id,
                                                    (directory / "control.sock").string());
        check(terminal.at("stopped") == true && !alive(sdk), "owned orphan SDK not stopped");
        check(terminal.at("host_recovery").at("sdk").at("signal") == "SIGTERM" &&
                      terminal.at("deliveries")
                                      .at("android")
                                      .at("shutdown")
                                      .at("guest_shutdown_unverified") == true,
              "recovery receipt incomplete");
        check(read_private_json(directory / "runtime.json") == terminal,
              "terminal epoch not published");
        check(stop_orphaned_session(directory, id, (directory / "control.sock").string()) ==
                      terminal,
              "repeated stop differs");
    }
    {
        // A recorded PID now names another process: the SDK is gone and nothing is signaled.
        const auto id = random_token();
        auto reused = recorded(foreign, false, console);
        reused["sdk_start"] = "reused-pid";
        const auto directory = session(root, id, reused);
        const auto terminal = stop_orphaned_session(directory, id,
                                                    (directory / "control.sock").string());
        check(terminal.at("stopped") == true &&
                      terminal.at("host_recovery").at("sdk").at("signal").is_null() &&
                      alive(foreign),
              "a reused PID was signaled");
    }
    {
        // PID and start match, but the process belongs to another session.
        const auto id = random_token();
        const auto directory = session(root, id, recorded(foreign, true, console));
        check(refusal(directory, id).starts_with("ANDROID_STOP_IDENTITY_UNKNOWN:") &&
                      alive(foreign) &&
                      read_private_json(directory / "runtime.json").at("stopped") == false,
              "a foreign owner was signaled or marked stopped");
    }
    {
        // A receipt without start identity cannot authorize a signal to a live PID.
        const auto id = random_token();
        const auto directory = session(root, id, recorded(foreign, false, console));
        check(refusal(directory, id).starts_with("ANDROID_STOP_IDENTITY_UNKNOWN:") &&
                      alive(foreign) &&
                      read_private_json(directory / "runtime.json").at("stopped") == false,
              "an incomplete receipt authorized a signal");
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto endpoint = bind_endpoint(directory, id);
        const auto terminal = stop_orphaned_session(directory, id, endpoint.string());
        check(terminal.at("stopped") == true && !std::filesystem::exists(endpoint),
              "owned stale control socket retained");
        check(stop_orphaned_session(directory, id, endpoint.string()) == terminal,
              "stale endpoint recovery not idempotent");
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto endpoint = bind_endpoint(directory, id);
        std::filesystem::remove(endpoint);
        {
            std::ofstream replacement(endpoint);
            replacement << "foreign";
        }
        bool refused = false;
        try {
            (void)stop_orphaned_session(directory, id, endpoint.string());
        } catch (const VoyemuError &error) {
            refused = std::string(error.what()).starts_with("SESSION_ENDPOINT_OWNER:");
        }

        check(refused && std::filesystem::is_regular_file(endpoint),
              "replaced endpoint was removed");
        std::filesystem::remove(endpoint);
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto endpoint = bind_endpoint(directory, id);
        const auto original = read_private_json(directory / "control-socket.json");
        std::filesystem::remove(endpoint);
        const auto replacement = bind_endpoint(directory, id);
        write_private_json(directory / "control-socket.json", original);
        bool refused = false;
        try {
            (void)stop_orphaned_session(directory, id, endpoint.string());
        } catch (const VoyemuError &error) {
            refused = std::string(error.what()).starts_with("SESSION_ENDPOINT_OWNER:");
        }

        check(refused && std::filesystem::exists(replacement), "foreign socket was removed");
        std::filesystem::remove(replacement);
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        int listener = -1;
        const auto endpoint = bind_endpoint(directory, id, &listener);
        bool refused = false;
        try {
            (void)stop_orphaned_session(directory, id, endpoint.string());
        } catch (const VoyemuError &error) {
            refused = std::string(error.what()).starts_with("SESSION_ENDPOINT_UNKNOWN:");
        }

        ::close(listener);
        check(refused && std::filesystem::exists(endpoint), "live control socket was removed");
        std::filesystem::remove(endpoint);
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto endpoint = bind_endpoint(directory, id);
        std::filesystem::remove(endpoint);
        std::filesystem::create_symlink(directory / "runtime.json", endpoint);
        bool refused = false;
        try {
            (void)stop_orphaned_session(directory, id, endpoint.string());
        } catch (const VoyemuError &error) {
            refused = std::string(error.what()).starts_with("SESSION_ENDPOINT_OWNER:");
        }

        check(refused && std::filesystem::is_symlink(endpoint) &&
                      read_private_json(directory / "runtime.json").at("session_id") == id,
              "symlink endpoint or its target was removed");
        std::filesystem::remove(endpoint);
    }
    {
        const auto id = random_token();
        const auto child = orphan(self, root, id);
        const Reap cleanup{child};
        const auto directory = session(root, id, Json(), child.pid);
        record_session_process(directory, id, child.pid);
        auto receipt = read_private_json(directory / "host-processes.json");
        receipt["processes"][0]["arguments_sha256"] = "foreign-command";
        write_private_json(directory / "host-processes.json", receipt);
        check(refusal(directory, id).starts_with("SESSION_STOP_IDENTITY_UNKNOWN:") && alive(child),
              "changed command authorized a host signal");
        receipt["processes"][0] = session_process_identity(child.pid);
        receipt["processes"][0]["executable"] = "/foreign/executable";
        write_private_json(directory / "host-processes.json", receipt);
        check(refusal(directory, id).starts_with("SESSION_STOP_IDENTITY_UNKNOWN:") && alive(child),
              "changed executable authorized a host signal");
        receipt["processes"][0] = session_process_identity(child.pid);
        receipt["processes"][0]["nonce"] = 17;
        write_private_json(directory / "host-processes.json", receipt);
        check(refusal(directory, id).starts_with("SESSION_STOP_IDENTITY_UNKNOWN:") && alive(child),
              "wrong graphics nonce authorized a host signal");
        receipt["processes"][0] = session_process_identity(child.pid);
        receipt["processes"][0]["start"] = "reused-pid";
        write_private_json(directory / "host-processes.json", receipt);
        check(refusal(directory, id).starts_with("SESSION_STOP_UNKNOWN:") && alive(child),
              "a reused host PID authorized a signal");
    }
    {
        // The shipped SDK launcher execs its backend without changing PID or start identity.
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        const auto gate = directory / "exec-gate";
        check(::mkfifo(gate.c_str(), 0600) == 0, "exec fixture gate failed");
        ProcessSpec spec;
        spec.executable = "/bin/sh";
        spec.arguments = {"-c",
                          "read -r ready < \"$1\"; exec \"$2\" --sdk \"$3\"",
                          "voyemu-sdk-exec",
                          gate.string(),
                          self.string(),
                          "qemu.voyemu.token=" + id};
        spec.output = directory / "exec-sdk.log";
        auto child = ChildProcess::launch(spec);
        const auto start = os::process_start_identity(child.pid()).value_or(std::string{});
        check(!start.empty(), "exec fixture start missing");
        const auto launcher = os::process_executable(child.pid());
        auto owner = read_private_json(directory / "session-owner.json");
        owner["creator_pid"] = os::current_process();
        owner["android_processes"] = recorded({child.pid(), start}, true, console);
        write_private_json(directory / "session-owner.json", owner);
        int writer = -1;
        check(settles([&] {
                  writer = ::open(gate.c_str(), O_WRONLY | O_NONBLOCK);

                  return writer >= 0;
              }),
              "exec fixture never opened its gate");
        check(::write(writer, "go\n", 3) == 3, "exec fixture gate write failed");
        ::close(writer);
        check(settles([&] {
                  return os::process_executable(child.pid()) == self &&
                         os::process_arguments(child.pid()).size() == 3;
              }),
              "SDK backend exec did not complete");
        check(os::process_start_identity(child.pid()) == start &&
                      refusal(directory, id).starts_with("SESSION_STOP_IDENTITY_UNKNOWN:") &&
                      child.alive(),
              "unregistered SDK exec was stopped or ignored");
        auto rejected = [&](const Json &receipt, const std::string &session,
                            std::uint64_t generation, const std::filesystem::path &backend) {
            write_private_json(directory / "session-owner.json", receipt);
            try {
                refresh_android_launch_identity(directory, session, child.pid(), generation,
                                                launcher, backend);
            } catch (const VoyemuError &error) {
                return std::string(error.what()).starts_with("ANDROID_PROCESS_IDENTITY_UNKNOWN:") &&
                       read_private_json(directory / "session-owner.json") == receipt &&
                       child.alive();
            }

            return false;
        };
        check(rejected(owner, id, 2, self), "foreign SDK generation was rebound");
        check(rejected(owner, id, 1, "/bin/sleep"), "foreign SDK executable was rebound");
        auto wrong = owner;
        wrong["android_processes"]["phase"] = "launch_pending";
        check(rejected(wrong, id, 1, self), "pending SDK was rebound");
        wrong = owner;
        wrong["android_processes"]["sdk_start"] = "foreign-start";
        check(rejected(wrong, id, 1, self), "foreign SDK start was rebound");
        wrong = owner;
        const auto foreign_token = random_token();
        wrong["session_id"] = foreign_token;
        check(rejected(wrong, foreign_token, 1, self), "foreign SDK token was rebound");
        write_private_json(directory / "session-owner.json", owner);
        refresh_android_launch_identity(directory, id, child.pid(), 1, launcher, self);
        const auto refreshed = read_private_json(directory / "session-owner.json");
        auto expected = owner;
        expected["android_processes"]["sdk_identity"] = session_process_identity(child.pid());
        check(refreshed == expected, "SDK exec refresh changed launch authority metadata");
        refresh_android_launch_identity(directory, id, child.pid(), 1, launcher, self);
        check(read_private_json(directory / "session-owner.json") == refreshed,
              "SDK refresh not idempotent");
        wrong = refreshed;
        wrong["android_processes"]["sdk_identity"]["arguments_sha256"] = "changed-final-command";
        check(rejected(wrong, id, 1, self), "final SDK identity was rebound a second time");
        write_private_json(directory / "session-owner.json", wrong);
        try {
            advance_android_launch_generation(directory, id, child.pid(), 1);
            throw std::runtime_error("changed SDK command advanced generation");
        } catch (const VoyemuError &error) {
            check(std::string(error.what()).starts_with("ANDROID_PROCESS_IDENTITY_UNKNOWN:") &&
                          read_private_json(directory / "session-owner.json") == wrong,
                  "changed SDK command modified receipt");
        }

        write_private_json(directory / "session-owner.json", refreshed);
        auto advanced = refreshed;

        for (const std::uint64_t generation : {1, 2}) {
            advance_android_launch_generation(directory, id, child.pid(), generation);
            advanced["android_processes"]["generation"] = generation + 1;
            check(read_private_json(directory / "session-owner.json") == advanced,
                  "guest reboot changed SDK launch authority metadata");
            refresh_android_launch_identity(directory, id, child.pid(), generation + 1, launcher,
                                            self);
            check(rejected(advanced, id, generation, self), "old SDK generation was accepted");
        }

        check(stop_orphaned_session(directory, id, (directory / "control.sock").string())
                                      .at("stopped") == true &&
                      !child.alive(),
              "registered SDK exec did not recover normally");
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        ProcessSpec spec;
        spec.executable = self;
        spec.arguments = {"--sdk", "qemu.voyemu.token=" + id};
        spec.output = directory / "registered-launch.log";
        auto child = launch_session_process(spec, directory, id);
        const auto receipt = read_private_json(directory / "host-processes.json");
        check(!receipt.contains("pending_launch") && receipt.at("processes").size() == 1 &&
                      receipt.at("processes")[0].at("pid") == child.pid(),
              "successful launch did not atomically register and clear its intent");
        check(stop_orphaned_session(directory, id, (directory / "control.sock").string())
                                      .at("stopped") == true &&
                      !child.alive(),
              "registered launch could not recover normally");
    }
    {
        const auto id = random_token();
        const auto directory = session(root, id, Json());
        ProcessSpec spec;
        spec.executable = self.string() + ".absent";
        spec.output = directory / "failed-launch.log";
        bool failed = false;
        try {
            (void)launch_session_process(spec, directory, id);
        } catch (const VoyemuError &error) {
            failed = std::string(error.what()).starts_with("PROCESS_SPAWN:");
        }

        const auto receipt = read_private_json(directory / "host-processes.json");
        check(failed && !receipt.contains("pending_launch") && receipt.at("processes").empty(),
              "proven pre-spawn failure left an unresolved launch");
    }
    {
        // Crash-boundary state: spawn succeeded but no launch identity was published yet.
        // The deferred replacement has not mapped RAM; a RAM snapshot cannot fence its future.
        for (const bool sdk_pending : {false, true}) {
            for (const bool stopped : {false, true}) {
                const auto id = random_token();
                const auto registered = orphan(self, root, id);
                const Reap registered_cleanup{registered};
                const auto unregistered = orphan(self, root, id);
                const Reap unregistered_cleanup{unregistered};
                const auto directory = session(root, id,
                                               sdk_pending ? Json{{"phase", "launch_pending"},
                                                                  {"sdk_pid", 999999999},
                                                                  {"sdk_start", "absent-old-sdk"}}
                                                           : Json(),
                                               registered.pid);
                record_session_process(directory, id, registered.pid);
                const auto endpoint = bind_endpoint(directory, id);
                auto runtime = read_private_json(directory / "runtime.json");
                runtime["stopped"] = stopped;
                write_private_json(directory / "runtime.json", runtime);

                if (!sdk_pending) {
                    auto receipt = read_private_json(directory / "host-processes.json");
                    receipt["pending_launch"] = {{"intent", random_token()}, {"nonce", 0}};
                    write_private_json(directory / "host-processes.json", receipt);
                }

                std::string failure;
                try {
                    (void)stop_orphaned_session(directory, id, endpoint.string());
                } catch (const VoyemuError &error) {
                    failure = error.what();
                }

                check(failure.starts_with(sdk_pending ? "ANDROID_STOP_IDENTITY_UNKNOWN:"
                                                      : "SESSION_PROCESS_PENDING_UNKNOWN:") &&
                              alive(registered) && alive(unregistered) &&
                              std::filesystem::exists(endpoint) &&
                              read_private_json(directory / "runtime.json") == runtime,
                      "pending launch authorized a signal, terminal publication or socket cleanup");
                std::filesystem::remove(endpoint);
            }
        }
    }
    {
        // A supervisor can die with all its owned host lifetimes still running.
        const auto id = random_token();
        const auto qnx = orphan(self, root, id);
        const auto window = orphan(self, root, id);
        const auto graphics = orphan(self, root, id);
        const auto sdk = orphan(self, root, id);
        const auto directory = session(root, id, recorded(sdk, true, console), qnx.pid);
        auto running = read_private_json(directory / "runtime.json");
        running["deliveries"]["qnx"] = {
                {"status", "graphics_running"},
                {"pid", qnx.pid},
                {"vehicle", {{"status", "guest_acknowledged"}, {"sequence", 7}}}};
        running["deliveries"]["frontend"] = {{"status", "started"}, {"pid", window.pid}};
        write_private_json(directory / "runtime.json", running);
        auto identity = [](const Orphan &process) {
            return Json{{"pid", process.pid},
                        {"start", process.start},
                        {"executable", os::process_executable(process.pid).string()},
                        {"arguments_sha256",
                         sha256_bytes(Json(os::process_arguments(process.pid)).dump())}};
        };
        write_private_json(directory / "host-processes.json",
                           {{"schema", 1},
                            {"session_id", id},
                            {"processes",
                             Json::array({identity(qnx), identity(window), identity(graphics)})}});
        try {
            const auto terminal = stop_orphaned_session(directory, id,
                                                        (directory / "control.sock").string());
            check(terminal.at("stopped") == true && !alive(qnx) && !alive(window) &&
                          !alive(graphics) && !alive(sdk),
                  "owned host lifetimes survived orphan stop");
            auto expected_qnx = running.at("deliveries").at("qnx");
            auto expected_frontend = running.at("deliveries").at("frontend");
            expected_qnx["status"] = "host_terminal";
            expected_frontend["status"] = "host_terminal";
            check(terminal.at("deliveries").at("qnx") == expected_qnx &&
                          terminal.at("deliveries").at("frontend") == expected_frontend &&
                          terminal.at("host_recovery").at("guest_shutdown_unverified") == true &&
                          read_private_json(directory / "runtime.json") == terminal,
                  "orphan terminal retained running labels or changed historical PID/guest ACK");
            auto old_terminal = terminal;
            old_terminal["deliveries"]["qnx"]["status"] = "booting";
            old_terminal["deliveries"]["frontend"]["status"] = "started";
            write_private_json(directory / "runtime.json", old_terminal);
            check(stop_orphaned_session(directory, id, (directory / "control.sock").string()) ==
                                  terminal &&
                          stop_orphaned_session(directory, id,
                                                (directory / "control.sock").string()) == terminal,
                  "repeated ordinary stop did not repair terminal delivery truth idempotently");
        } catch (...) {
            for (const auto &process : {qnx, window, graphics, sdk}) {
                reap(process);
            }

            throw;
        }
    }
    {
        // Inactive/failed observations remain historical evidence, not successful guest shutdown.
        for (const auto *status : {"not_started", "off", "graphics_worker_exited"}) {
            const auto id = random_token();
            const auto directory = session(root, id, Json(), 999999999);
            auto runtime = read_private_json(directory / "runtime.json");
            runtime["deliveries"]["qnx"] = {{"status", status},
                                            {"pid", 999999999},
                                            {"last_error", "retained fixture failure"}};
            runtime["deliveries"]["frontend"] = {{"status", "startup_failed"}, {"pid", 999999999}};
            write_private_json(directory / "runtime.json", runtime);
            const auto terminal = stop_orphaned_session(directory, id,
                                                        (directory / "control.sock").string());
            check(terminal.at("deliveries").at("qnx") == runtime.at("deliveries").at("qnx") &&
                          terminal.at("deliveries").at("frontend") ==
                                  runtime.at("deliveries").at("frontend"),
                  "failed or unstarted delivery was converted to successful guest shutdown");
        }

        const auto id = random_token();
        const auto directory = session(root, id, Json());
        auto runtime = read_private_json(directory / "runtime.json");
        runtime["deliveries"].erase("qnx");
        write_private_json(directory / "runtime.json", runtime);
        const auto terminal = stop_orphaned_session(directory, id,
                                                    (directory / "control.sock").string());
        check(!terminal.at("deliveries").contains("qnx") &&
                      !terminal.at("deliveries").contains("frontend"),
              "Android-only recovery invented another component");
    }
    {
        // A remaining QNX lifetime of the session keeps it running and unknown.
        const auto id = random_token();
        const auto qnx = orphan(self, root, id);
        const auto directory = session(root, id, Json(), qnx.pid);
        check(refusal(directory, id).starts_with("SESSION_STOP_UNKNOWN:") && alive(qnx) &&
                      read_private_json(directory / "runtime.json").at("stopped") == false,
              "a remaining QNX process was stopped or ignored");
        reap(qnx);
    }

    reap(foreign);
    std::filesystem::remove_all(root);
    std::cout << "session orphan stop PASS\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
