#pragma once
#include "error.hpp"
#include "model.hpp"
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
// The USB drive of a multimedia guest is a QEMU usb-storage device on a USB 2.0 EHCI controller
// that the SDK process has from power-on; the session plugs and unplugs it through its own QMP
// channel, so the guest kernel enumerates a new mass-storage device as from a real USB port.
inline constexpr std::string_view usb_controller_id = "voyemu-usb";
inline constexpr std::string_view usb_drive_block_id = "voyemu-usb-drive";
inline constexpr std::string_view usb_drive_device_id = "voyemu-usb-stick";
// The FAT32 volume serial 0x20261122 of the drive image, as vold names the volume.
inline constexpr std::string_view usb_drive_volume_uuid = "2026-1122";

// The QMP endpoint of the SDK process: a private Unix socket path on macOS and Linux, a loopback
// TCP port on Windows.
inline std::string android_qmp_argument(const Json &endpoint) {
    if (endpoint.is_string()) {
        return "unix:" + endpoint.get<std::string>() + ",server=on,wait=off";
    }

    const auto port = endpoint.at("port").get<unsigned>();

    if (endpoint.at("host") != "127.0.0.1" || port < 1024 || port > 65535) {
        throw VoyemuError("USB_DRIVE", "owned loopback QMP port required");
    }

    return "tcp:127.0.0.1:" + std::to_string(port) + ",server=on,wait=off";
}

// The EHCI controller and the QMP channel after the QEMU boundary of the SDK arguments.
inline std::vector<std::string> android_usb_arguments(bool qemu_boundary_present,
                                                      const Json &endpoint) {
    std::vector<std::string> result;

    if (!qemu_boundary_present) {
        result.push_back("-qemu");
    }

    result.insert(result.end(), {"-device", "usb-ehci,id=" + std::string(usb_controller_id), "-qmp",
                                 android_qmp_argument(endpoint)});

    return result;
}

// A monitor string argument: the monitor reads a quoted argument with backslash escapes.
inline std::string monitor_quoted(std::string_view text) {
    std::string result = "\"";

    for (const char c : text) {
        if (c == '\0' || c == '\n' || c == '\r') {
            throw VoyemuError("USB_DRIVE", "monitor argument has a control character");
        }

        if (c == '"' || c == '\\') {
            result += '\\';
        }

        result += c;
    }

    return result + "\"";
}

// An option value of a QemuOpts list, which doubles each comma of a value.
inline std::string option_value(std::string_view value) {
    std::string result;

    for (const char c : value) {
        result += c;

        if (c == ',') {
            result += ',';
        }
    }

    return result;
}

// The monitor command that opens the drive image as a block backend without a device; the
// usb-storage device then takes it, and unplugging the device removes the backend.
inline std::string usb_drive_add_command(const std::filesystem::path &image) {
    return "drive_add 0 " + monitor_quoted("if=none,id=" + std::string(usb_drive_block_id) +
                                           ",format=raw,file=" + option_value(image.string()));
}

inline Json usb_drive_device_arguments() {
    return {{"driver", "usb-storage"},
            {"id", usb_drive_device_id},
            {"bus", std::string(usb_controller_id) + ".0"},
            {"drive", usb_drive_block_id},
            {"removable", true}};
}

// The state of the public volume with this file system UUID in `sm list-volumes public`, whose
// lines read "<volume id> <state> <uuid>"; empty when the guest lists no such volume.
inline std::optional<std::string> usb_volume_state(std::string_view listing,
                                                   std::string_view uuid) {
    while (!listing.empty()) {
        const auto end = listing.find('\n');
        auto line = listing.substr(0, end);
        listing = end == std::string_view::npos ? std::string_view{} : listing.substr(end + 1);

        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }

        const auto first = line.find(' ');
        const auto second = first == std::string_view::npos ? first : line.find(' ', first + 1);

        if (!line.starts_with("public:") || second == std::string_view::npos ||
            line.substr(second + 1) != uuid) {
            continue;
        }

        return std::string(line.substr(first + 1, second - first - 1));
    }

    return std::nullopt;
}
// /proc/mounts is authoritative when the restricted guest cannot enumerate public volumes via sm.
inline std::optional<std::string> usb_volume_mount_state(std::string_view mounts,
                                                         std::string_view uuid) {
    const std::string target = "/mnt/media_rw/" + std::string(uuid);
    constexpr std::string_view prefix = "/dev/block/vold/public:";
    std::istringstream input{std::string(mounts)};
    std::string line;
    std::size_t matches = 0;

    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string source, point, filesystem, options, dump, pass, extra;

        fields >> source >> point;
        if (point != target) {
            continue;
        }
        if (++matches != 1 || !(fields >> filesystem >> options >> dump >> pass) ||
            fields >> extra || filesystem != "vfat" || !source.starts_with(prefix)) {
            return std::nullopt;
        }

        const std::string_view device(source.data() + prefix.size(), source.size() - prefix.size());
        const auto comma = device.find(',');
        const auto major = device.substr(0, comma);
        const auto minor = comma == std::string_view::npos ? std::string_view{}
                                                           : device.substr(comma + 1);
        const auto flags = "," + options + ",";

        if (major.empty() || minor.empty() ||
            major.find_first_not_of("0123456789") != std::string_view::npos ||
            minor.find_first_not_of("0123456789") != std::string_view::npos ||
            flags.find(",rw,") == std::string::npos || flags.find(",ro,") != std::string::npos ||
            dump != "0" || pass != "0") {
            return std::nullopt;
        }
    }

    return matches == 1 ? std::optional<std::string>("mounted") : std::nullopt;
}
} // namespace voyemu
