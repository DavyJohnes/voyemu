#include "error.hpp"
#include "process.hpp"
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <system_error>
#include <unistd.h>

extern char **environ;

namespace voyemu::os {
namespace {
std::filesystem::path proc_path(std::int64_t pid) {
    return std::filesystem::path("/proc") / std::to_string(pid);
}

bool numeric(const std::string &name) {
    return !name.empty() && name.find_first_not_of("0123456789") == std::string::npos;
}

// Parses one /proc/net/tcp{,6} address: hex words in the kernel's in-memory byte order.
bool parse_endpoint(const std::string &text, bool six, TcpEndpoint &endpoint) {
    const auto colon = text.find(':');

    if (colon != (six ? 32U : 8U) || text.size() != colon + 5) {
        return false;
    }

    endpoint.port = static_cast<std::uint16_t>(std::stoul(text.substr(colon + 1), nullptr, 16));

    if (!six) {
        endpoint.ipv4 = true;
        endpoint.address4 = static_cast<std::uint32_t>(std::stoul(text.substr(0, 8), nullptr, 16));

        return true;
    }

    for (int word = 0; word < 4; ++word) {
        const auto value = static_cast<std::uint32_t>(
                std::stoul(text.substr(static_cast<std::size_t>(word) * 8, 8), nullptr, 16));
        std::memcpy(endpoint.address6.s6_addr + static_cast<std::ptrdiff_t>(word) * 4, &value,
                    sizeof value);
    }

    endpoint.ipv6 = true;
    // Dual-stack sockets also carry IPv4 traffic: a v4-mapped or unspecified v6 address.
    constexpr unsigned char mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

    if (std::memcmp(endpoint.address6.s6_addr, mapped_prefix, sizeof mapped_prefix) == 0) {
        endpoint.ipv4 = true;
        std::memcpy(&endpoint.address4, endpoint.address6.s6_addr + 12, sizeof endpoint.address4);
    } else if (IN6_IS_ADDR_UNSPECIFIED(&endpoint.address6)) {
        endpoint.ipv4 = true;
    }

    return true;
}
} // namespace

std::int64_t current_process() {
    return ::getpid();
}

std::filesystem::path executable_path() {
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);

    if (error || !path.is_absolute()) {
        throw VoyemuError("PROCESS_PATH", "cannot resolve own executable");
    }

    return std::filesystem::canonical(path);
}

FileUse file_use(const std::filesystem::path &path) {
    struct stat target{};

    if (::stat(path.c_str(), &target) != 0) {
        return FileUse::unknown;
    }

    std::error_code error;
    std::filesystem::directory_iterator processes("/proc", error);

    if (error) {
        return FileUse::unknown;
    }

    for (const auto &process : processes) {
        if (!numeric(process.path().filename().string())) {
            continue;
        }

        struct stat cwd{};

        if (::stat((process.path() / "cwd").c_str(), &cwd) == 0 && cwd.st_dev == target.st_dev &&
            cwd.st_ino == target.st_ino) {
            return FileUse::held;
        }

        // Open descriptors, then file mappings that may outlive their descriptor.
        std::filesystem::directory_iterator descriptors(process.path() / "fd", error);

        if (!error) {
            for (const auto &descriptor : descriptors) {
                struct stat held{};

                if (::stat(descriptor.path().c_str(), &held) == 0 && held.st_dev == target.st_dev &&
                    held.st_ino == target.st_ino) {
                    return FileUse::held;
                }
            }
        }

        std::ifstream maps(process.path() / "maps");
        std::string line;

        while (std::getline(maps, line)) {
            std::istringstream fields(line);
            std::string range, mode, offset, device;
            unsigned long long inode = 0;
            unsigned major = 0, minor = 0;

            if (!(fields >> range >> mode >> offset >> device >> inode) || !inode) {
                continue;
            }

            const auto colon = device.find(':');

            if (colon == std::string::npos ||
                std::from_chars(device.data(), device.data() + colon, major, 16).ec !=
                        std::errc{} ||
                std::from_chars(device.data() + colon + 1, device.data() + device.size(), minor, 16)
                                .ec != std::errc{}) {
                continue;
            }

            if (inode == target.st_ino && major == ::major(target.st_dev) &&
                minor == ::minor(target.st_dev)) {
                return FileUse::held;
            }
        }
    }

    return FileUse::unused;
}

char **environment() {
    return environ;
}

