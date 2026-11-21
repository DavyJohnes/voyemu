#include "session.hpp"
#include "session_recovery.hpp"
namespace voyemu::api {
StoppedAndroidLease::StoppedAndroidLease(const std::filesystem::path &root,
                                         const std::string &profile,
                                         const MachineIdentity &machine_scope,
                                         const std::string &id, const std::string &ownership)
    : root_(root), profile_(profile), id_(id), ownership_(ownership) {
    require(valid_profile_name(profile) && token(id) && token(ownership) &&
                    machine_scope.valid_for(profile),
            "explicit owned epoch required");
    throw VoyemuError("HOST_RECOVERY_UNSUPPORTED", "Windows host-only recovery required");
}

StoppedAndroidLease::~StoppedAndroidLease() = default;

bool StoppedAndroidLease::endpoint_absent(const std::filesystem::path &endpoint) {
    return !std::filesystem::exists(endpoint);
}

void StoppedAndroidLease::verify_host_absence() const {}

void StoppedAndroidLease::verify() const {}

std::optional<Json> recover_stopped_supervisor(const std::filesystem::path &root,
                                               const std::string &profile,
                                               const MachineIdentity &machine, const Json &record,
                                               const std::string &ownership,
                                               const std::filesystem::path &active_directory,
                                               bool failed_stop) {
    (void)root;
    (void)machine;
    (void)ownership;
    // The session receipts decide what remains to stop.
    const auto supervisor_lock = active_directory / (profile + ".supervisor.lock");

    if (failed_stop ||
        !StoppedAndroidLease::endpoint_absent(record.at("endpoint").get<std::string>()) ||
        !file_lock_released(supervisor_lock)) {
        return std::nullopt;
    }

    const FileLock launch(active_directory / (profile + ".launch.lock")),
            supervisor(supervisor_lock);

    return stop_orphaned_session(record.at("directory").get<std::string>(),
                                 record.at("session_id").get<std::string>(),
                                 record.at("endpoint").get<std::string>());
}
} // namespace voyemu::api
