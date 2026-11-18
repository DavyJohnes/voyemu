#include "error.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <crt_externs.h>
#include <cstdint>
#include <cstring>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <stdexcept>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>

namespace voyemu::os {
std::int64_t current_process() {
    return ::getpid();
}

std::filesystem::path executable_path() {
    std::uint32_t size = 0;

    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);

    if (!size || _NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw VoyemuError("PROCESS_PATH", "cannot resolve own executable");
    }

    return std::filesystem::canonical(buffer.data());
}

FileUse file_use(const std::filesystem::path &path) {
    std::array<pid_t, 1024> holders{};
    const int bytes = ::proc_listpidspath(PROC_ALL_PIDS, 0, path.c_str(), 0, holders.data(),
                                          sizeof holders);

    if (bytes < 0 || static_cast<std::size_t>(bytes) >= sizeof holders ||
        bytes % sizeof(pid_t) != 0) {
        return FileUse::unknown;
    }

    for (int i = 0; i < bytes / static_cast<int>(sizeof(pid_t)); ++i) {
        if (holders[i] != 0) {
            return FileUse::held;
        }
    }

    return FileUse::unused;
}

char **environment() {
    return *_NSGetEnviron();
}

bool process_exists(std::int64_t pid) {
    proc_bsdinfo info{};

    return pid > 1 && pid <= 0x7fffffffLL &&
           proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof info) ==
                   static_cast<int>(sizeof info);
}

std::optional<std::int64_t> parent_process(std::int64_t pid) {
    proc_bsdinfo info{};

    if (pid <= 0 || pid > 0x7fffffffLL ||
        proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof info) !=
                static_cast<int>(sizeof info)) {
        return std::nullopt;
    }

    return info.pbi_ppid;
}

std::optional<std::string> process_start_identity(std::int64_t pid) {
    proc_bsdinfo info{};

    if (pid <= 1 || pid > 0x7fffffffLL ||
        proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof info) !=
                static_cast<int>(sizeof info) ||
        info.pbi_status == SZOMB) {
        return std::nullopt;
    }

    return std::to_string(info.pbi_start_tvsec) + "." + std::to_string(info.pbi_start_tvusec);
}

std::filesystem::path process_executable(std::int64_t pid) {
    std::array<char, PROC_PIDPATHINFO_MAXSIZE> path{};

    if (pid <= 1 || pid > 0x7fffffffLL ||
        ::proc_pidpath(static_cast<int>(pid), path.data(), path.size()) <= 0) {
        return {};
    }

    return path.data();
}

std::vector<std::string> process_arguments(std::int64_t pid) {
    std::vector<std::string> arguments;
    int request[3] = {CTL_KERN, KERN_PROCARGS2, static_cast<int>(pid)};
    std::size_t size = 0;

    if (pid <= 1 || pid > 0x7fffffffLL || ::sysctl(request, 3, nullptr, &size, nullptr, 0) != 0 ||
        size < sizeof(int) || size > 4 * 1024 * 1024) {
        return arguments;
    }

    std::vector<char> buffer(size);

    if (::sysctl(request, 3, buffer.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) {
        return arguments;
    }

    // KERN_PROCARGS2: argc, the executable path, NUL padding, then argc NUL-terminated arguments.
    int count = 0;
    std::memcpy(&count, buffer.data(), sizeof count);
    std::size_t at = sizeof count;

    while (at < size && buffer[at] != '\0') {
        ++at;
    }

    while (at < size && buffer[at] == '\0') {
        ++at;
    }

    while (count-- > 0 && at < size) {
        const auto end = std::find(buffer.begin() + static_cast<std::ptrdiff_t>(at),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(size), '\0');
        arguments.emplace_back(buffer.begin() + static_cast<std::ptrdiff_t>(at), end);
        at = static_cast<std::size_t>(end - buffer.begin()) + 1;
    }

    return arguments;
}

std::vector<TcpSocket> process_tcp_sockets(std::int64_t pid) {
    std::vector<TcpSocket> sockets;

    if (pid <= 0 || pid > 0x7fffffffLL) {
        return sockets;
    }

    const int id = static_cast<int>(pid);
    const int bytes = proc_pidinfo(id, PROC_PIDLISTFDS, 0, nullptr, 0);

    if (bytes <= 0 || bytes > 1024 * 1024) {
        return sockets;
    }

    std::vector<proc_fdinfo> descriptors(static_cast<std::size_t>(bytes) / sizeof(proc_fdinfo) +
                                         32);
    const int actual = proc_pidinfo(id, PROC_PIDLISTFDS, 0, descriptors.data(),
                                    static_cast<int>(descriptors.size() * sizeof(proc_fdinfo)));

    if (actual <= 0 ||
        static_cast<std::size_t>(actual) > descriptors.size() * sizeof(proc_fdinfo)) {
        return sockets;
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(actual) / sizeof(proc_fdinfo); ++i) {
        const auto &fd = descriptors[i];

        if (fd.proc_fdtype != PROX_FDTYPE_SOCKET) {
            continue;
        }

        socket_fdinfo detail{};

        if (proc_pidfdinfo(id, fd.proc_fd, PROC_PIDFDSOCKETINFO, &detail, sizeof detail) !=
            sizeof detail) {
            continue;
        }

        const auto &socket = detail.psi;
        const auto &tcp = socket.soi_proto.pri_tcp;
        const auto &address = tcp.tcpsi_ini;

        if (socket.soi_kind != SOCKINFO_TCP || socket.soi_type != SOCK_STREAM) {
            continue;
        }

        TcpSocket item;
        item.family = socket.soi_family;
        item.state = tcp.tcpsi_state == TSI_S_LISTEN        ? TcpSocket::State::listen
                     : tcp.tcpsi_state == TSI_S_ESTABLISHED ? TcpSocket::State::established
                                                            : TcpSocket::State::other;

        for (auto *endpoint : {&item.local, &item.remote}) {
            endpoint->ipv4 = address.insi_vflag & INI_IPV4;
            endpoint->ipv6 = address.insi_vflag & INI_IPV6;
        }

        item.local.address4 = address.insi_laddr.ina_46.i46a_addr4.s_addr;
        item.local.address6 = address.insi_laddr.ina_6;
        item.local.port = ntohs(static_cast<std::uint16_t>(address.insi_lport));
        item.remote.address4 = address.insi_faddr.ina_46.i46a_addr4.s_addr;
        item.remote.address6 = address.insi_faddr.ina_6;
        item.remote.port = ntohs(static_cast<std::uint16_t>(address.insi_fport));
        sockets.push_back(item);
    }

    return sockets;
}
} // namespace voyemu::os