bool process_exists(std::int64_t pid) {
    std::error_code error;

    return pid > 1 && pid <= 0x7fffffffLL &&
           std::filesystem::exists(proc_path(pid) / "stat", error);
}

std::optional<std::int64_t> parent_process(std::int64_t pid) {
    if (pid <= 0 || pid > 0x7fffffffLL) {
        return std::nullopt;
    }

    std::ifstream input(proc_path(pid) / "stat");
    std::string stat;

    if (!std::getline(input, stat)) {
        return std::nullopt;
    }

    // The command name may contain spaces and parentheses; fields resume after the last ')'.
    const auto close = stat.rfind(')');
    std::istringstream fields(close == std::string::npos ? std::string{} : stat.substr(close + 1));
    std::string state;
    std::int64_t parent = 0;

    if (!(fields >> state >> parent)) {
        return std::nullopt;
    }

    return parent;
}

std::optional<std::string> process_start_identity(std::int64_t pid) {
    if (pid <= 1 || pid > 0x7fffffffLL) {
        return std::nullopt;
    }

    std::ifstream input(proc_path(pid) / "stat"), boot("/proc/sys/kernel/random/boot_id");
    std::string stat, boot_id;

    if (!std::getline(input, stat) || !std::getline(boot, boot_id) || boot_id.empty()) {
        return std::nullopt;
    }

    // starttime is the twentieth field after the parenthesized command name; it counts clock
    // ticks since this boot.
    const auto close = stat.rfind(')');
    std::istringstream fields(close == std::string::npos ? std::string{} : stat.substr(close + 1));
    std::string state, field;

    if (!(fields >> state) || state == "Z" || state == "X") {
        return std::nullopt;
    }

    for (int index = 4; index <= 22; ++index) {
        if (!(fields >> field)) {
            return std::nullopt;
        }
    }

    return boot_id + ":" + field;
}

std::filesystem::path process_executable(std::int64_t pid) {
    if (pid <= 1 || pid > 0x7fffffffLL) {
        return {};
    }

    std::error_code error;
    const auto path = std::filesystem::read_symlink(proc_path(pid) / "exe", error);

    return error ? std::filesystem::path{} : path;
}

std::vector<std::string> process_arguments(std::int64_t pid) {
    std::vector<std::string> arguments;

    if (pid <= 1 || pid > 0x7fffffffLL) {
        return arguments;
    }

    std::ifstream input(proc_path(pid) / "cmdline", std::ios::binary);
    std::string argument;

    while (std::getline(input, argument, '\0')) {
        arguments.push_back(argument);
    }

    return arguments;
}

std::vector<TcpSocket> process_tcp_sockets(std::int64_t pid) {
    std::vector<TcpSocket> sockets;

    if (pid <= 0 || pid > 0x7fffffffLL) {
        return sockets;
    }

    std::set<unsigned long long> inodes;
    std::error_code error;
    std::filesystem::directory_iterator descriptors(proc_path(pid) / "fd", error);

    if (error) {
        return sockets;
    }

    for (const auto &descriptor : descriptors) {
        const auto target = std::filesystem::read_symlink(descriptor.path(), error).string();

        if (!error && target.starts_with("socket:[") && target.ends_with("]")) {
            inodes.insert(std::stoull(target.substr(8, target.size() - 9)));
        }
    }

    for (const bool six : {false, true}) {
        std::ifstream table(proc_path(pid) / "net" / (six ? "tcp6" : "tcp"));
        std::string line;
        std::getline(table, line);

        while (std::getline(table, line)) {
            std::istringstream fields(line);
            std::string slot, local, remote, state, queues, timer, retransmits;
            unsigned long long uid = 0, timeout = 0, inode = 0;

            if (!(fields >> slot >> local >> remote >> state >> queues >> timer >> retransmits >>
                  uid >> timeout >> inode) ||
                !inodes.contains(inode)) {
                continue;
            }

            TcpSocket item;
            item.family = six ? AF_INET6 : AF_INET;
            item.state = state == "0A"   ? TcpSocket::State::listen
                         : state == "01" ? TcpSocket::State::established
                                         : TcpSocket::State::other;

            if (parse_endpoint(local, six, item.local) &&
                parse_endpoint(remote, six, item.remote)) {
                sockets.push_back(item);
            }
        }
    }

    return sockets;
}
} // namespace voyemu::os
