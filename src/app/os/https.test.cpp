#include "https.hpp"
#include "interruption.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace voyemu;

int main() try {
#ifdef _WIN32
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int size = sizeof(sockaddr_in);
#else
    const auto listener = ::socket(AF_INET, SOCK_STREAM, 0);
    socklen_t size = sizeof(sockaddr_in);
#endif
    // The kernel accepts the connection into the backlog and nothing ever answers its TLS hello:
    // a host that stalls before the first byte.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
        ::listen(listener, 8) != 0 ||
        ::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
        std::cerr << "local listener unavailable\n";

        return 1;
    }

    const auto url = "https://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/stalled";
    std::thread interrupt([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        interruption::requested = true;
    });
    const auto started = std::chrono::steady_clock::now();
    std::string failure;

    try {
        os::https_get(url, {}, [](std::string_view) {});
    } catch (const std::exception &error) {
        failure = error.what();
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    interrupt.join();
#ifdef _WIN32
    closesocket(listener);
#else
    ::close(listener);
#endif

    // A host without the system HTTPS library has no transfer to interrupt.
    if (failure.starts_with("DOWNLOAD_LIBRARY:")) {
        std::cout << "HTTPS library unavailable; interruption not checked\n";

        return 77;
    }

    // Ctrl+C stops a stalled download within about a second on every platform, long before the
    // transfer timeouts.
    if (!failure.starts_with("INTERRUPTED:") || elapsed > std::chrono::seconds(5)) {
        std::cerr << "stalled HTTPS transfer not interrupted promptly: " << failure << " after "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << " ms\n";

        return 1;
    }

    std::cout << "stalled HTTPS transfer interrupted\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
