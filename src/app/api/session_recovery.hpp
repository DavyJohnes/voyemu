#pragma once
#include "android_adb.hpp"
#include "android_engine.hpp"
#include "check.hpp"
#include "machine_path.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "platform_file.hpp"
#include "process.hpp"
#include "profile_runtime.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <set>

namespace voyemu::api {
// A host-only recovery never asserts that the guest completed its shutdown.
class StoppedAndroidLease {
public:
    StoppedAndroidLease(const std::filesystem::path &root, const std::string &profile,
                        const MachineIdentity &machine_scope, const std::string &id,
                        const std::string &ownership);

    ~StoppedAndroidLease();

    StoppedAndroidLease(const StoppedAndroidLease &) = delete;

    const Json &original_runtime() const {
        return runtime_;
    }

    static bool endpoint_absent(const std::filesystem::path &endpoint);

    void verify_host_absence() const;

    void verify() const;

    Json publish_terminal() {
        verify();
        auto terminal = runtime_;
        terminal["stopped"] = true;
        auto &android = terminal["deliveries"]["android"];
        android["status"] = "host_terminal";
        android["powered"] = false;
        const auto previous_shutdown = android.value("shutdown", Json::object());
        android["shutdown"] = {
                {"wrapper_terminal", true},
                {"ports_released", true},
                {"guest_shutdown_unverified", true},
                {"source", "owned host absence proof; no guest shutdown acknowledgement"}};
        terminal["host_recovery"] = {
                {"schema", 1},
                {"session_id", id_},
                {"guest_shutdown_unverified", true},
                {"previous_android_shutdown", previous_shutdown},
                {"original_runtime_sha256", sha256_file(directory_ / "runtime.json")},
                {"launch_sha256", launch_sha_},
                {"process_enumeration", "exact pinned image paths absent"},
                {"ports", ports_}};
        verify();
        write_private_json(directory_ / "runtime.json", terminal);
        runtime_ = terminal;

        return terminal;
    }

private:
    static bool token(const std::string &s) {
        return hex_text(s, 32);
    }

    static void require(bool yes, const char *message) {
        if (!yes) {
            throw VoyemuError("HOST_RECOVERY", message);
        }
    }

    static std::filesystem::path path(const Json &record, const char *key) {
        require(record.at(key).is_string(), "path string required");

        return record.at(key).get<std::string>();
    }

    static std::int64_t pid(const Json &record, const char *key, bool positive) {
        const auto value = record.value(key, std::int64_t{});

        require(value >= (positive ? 1 : 0) && value <= UINT32_MAX, "bounded process id required");

        return value;
    }

    void verify_owner_records() const;
    static void ordinary_path(const std::filesystem::path &file);
    void endpoint_dead() const;
    void verify_processes() const;
    static std::array<unsigned long, 3> file_identity(const std::filesystem::path &file);
    void verify_listeners() const;
    void hold(unsigned port, int family);
    void release_sockets();
    std::vector<std::uintptr_t> sockets_;
    bool wsa_ = false;
    std::filesystem::path root_, directory_, active_path_, scoped_, frontend_image_;
    std::string profile_, id_, ownership_, launch_sha_;
    Json active_, owner_, supervisor_, runtime_, activation_, launch_, selection_, media_;
    std::vector<unsigned> ports_;
    std::set<std::int64_t> pids_;
    std::set<std::filesystem::path> images_;
    std::unique_ptr<FileLock> launch_lock_, supervisor_lock_;
};

// Stops the session of a supervisor that left no endpoint and returns its terminal record, or none
// while the supervisor may still run. Windows proves the absence of the owned host processes with
// StoppedAndroidLease, also after a stop request whose IPC failed (failed_stop); macOS and Linux
// take a released supervisor lock as proof that its holder exited, before any stop request.
std::optional<Json> recover_stopped_supervisor(const std::filesystem::path &root,
                                               const std::string &profile,
                                               const MachineIdentity &machine, const Json &record,
                                               const std::string &ownership,
                                               const std::filesystem::path &active_directory,
                                               bool failed_stop);
} // namespace voyemu::api
