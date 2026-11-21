#pragma once
#include "check.hpp"
#include "model.hpp"
#include "process.hpp"
#include "profile_runtime.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace voyemu {
namespace inactive_profile {
inline void require(bool yes) {
    if (!yes) {
        throw VoyemuError("PORTABLE_RUNTIME", "inactive profile identity is unverified");
    }
}

// The host checks of an inactive profile, in inactive_profile_posix.cpp and
// inactive_profile_windows.cpp; each throws PORTABLE_RUNTIME through require.
// An absolute normal private path of the current user: a directory or a single-link file. POSIX
// refuses any group or other mode bit, Windows any reparse component and any access entry beyond
// the current user, SYSTEM and Administrators.
void ordinary(const std::filesystem::path &path, bool directory = false);
// Whether a recorded session directory names directory: equal on POSIX, the same last three
// components on Windows, whose recorded form may differ in its prefix.
bool same_directory(const Json &recorded, const std::filesystem::path &directory);
// A recorded process id that names no live process; 0 names none.
void absent(const Json &pid);
// An ordinary file that no process holds open.
void unused(const std::filesystem::path &path);
// The control endpoint of session id is gone: a removed Unix socket in an ordinary directory on
// POSIX, an absent named pipe on Windows.
void endpoint_absent(const std::filesystem::path &endpoint, const std::string &id);

// The supervisor lock of a profile, held exclusively and checked to stay the same private file.
class SupervisorLock {
public:
    explicit SupervisorLock(const std::filesystem::path &path);
    ~SupervisorLock();
    SupervisorLock(const SupervisorLock &) = delete;
    SupervisorLock &operator=(const SupervisorLock &) = delete;
    void verify() const;

private:
    std::filesystem::path path_;
    std::intptr_t handle_ = -1;
    std::uint64_t volume_ = 0, index_ = 0;
};
} // namespace inactive_profile

// Keep the supervisor lease through the derived configuration refresh; stale records remain
// evidence.
class InactiveProfileLease {
    Json active_, runtime_;
    std::unique_ptr<inactive_profile::SupervisorLock> lock_;

public:
    InactiveProfileLease(const std::filesystem::path &root, std::string_view profile,
                         const MachineIdentity &machine, const Json &active, const Json &runtime,
                         const Json &storage)
        : active_(active), runtime_(runtime) {
        using namespace inactive_profile;
        require(machine.valid_for(profile));
        const auto id = active.at("session_id").get<std::string>();
        require(hex_text(id, 32) && active.at("schema") == 1 &&
                active.at("profile") == std::string(profile));
        const auto directory = native_session_directory(root, profile, machine, id);
        ordinary(directory, true);
        require(same_directory(active.at("directory"), directory) &&
                runtime.at("session_id") == id && runtime.at("endpoint") == active.at("endpoint"));
        const auto owner_path = directory / "session-owner.json";
        ordinary(owner_path);
        const auto owner = read_private_json(owner_path, 16384);
        require(owner.at("schema") == 1 && owner.at("session_id") == id &&
                same_directory(owner.at("directory"), directory) &&
                owner.at("origin") == "voyemu-native-supervisor");
        const auto active_dir = native_current_active_directory(root, profile, machine);
        ordinary(active_dir, true);
        lock_ = std::make_unique<SupervisorLock>(active_dir /
                                                 (std::string(profile) + ".supervisor.lock"));
        const auto verify = [&] {
            lock_->verify();
            endpoint_absent(active.at("endpoint").get<std::string>(), id);
            absent(owner.at("creator_pid"));

            if (owner.contains("qnx_pid")) {
                absent(owner.at("qnx_pid"));
            }

            for (const char *key : {"android_pid", "qnx_pid"}) {
                if (runtime.contains(key)) {
                    absent(runtime.at(key));
                }
            }

            if (owner.contains("android_processes")) {
                const auto &android = owner.at("android_processes");

                for (const char *key : {"sdk_pid", "adb_pid", "frontend_pid"}) {
                    if (android.contains(key)) {
                        absent(android.at(key));
                    }
                }

                for (const char *key : {"console_port", "adb_server_port", "grpc_port"}) {
                    if (android.contains(key)) {
                        const auto port = android.at(key).get<unsigned>();
                        require(port >= 1024 && port < 65535 && tcp_port_free(port));

                        if (std::string_view(key) == "console_port") {
                            require(tcp_port_free(port + 1));
                        }
                    }
                }
            }

            const auto ram = directory / "qnx-ram.bin";

            if (std::filesystem::exists(ram)) {
                unused(ram);
            }

            if (storage.is_object() && !storage.empty()) {
                for (const char *key : {"system", "userdata"}) {
                    unused(storage.at(key).get<std::string>());
                }
            }

            require(read_private_json(active_dir / (std::string(profile) + ".json"), 4096) ==
                            active &&
                    read_private_json(directory / "runtime.json", 1024 * 1024) == runtime &&
                    read_private_json(owner_path, 16384) == owner);
        };
        verify();
        verify();
    }

    bool matches(const Json &active, const Json &runtime) const {
        return active == active_ && runtime == runtime_;
    }
};
} // namespace voyemu
