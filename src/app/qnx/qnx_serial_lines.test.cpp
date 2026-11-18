#include "qnx_serial_lines.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace voyemu;
namespace {
void check(bool value, const char *why) {
    if (!value) {
        throw std::runtime_error(why);
    }
}

void append(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::app);

    output << bytes;
    check(output.good(), "fixture write");
}
} // namespace

int main() {
    try {
        const auto directory =
                std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("qnx-serial-lines-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        const auto serial = directory / "qnx-serial.log";
        QnxSerialLines lines;

        check(lines.read(serial).empty(), "missing file has no lines");
        append(serial, "QMC BEGIN raw-OEM-QNX6-readonly\r\nQIP STARTUP ignition reason=gauge-ch");
        check(lines.read(serial) == "QMC BEGIN raw-OEM-QNX6-readonly\n", "complete line only");
        append(serial,
               "eck\nNov 15 20:00:00.123    ssplash  qm  12 QMC CHILD begin=animation-ready\n" +
                       std::string(300, 'x') + "\nQIP GAUGE window-ready=/dev/screen/7\n");
        check(lines.read(serial) ==
                      "QIP STARTUP ignition reason=gauge-check\nQMC CHILD begin=animation-ready\n"
                      "QIP GAUGE window-ready=/dev/screen/7\n",
              "partial line joined, slog2 header removed, overlong line dropped");
        check(lines.read(serial).empty(), "each line once");

        // A new QEMU incarnation truncates the file and is read from its start.
        std::filesystem::remove(serial);
        append(serial, "QMC BEGIN\n");
        check(lines.read(serial) == "QMC BEGIN\n", "new incarnation from its start");

#ifndef _WIN32
        // A symbolic link is not the session UART file.
        const auto other = directory / "other.log";
        append(other, "QIP STARTUP ignition reason=gauge-check\n");
        std::filesystem::remove(serial);
        std::filesystem::create_symlink(other, serial);
        QnxSerialLines fresh;
        check(fresh.read(serial).empty(), "symbolic link rejected");
#endif
        std::filesystem::remove_all(directory);
        std::cout << "QNX serial lines: framing, slog2, bounds, incarnation, link PASS\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
