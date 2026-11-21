#include "android_console.hpp"
#include <winsock2.h>
#include <windows.h>
namespace voyemu::android_console_detail {
namespace {
using Handle = SOCKET;
using Length = int;
constexpr std::intptr_t invalid = -1;

void close(Handle fd) {
    closesocket(fd);
}

bool interrupted() {
    return WSAGetLastError() == WSAEINTR;
}

bool would_block() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

sockaddr_in loopback(unsigned port) {
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));

    return address;
}
} // namespace

Socket::Socket() {
    WSADATA data{};

    if (WSAStartup(MAKEWORD(2, 2), &data)) {
        throw Disconnected{};
    }

    fd_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    u_long nonblocking = 1;
    const bool configured = fd_ != invalid && ioctlsocket(fd_, FIONBIO, &nonblocking) == 0;

    if (!configured) {
        if (fd_ != invalid) {
            close(fd_);
        }

        WSACleanup();
        throw Disconnected{};
    }
}

Socket::~Socket() {
    close(fd_);
    WSACleanup();
}

unsigned Socket::bind_inactive() {
    sockaddr_in address = loopback(0);

    int exclusive = 1;

    if (setsockopt(fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                   sizeof(exclusive))) {
        throw Disconnected{};
    }

    Length size = sizeof(address);

    if (::bind(fd_, reinterpret_cast<const sockaddr *>(&address), size) ||
        getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size)) {
        throw Disconnected{};
    }

    return ntohs(address.sin_port);
}

void Socket::connect(unsigned port, Clock::time_point deadline) {
    const sockaddr_in address = loopback(port);

    if (!::connect(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address))) {
        return;
    }

    if (!would_block()) {
        throw Disconnected{};
    }

    ready(true, deadline);
    int error = 0;
    Length size = sizeof(error);

    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) || error) {
        throw Disconnected{};
    }
}

void Socket::send(std::string_view text, Clock::time_point deadline) {
    while (!text.empty()) {
        ready(true, deadline);
        constexpr int flags =
#ifdef MSG_NOSIGNAL
                MSG_NOSIGNAL;
#else
                0;
#endif
        const auto count = ::send(fd_, text.data(), static_cast<int>(text.size()), flags);

        if (count < 0 && (interrupted() || would_block())) {
            continue;
        }

        if (count <= 0) {
            throw Disconnected{};
        }

        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

std::string Socket::receive(Clock::time_point deadline) {
    std::array<char, 4096> bytes{};

    for (;;) {
        ready(false, deadline);
        const auto count = ::recv(fd_, bytes.data(), static_cast<int>(bytes.size()), 0);

        if (count < 0 && (interrupted() || would_block())) {
            continue;
        }

        if (count <= 0) {
            throw Disconnected{};
        }

        return {bytes.data(), static_cast<std::size_t>(count)};
    }
}

void Socket::ready(bool writing, Clock::time_point deadline) {
    for (;;) {
        const auto left =
                std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now())
                        .count();

        if (left <= 0) {
            throw Timeout{};
        }

        fd_set selected, failed;
        FD_ZERO(&selected);
        FD_SET(static_cast<Handle>(fd_), &selected);
        FD_ZERO(&failed);
        FD_SET(static_cast<Handle>(fd_), &failed);
        timeval budget{static_cast<long>(left / 1000000), static_cast<long>(left % 1000000)};
        const int result = ::select(0, writing ? nullptr : &selected, writing ? &selected : nullptr,
                                    &failed, &budget);

        if (result > 0 && !FD_ISSET(fd_, &failed)) {
            return;
        }

        if (result < 0 && interrupted()) {
            continue;
        }

        if (result == 0) {
            throw Timeout{};
        }

        throw Disconnected{};
    }
}

std::string token(const std::filesystem::path &home) {
    const auto path = home / ".emulator_console_auth_token";
    std::array<char, 257> data{};

    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw Disconnected{};
    }

    struct Close {
        HANDLE file;

        ~Close() {
            CloseHandle(file);
        }
    } close{file};
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD bytes = 0;

    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &bytes, nullptr)) {
        throw Disconnected{};
    }

    if (bytes == 0 || static_cast<std::size_t>(bytes) >= data.size()) {
        throw Disconnected{};
    }

    std::string result(data.data(), static_cast<std::size_t>(bytes));

    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }

    if (result.empty() || !std::all_of(result.begin(), result.end(), [](unsigned char ch) {
            return ch > 32 && ch < 127;
        })) {
        throw Disconnected{};
    }

    return result;
}
} // namespace voyemu::android_console_detail
