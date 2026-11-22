#include "android_usb_drive.hpp"
#include <iostream>
#include <stdexcept>
using namespace voyemu;

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool rejected(const auto &call) {
    try {
        call();
    } catch (const std::exception &error) {
        return std::string_view(error.what()).starts_with("USB_DRIVE:");
    }

    return false;
}
} // namespace

int main() {
    try {
        require(android_usb_arguments(false, "/tmp/voyemu-501/0123-media-qmp.sock") ==
                        std::vector<std::string>{
                                "-qemu", "-device", "usb-ehci,id=voyemu-usb", "-qmp",
                                "unix:/tmp/voyemu-501/0123-media-qmp.sock,server=on,wait=off"},
                "socket endpoint arguments differ");
        require(android_usb_arguments(true, Json{{"host", "127.0.0.1"}, {"port", 5564}}) ==
                        std::vector<std::string>{"-device", "usb-ehci,id=voyemu-usb", "-qmp",
                                                 "tcp:127.0.0.1:5564,server=on,wait=off"},
                "an existing QEMU boundary was repeated or the port differs");
        require(rejected([] {
                    (void)android_usb_arguments(true, Json{{"host", "0.0.0.0"}, {"port", 5564}});
                }),
                "a QMP port outside loopback was accepted");
        require(rejected([] {
                    (void)android_usb_arguments(true, Json{{"host", "127.0.0.1"}, {"port", 80}});
                }),
                "a privileged QMP port was accepted");

        require(usb_drive_add_command("/machines/free, test/android/usb-drive.img") ==
                        "drive_add 0 \"if=none,id=voyemu-usb-drive,format=raw,file=/machines/"
                        "free,, test/android/usb-drive.img\"",
                "drive command differs");
        require(monitor_quoted("C:\\My \"drive\"") == "\"C:\\\\My \\\"drive\\\"\"",
                "quotes and backslashes are not escaped");
        require(rejected([] {
                    (void)monitor_quoted("line\nbreak");
                }),
                "a control character reached the monitor");

        const auto device = usb_drive_device_arguments();
        require(device.at("driver") == "usb-storage" && device.at("bus") == "voyemu-usb.0" &&
                        device.at("drive") == "voyemu-usb-drive" && device.at("removable") == true,
                "the drive is not a removable usb-storage device on the EHCI bus");

        const std::string listing = "public:8,1 mounted 2026-1122\r\n"
                                    "public:179,1 unmounted 1234-ABCD\n";
        require(usb_volume_state(listing, "2026-1122") == "mounted", "mounted volume missed");
        require(usb_volume_state("public:8,1 checking 2026-1122", "2026-1122") == "checking",
                "last line without a newline missed");
        require(!usb_volume_state(listing, "2026-1123"), "a foreign volume matched");
        require(!usb_volume_state("private mounted null\nemulated mounted null\n", "2026-1122"),
                "a non-public volume matched");
        require(!usb_volume_state("", "2026-1122"), "an empty listing matched");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "USB drive arguments, monitor commands and volume listing passed\n";

    return 0;
}
