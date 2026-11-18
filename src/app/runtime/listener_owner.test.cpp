#include "process.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <fstream>
#include <iostream>
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace voyemu;
namespace {
volatile std::sig_atomic_t stopped = 0;

void stop(int) {
    stopped = 1;
}
} // namespace

int main(int argc, char **argv) try {
    if (argc == 2 && std::string(argv[1]) == "--mapped-listener") {
        int fd = ::socket(AF_INET6, SOCK_STREAM, 0), disabled = 0;

        if (fd < 0 || ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &disabled, sizeof disabled)) {
            return 2;
        }

        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;

        if (::inet_pton(AF_INET6, "::ffff:127.0.0.1", &address.sin6_addr) != 1 ||
            ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) || ::listen(fd, 4)) {
            return 3;
        }

        socklen_t size = sizeof address;

        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size)) {
            return 4;
        }

        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);
        std::cout << ntohs(address.sin6_port) << std::endl;

        while (!stopped) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        ::close(fd);

        return 0;
    }

    auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("voyemu-mapped-listener-" + random_token());
    std::filesystem::create_directory(root);
    ProcessSpec spec;
    spec.executable = std::filesystem::canonical(argv[0]);
    spec.arguments = {"--mapped-listener"};
    spec.output = root / "port.txt";
    auto child = ChildProcess::launch(spec);
    unsigned port = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);

    while (!port && child.alive() && std::chrono::steady_clock::now() < deadline) {
        std::ifstream(spec.output) >> port;

        if (!port) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    const bool owned = port && child.owns_tcp_listener(port);
    ChildProcess foreign;
    const bool rejected = !foreign.owns_tcp_listener(port);

    if (!owned) {
        int bytes = proc_pidinfo(child.pid(), PROC_PIDLISTFDS, 0, nullptr, 0);
        std::cerr << "diagnostic pid=" << child.pid() << " port=" << port << " fd_bytes=" << bytes
                  << "\n";

        if (bytes > 0) {
            std::vector<proc_fdinfo> rows(bytes / sizeof(proc_fdinfo) + 32);
            int actual = proc_pidinfo(child.pid(), PROC_PIDLISTFDS, 0, rows.data(),
                                      rows.size() * sizeof(proc_fdinfo));

            for (int i = 0; i < actual / int(sizeof(proc_fdinfo)); ++i) {
                if (rows[i].proc_fdtype == PROX_FDTYPE_SOCKET) {
                    socket_fdinfo detail{};
                    int n = proc_pidfdinfo(child.pid(), rows[i].proc_fd, PROC_PIDFDSOCKETINFO,
                                           &detail, sizeof detail);
                    const auto &tcp = detail.psi.soi_proto.pri_tcp;
                    char v4[INET_ADDRSTRLEN]{}, v6[INET6_ADDRSTRLEN]{};
                    inet_ntop(AF_INET, &tcp.tcpsi_ini.insi_laddr.ina_46.i46a_addr4, v4, sizeof v4);
                    inet_ntop(AF_INET6, &tcp.tcpsi_ini.insi_laddr.ina_6, v6, sizeof v6);
                    std::cerr << "socket bytes=" << n << " family=" << detail.psi.soi_family
                              << " kind=" << detail.psi.soi_kind << " state=" << tcp.tcpsi_state
                              << " vflag=" << unsigned(tcp.tcpsi_ini.insi_vflag)
                              << " port=" << ntohs(std::uint16_t(tcp.tcpsi_ini.insi_lport))
                              << " v4=" << v4 << " v6=" << v6 << "\n";
                }
            }
        }
    }

    child.terminate(500);

    if (!owned || !rejected) {
        std::cerr
                << "owned IPv4-mapped loopback listener was not recognized, or ownership guard weakened\n";
        std::filesystem::remove_all(root);

        return 1;
    }

    std::filesystem::remove_all(root);
    std::cout
            << "retained child owns its IPv4-mapped IPv6 loopback listener; foreign handle rejected\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
} catch (...) {
    std::cerr << "unexpected non-standard exception\n";

    return 1;
}
