// The native listener that wine_native_backend_windows.test.cpp launches through the Wine
// diagnostic backend.
#include "../core/model.hpp"
#include "../core/process.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        return 64;
    }

    if (std::string(argv[1]) == "--sleep") {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[2])));

        return 7;
    }

    if (argc != 4 || std::string(argv[1]) != "--listen" ||
        std::string(argv[3]) != "argument with spaces") {
        return 65;
    }

    auto path = std::getenv("VOYEMU_DIAG_PATH");

    if (!path || path[0] != '/' ||
        (std::getenv("PATH") && std::string(std::getenv("PATH")).starts_with("C:"))) {
        return 67;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(std::stoi(argv[2]));

    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) || listen(fd, 1)) {
        return 66;
    }

    std::this_thread::sleep_for(std::chrono::seconds(30));
    close(fd);

    return 0;
}
