#include "model.hpp"
#include "session_test_host.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
using namespace voyemu;

namespace {
int tcp_qmp_fixture(const char *log_path) {
    WSADATA startup{};

    assert(WSAStartup(MAKEWORD(2, 2), &startup) == 0);
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    int extent = sizeof address;
    assert(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &extent) == 0 &&
           ::listen(listener, 4) == 0);
    {
        std::ofstream ready(std::string(log_path) + ".ready");
        ready << ntohs(address.sin_port);
    }
    bool running = true;

    for (;;) {
        const SOCKET fd = ::accept(listener, nullptr, nullptr);
        assert(fd != INVALID_SOCKET);
        auto emit = [&](const Json &value) {
            const auto text = value.dump() + "\n";

            return ::send(fd, text.data(), static_cast<int>(text.size()), 0) ==
                   static_cast<int>(text.size());
        };

        if (!emit({{"QMP", Json::object()}})) {
            closesocket(fd);
            continue;
        }

        std::string line;
        char byte;

        while (::recv(fd, &byte, 1, 0) == 1) {
            if (byte != '\n') {
                line += byte;
                continue;
            }

            const auto request = Json::parse(line);
            line.clear();
            const auto command = request.at("execute").get<std::string>();
            {
                std::ofstream log(log_path, std::ios::app);
                log << command << '\n';
            }
            Json result = Json::object();

            if (command == "stop") {
                running = false;
            } else if (command == "cont") {
                running = true;
            } else if (command == "query-status") {
                result = {{"running", std::filesystem::exists(std::string(log_path) + ".badstate")
                                              ? !running
                                              : running}};
            } else {
                assert(command == "qmp_capabilities");
            }

            if (command == "stop" || command == "cont") {
                if (!emit({{"event", "STOP"}})) {
                    break;
                }
            }

            Json reply{{"return", result}};

            if (request.contains("id")) {
                reply["id"] = request.at("id");
            }

            if (!emit(reply)) {
                break;
            }
        }

        closesocket(fd);
    }
}
} // namespace

std::optional<int> persistence_qmp_fixture(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--qnx-persistence-win-qmp-fixture") {
        return tcp_qmp_fixture(argv[2]);
    }

    return std::nullopt;
}

void with_failing_disk_flushes(std::intptr_t, const std::function<void()> &) {
    throw std::logic_error("a durable disk handle cannot be replaced on Windows");
}

bool process_gone(std::int64_t pid) {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));

    if (!process) {
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }

    const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    CloseHandle(process);

    return exited;
}

// Files created under an elevated Windows Server token are owned by Administrators; reproduce that
// default owner whenever the token allows it. The previous default owner is returned for restoring,
// or nothing when the token, such as under Wine, keeps its default owner.
std::vector<std::byte> elevated_default_owner() {
    HANDLE token = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &token)) {
        throw std::runtime_error("process token unavailable");
    }

    DWORD size = 0;
    GetTokenInformation(token, TokenOwner, nullptr, 0, &size);
    std::vector<std::byte> previous(size);
    BYTE administrators[SECURITY_MAX_SID_SIZE]{};
    DWORD administrators_size = sizeof administrators;
    TOKEN_OWNER owner{administrators};
    const bool loaded = GetTokenInformation(token, TokenOwner, previous.data(), size, &size) != 0 &&
                        CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators,
                                           &administrators_size) != 0;

    const bool set = loaded && SetTokenInformation(token, TokenOwner, &owner, sizeof owner) != 0;
    CloseHandle(token);

    if (!loaded) {
        throw std::runtime_error("default owner unavailable");
    }

    if (!set) {
        previous.clear();
    }

    // Returned without a copy: the owner pointer of TOKEN_OWNER refers into this buffer.
    return previous;
}

void restore_default_owner(std::vector<std::byte> &previous) {
    if (previous.empty()) {
        return;
    }

    HANDLE token = nullptr;
    const bool restored = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT, &token) !=
                                  0 &&
                          SetTokenInformation(token, TokenOwner, previous.data(),
                                              static_cast<DWORD>(previous.size())) != 0;

    if (token) {
        CloseHandle(token);
    }

    if (!restored) {
        throw std::runtime_error("default owner not restored");
    }
}

void project_socket_root_guard() {}

void control_server_idle_clients(const std::filesystem::path &) {}

void control_server_early_busy_reply() {}
