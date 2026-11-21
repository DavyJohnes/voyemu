#include "android_port_lease.hpp"
#include "machine_host_ports.hpp"
#include "model.hpp"
#include "os/platform.hpp"
#include "os/process.hpp"
#include "os/socket.hpp"
#include "process.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace voyemu;
using Clock = std::chrono::steady_clock;

void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class Ready> void await(Ready ready, const char *message) {
    const auto deadline = Clock::now() + std::chrono::seconds(30);

    while (!ready()) {
        check(Clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

unsigned select_free(unsigned preferred) {
    return select_machine_console_port(
            preferred,
            [](unsigned port) {
                return tcp_port_free(port);
            },
            android_port_leasable);
}

std::size_t count_suffix(const std::filesystem::path &directory, const std::string &suffix) {
    std::size_t count = 0;

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        count += entry.path().filename().string().ends_with(suffix);
    }

    return count;
}

// One claimant models a machine start: it selects like the CLI binding, then leases like the
// supervisor before its SDK may bind, and selects again when another start leased first. The
// barriers make every claimant select before any of them leases.
int claim(const std::filesystem::path &directory, const std::string &name, unsigned preferred,
          std::size_t claimants) {
    await(
            [&] {
                return std::filesystem::exists(directory / "go");
            },
            "claimant barrier not released");
    // Port probes bind for an instant, so concurrent probes may see each other; selections take
    // turns and none leases before all have selected.
    await(
            [&] {
                return name != "second" || std::filesystem::exists(directory / "first.selected");
            },
            "first selection missing");
    auto console = select_free(preferred);
    write_private_json(directory / (name + ".selected"), {{"console", console}});
    await(
            [&] {
                return count_suffix(directory, ".selected") == claimants;
            },
            "competing selection missing");
    std::optional<AndroidPortLease> lease(std::in_place, console);

    while (!lease->held()) {
        lease.reset();
        console = select_free(preferred);
        lease.emplace(console);
    }

    write_private_json(directory / (name + ".console"), {{"console", console}});
    await(
            [&] {
                return std::filesystem::exists(directory / "release");
            },
            "claimant release missing");

    return 0;
}

ChildProcess claimant(const std::filesystem::path &self, const std::filesystem::path &directory,
                      const std::string &name, unsigned preferred, std::size_t claimants) {
    ProcessSpec spec;

    spec.executable = self;
    spec.arguments = {"--claim", directory.string(), name, std::to_string(preferred),
                      std::to_string(claimants)};
    spec.output = directory / (name + ".log");

    return ChildProcess::launch(spec);
}

unsigned console_of(const std::filesystem::path &directory, const std::string &name) {
    await(
            [&] {
                return std::filesystem::exists(directory / (name + ".console"));
            },
            "claimant lease missing");

    return read_private_json(directory / (name + ".console")).at("console").get<unsigned>();
}

void finish(const std::filesystem::path &directory, std::vector<ChildProcess> &children) {
    write_private_json(directory / "release", Json::object());

    for (auto &child : children) {
        await(
                [&] {
                    return !child.alive();
                },
                "claimant did not exit");
        check(child.exit_code() == 0, "claimant failed");
    }
}

// A listener of a process that is no Voyemu session: it holds the port without a lease.
class ForeignListener {
public:
    explicit ForeignListener(unsigned port) {
        socket_ = os::open_stream_socket(AF_INET);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        check(os::bind_socket(socket_, reinterpret_cast<sockaddr *>(&address), sizeof address) &&
                      os::listen_socket(socket_, 1),
              "foreign listener unavailable");
    }

    ~ForeignListener() {
        os::close_socket(socket_);
    }

    ForeignListener(const ForeignListener &) = delete;

private:
    os::SocketHandle socket_;
};

std::filesystem::path scenario(const std::filesystem::path &root, const char *name) {
    const auto directory = root / name;

    ensure_private_directory(directory);

    return directory;
}
} // namespace

int main(int argc, char **argv) try {
    if (argc == 6 && std::string(argv[1]) == "--claim") {
        return claim(argv[2], argv[3], static_cast<unsigned>(std::stoul(argv[4])),
                     std::stoul(argv[5]));
    }

    const os::SocketLibrary sockets;
    check(!sockets.error(), "Winsock unavailable");
    const auto self = std::filesystem::canonical(argv[0]);
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("android-port-lease-" + random_token());
    ensure_private_directory(root);
    // Claimants inherit a private lease directory, isolated from live sessions of this user.
    check(os::host_windows ? os::set_environment_variable("TMP", root.string()) &&
                                     os::set_environment_variable("TEMP", root.string())
                           : os::set_environment_variable("TMPDIR", root.string()),
          "private lease directory unavailable");
    const auto preferred = select_free(5554);

    {
        // Two starts select the same free tuple at the same moment; exactly one keeps it.
        const auto directory = scenario(root, "race");
        std::vector<ChildProcess> children;
        children.push_back(claimant(self, directory, "first", preferred, 2));
        children.push_back(claimant(self, directory, "second", preferred, 2));
        write_private_json(directory / "go", Json::object());
        const auto first = console_of(directory, "first"), second = console_of(directory, "second");
        check(read_private_json(directory / "first.selected").at("console") == preferred &&
                      read_private_json(directory / "second.selected").at("console") == preferred,
              "claimants did not compete for one tuple");
        check(first != second, "competing starts received the same Android console tuple");
        check(std::set<unsigned>{first, second}.contains(preferred) &&
                      valid_machine_console_port(first) && valid_machine_console_port(second),
              "competing starts received invalid tuples");
        check(!android_port_leasable(first) && !android_port_leasable(second),
              "a held tuple remained leasable");
        finish(directory, children);
        check(android_port_leasable(first) && android_port_leasable(second),
              "exited claimants retained their leases");
    }
    {
        // A foreign listener keeps its port; the start moves to another tuple.
        const auto directory = scenario(root, "foreign");
        ForeignListener foreign(preferred + 1);
        std::vector<ChildProcess> children;
        children.push_back(claimant(self, directory, "only", preferred, 1));
        write_private_json(directory / "go", Json::object());
        const auto console = console_of(directory, "only");
        check(console != preferred && !tcp_port_free(preferred + 1),
              "a start claimed a port of a foreign listener");
        finish(directory, children);
    }
    {
        // The death of a holder releases its lease for the next start.
        const auto directory = scenario(root, "death");
        std::vector<ChildProcess> children;
        children.push_back(claimant(self, directory, "holder", preferred, 1));
        write_private_json(directory / "go", Json::object());
        check(console_of(directory, "holder") == preferred && !android_port_leasable(preferred),
              "holder lease missing");
        children.front().terminate(1000);
        check(!children.front().alive() && android_port_leasable(preferred),
              "a dead holder retained its lease");
    }
    {
        // A failed start releases its lease with the owning object.
        std::optional<AndroidPortLease> lease(std::in_place, preferred);
        check(lease->held() && lease->console() == preferred && !android_port_leasable(preferred),
              "own lease missing");
        lease.reset();
        check(android_port_leasable(preferred), "a released lease remained held");
    }

    std::filesystem::remove_all(root);
    std::cout << "android port lease PASS\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
