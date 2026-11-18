#include "android_reboot.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <string>
using namespace voyemu;

int main() try {
    const std::string first = "12345678-1234-1234-1234-123456789abc",
                      second = "12345678-1234-1234-1234-123456789abd";

    assert(android_boot_id_valid(first));
    assert(!android_boot_id_valid(first + "\n"));
    assert(!android_boot_id_valid("12345678x1234-1234-1234-123456789abc"));
    assert(!android_boot_id_valid("12345678-1234-1234-1234-123456789abz"));
    assert(!android_reboot_changed(first, first));
    assert(android_reboot_changed(first, second));
    validate_android_reboot_mode("warm");
    validate_android_reboot_mode("cold");

    for (const auto &mode : {"", "restart", "Warm", "cold; reboot"}) {
        bool rejected = false;
        try {
            validate_android_reboot_mode(mode);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    }

    bool rejected = false;
    try {
        android_reboot_changed(first, "");
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
