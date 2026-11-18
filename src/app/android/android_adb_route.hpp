#pragma once
#include "android_adb.hpp"
#include "model.hpp"
#include "process.hpp"
#include <cstdlib>
namespace voyemu {
inline void android_session_environment(ProcessSpec &process,
                                        const std::filesystem::path &directory) {
    const auto home = (directory / "emulator-home").string(),
               temporary = (directory / "tmp").string();

    for (const char *name :
         {"HOME", "ANDROID_EMULATOR_HOME", "ANDROID_USER_HOME", "USERPROFILE", "LOCALAPPDATA"}) {
        process.environment[name] = home;
    }

    for (const char *name : {"ANDROID_TMP", "TMPDIR", "TMP", "TEMP", "XDG_RUNTIME_DIR"}) {
        process.environment[name] = temporary;
    }
}

// Runtime-owned metadata is published by Session after retaining its server.
// Formatting never establishes ownership; callers must fence the retained handle.
inline AndroidOwnedAdb android_adb_binding(const Json &specification) {
    const auto &r = specification.at("owned_adb");

    if (!r.is_object() || r.value("schema", 0) != 1 || r.value("server_pid", std::int64_t{}) < 2) {
        throw VoyemuError("ANDROID_ADB_OWNER", "runtime server binding required");
    }

    return android_owned_adb(specification.at("port").get<unsigned>(),
                             r.at("grpc_port").get<unsigned>(), r.at("owner").get<std::string>(),
                             r.at("server_port").get<unsigned>());
}

inline void android_adb_owner_fence(const Json &specification, const Json &current,
                                    const std::string &owner, ChildProcess &sdk,
                                    ChildProcess &server, std::int64_t expected_pid,
                                    std::uint64_t expected_generation, std::uint64_t generation,
                                    bool require_backend) {
    if (expected_pid != sdk.pid() || expected_generation != generation ||
        specification.at("owned_adb") != current.at("owned_adb")) {
        throw VoyemuError("ANDROID_ADB_OWNER", "SDK generation changed");
    }

    const auto binding = android_adb_binding(specification);

    if (binding.owner != owner || specification.at("owned_adb").at("server_pid") != server.pid() ||
        !server.alive() || !server.owns_tcp_listener(binding.server)) {
        throw VoyemuError("ANDROID_ADB_OWNER", "retained server listener unavailable");
    }

    if (require_backend && (!sdk.alive() || !sdk.owns_tcp_listener(binding.console) ||
                            !sdk.owns_tcp_listener(binding.console + 1))) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY", "retained SDK listeners unavailable");
    }
}

inline std::string android_adb_serial(const Json &specification) {
    const auto &binding = specification.at("owned_adb");

    if (binding.value("schema", 0) == 1 && binding.contains("console_port")) {
        const auto port = specification.at("port").get<unsigned>();

        if (port < 5554 || port > 5682 || port % 2 || port == 5562 ||
            binding.at("console_port") != port || binding.at("adbd_port") != port + 1) {
            throw VoyemuError("ANDROID_ADB_OWNER", "direct port binding differs");
        }

        return "emulator-" + std::to_string(port);
    }

    return android_adb_binding(specification).serial();
}

inline ProcessSpec android_adb_process(const Json &specification,
                                       const std::vector<std::string> &command) {
    ProcessSpec process;

    process.executable = specification.at("adb").get<std::string>();
    const auto binding = android_adb_binding(specification);
    process.arguments = android_owned_adb_arguments(binding, command, true);
    process.environment = android_owned_adb_environment(binding);

    if (android_adb_trace_enabled(std::getenv("VOYEMU_ANDROID_ADB_TRACE"))) {
        process.environment["ADB_TRACE"] = "";
    }

    return process;
}
} // namespace voyemu
