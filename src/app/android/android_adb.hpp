#pragma once
#include "check.hpp"
#include "error.hpp"
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace voyemu {
inline bool android_adb_trace_enabled(const char *value) {
    if (!value) {
        return false;
    }

    if (std::string_view(value) != "1") {
        throw VoyemuError("ANDROID_ADB_TRACE", "expected exact value 1 or an unset variable");
    }

    return true;
}

// This describes an owned foreground server. The caller retains its ChildProcess
// and verifies that process owns the listener before and after every invocation.
struct AndroidOwnedAdb {
    unsigned console{}, grpc{}, server{};
    std::string owner;

    std::string serial() const {
        return "emulator-" + std::to_string(console);
    }

    std::string endpoint() const {
        return "127.0.0.1:" + std::to_string(console + 1);
    }

    std::string socket() const {
        return "tcp:127.0.0.1:" + std::to_string(server);
    }
};

inline AndroidOwnedAdb android_owned_adb(unsigned console, unsigned grpc, std::string owner,
                                         unsigned server = 0) {
    if (console < 5554 || console > 5682 || console % 2 || console == 5562 || grpc < 1024 ||
        grpc > 65535 || grpc == console || grpc == console + 1) {
        throw VoyemuError("ANDROID_ADB_BINDING", "isolated SDK ports required");
    }

    if (!server) {
        server = console + 20000;
    }

    if (server < 1024 || server > 65535 || server == 5037 || server == console ||
        server == console + 1 || server == grpc) {
        throw VoyemuError("ANDROID_ADB_BINDING", "isolated server port required");
    }

    if (!hex_text(owner, 32) && !sha256_text(owner)) {
        throw VoyemuError("ANDROID_ADB_BINDING", "retained session owner required");
    }

    return {console, grpc, server, std::move(owner)};
}

inline std::map<std::string, std::string>
android_owned_adb_environment(const AndroidOwnedAdb &binding) {
    // Keep the disconnected-emulator retry worker alive. Its initial scan range
    // 5555..1 is empty; retries address only a previously registered SDK endpoint.
    // USB and mDNS remain disabled; the SDK registers its own console alias.
    return {{"ADB_USB", "0"},
            {"ADB_EMU", "1"},
            {"ADB_MDNS", "0"},
            {"ADB_LOCAL_TRANSPORT_MAX_PORT", "1"},
            {"ADB_MDNS_AUTO_CONNECT", "0"},
            {"ADB_SERVER_SOCKET", binding.socket()},
            {"ANDROID_ADB_SERVER_ADDRESS", "127.0.0.1"},
            {"ANDROID_ADB_SERVER_PORT", std::to_string(binding.server)}};
}

// Trace belongs to the retained foreground server, not SDK-internal ADB clients.
inline std::map<std::string, std::string>
android_owned_adb_sdk_environment(const AndroidOwnedAdb &binding) {
    auto result = android_owned_adb_environment(binding);

    result["ADB_TRACE"] = "";

    return result;
}

inline std::vector<std::string> android_owned_adb_server_arguments(const AndroidOwnedAdb &binding) {
    // Pinned Windows ADB rejects a hostname in a listening socket specification.
    // tcp:PORT binds loopback by default; clients still use an explicit numeric host.
    return {"-L",           "tcp:" + std::to_string(binding.server),
            "--one-device", "voyemu-owned-" + binding.owner,
            "server",       "nodaemon"};
}

inline std::vector<std::string> android_owned_adb_prefix(const AndroidOwnedAdb &binding,
                                                         bool server_owned) {
    if (!server_owned) {
        throw VoyemuError("ANDROID_ADB_OWNER", "retained live server listener required");
    }

    // A numeric host is intentional: pinned ADB must fail if that server disappears,
    // rather than starting a detached localhost daemon. Runtime proof is separate.
    return {"-H", "127.0.0.1", "-P", std::to_string(binding.server)};
}

inline std::vector<std::string>
android_owned_adb_registration_arguments(const AndroidOwnedAdb &binding, bool server_owned,
                                         bool backend_owned) {
    if (!backend_owned) {
        throw VoyemuError("ANDROID_ADB_OWNER", "retained SDK transport listener required");
    }

    // The SDK registers emulator-N itself. A generic TCP connect would create a
    // second transport rather than select this already owned SDK registration.
    auto result = android_owned_adb_prefix(binding, server_owned);
    result.insert(result.end(), {"-s", binding.serial(), "get-state"});

    return result;
}

inline bool android_owned_adb_registration_ready(std::string_view state) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = state.find_first_not_of(whitespace);

    if (first == std::string_view::npos) {
        return false;
    }

    const auto last = state.find_last_not_of(whitespace);

    return state.substr(first, last - first + 1) == "device";
}

inline bool android_owned_observer_forward(const std::vector<std::string> &command) {
    if (command == std::vector<std::string>{"forward", "--list"}) {
        return true;
    }

    if (command.size() != 3 || command[0] != "forward") {
        return false;
    }

    if (command[1] == "tcp:0") {
        constexpr std::string_view prefix = "localabstract:voyemu-ui-";
        const auto &target = command[2];

        if (!target.starts_with(prefix)) {
            return false;
        }

        const auto owner = std::string_view(target).substr(prefix.size());

        return hex_text(owner, 32);
    }

    if (command[1] != "--remove" || !command[2].starts_with("tcp:")) {
        return false;
    }

    const auto text = std::string_view(command[2]).substr(4);
    unsigned port = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);

    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && port >= 1024 &&
           port <= 65535;
}

inline std::vector<std::string> android_owned_adb_arguments(const AndroidOwnedAdb &binding,
                                                            const std::vector<std::string> &command,
                                                            bool server_owned) {
    if (command.empty() ||
        (command[0] != "shell" && command[0] != "push" && command[0] != "pull" &&
         command[0] != "root" && command[0] != "wait-for-device" && command[0] != "emu" &&
         command[0] != "reboot" && !android_owned_observer_forward(command))) {
        throw VoyemuError("ANDROID_ADB_COMMAND", "owned device command required");
    }

    auto result = android_owned_adb_prefix(binding, server_owned);
    // Guest and console commands share the SDK registration in this private server.
    const auto serial = binding.serial();
    result.insert(result.end(), {"-s", serial});
    result.insert(result.end(), command.begin(), command.end());

    return result;
}
} // namespace voyemu
