#include "portable_runtime_internal.hpp"

// Publishes refreshed parts of a prepared machine: the UI observer, the Android system and the QNX
// images.
namespace voyemu {
using namespace portable_detail;

Json refresh_machine_ui_observer(const std::filesystem::path &root, std::string_view profile,
                                 const MachineIdentity &machine, const std::filesystem::path &seed,
                                 const std::function<Json()> &prepare, bool refresh_configuration) {
    const bool a9 = embedded_profile_declares(profile, ProfilePlatform::qinggan09,
                                              ProfileBoard::sa8155);

    need((a9 || portable_api30_ui_observer_profile(profile)) && machine.valid_for(profile) &&
                 (machine.selected || !refresh_configuration),
         "exact observer profile and named machine required");
    ordinary(root);
    ordinary(seed);
    // Same OS exclusion as CLI OperationLock, independent of API library dependencies.
    struct Lock {
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE;

        explicit Lock(const std::filesystem::path &path) {
            file = voyemu::detail::open_private_state_lock(
                    path, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);

            if (file == INVALID_HANDLE_VALUE) {
                const auto error = GetLastError();
                throw VoyemuError("SESSION_LOCK",
                                  "observer refresh requires stopped profile; win32=" +
                                          std::to_string(error));
            }

            BY_HANDLE_FILE_INFORMATION info{};

            if (!GetFileInformationByHandle(file, &info) ||
                (info.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                info.nNumberOfLinks != 1) {
                CloseHandle(file);
                throw VoyemuError("SESSION_LOCK", "observer refresh requires stopped profile");
            }
        }

        ~Lock() {
            CloseHandle(file);
        }
#else
        int file = -1;

        explicit Lock(const std::filesystem::path &path) {
            file = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            struct stat info{};

            if (file < 0 || ::fstat(file, &info) || !S_ISREG(info.st_mode) ||
                info.st_uid != ::geteuid() || info.st_nlink != 1 ||
                ::flock(file, LOCK_EX | LOCK_NB)) {
                if (file >= 0) {
                    ::close(file);
                }

                throw VoyemuError("SESSION_LOCK", "observer refresh requires stopped profile");
            }
        }

        ~Lock() {
            ::close(file);
        }
#endif
        Lock(const Lock &) = delete;
    };
    const auto active = native_current_active_directory(root, profile, machine);
    ensure_private_directory(active);
    Lock launch_lock(active / (std::string(profile) + ".launch.lock")),
            supervisor_lock(active / (std::string(profile) + ".supervisor.lock"));

    const auto stopped = [&] {
        const auto file = active / (std::string(profile) + ".json");

        if (std::filesystem::exists(file)) {
            ordinary(file);
            const auto owner = read_private_json(file, 4096);
            const std::filesystem::path directory = owner.at("directory").get<std::string>();
            need(same_directory_tail(directory,
                                     native_session_directory(root, profile, machine,
                                                              directory.filename().string()),
                                     3),
                 "active session directory differs");
            const auto runtime = read_private_json(directory / "runtime.json");
            need(runtime.at("session_id") == owner.at("session_id") &&
                         runtime.value("stopped", false),
                 "stop selected profile before observer refresh");
        }
    };
    stopped();
    const auto manifest_file = seed / "runtime.json";
    ordinary(manifest_file);
    const auto manifest_bytes = read_private_json_bytes(manifest_file, 128 * 1024);
    const auto manifest = Json::parse(manifest_bytes);
    const unsigned expected_api = a9 ? 28 : 30;
    need(manifest.at("profile") == profile && manifest.at("kind") == "portable-guest-runtime" &&
                 manifest.at("schema") == 1 && manifest.at("android").at("api") == expected_api,
         "exact observer machine API required");
    const auto definition = portable_profile_definition(root, profile, machine, seed);
    const bool identity_matches =
            a9 ? (!definition.contains("id") || definition.at("id") == profile)
               : (definition.contains("id") && definition.at("id") == profile);
    need(identity_matches && native_base_api(definition.at("android")) == expected_api,
         "exact observer snapshot profile required");
    const auto scoped = profile_launch_directory(root, profile, machine);
    ordinary(scoped);
    auto config_lock = owned_directory(scoped.parent_path() /
                                       (scoped.filename().string() + ".configuration.lock"));
    std::filesystem::path activation_file, launch_file;
    std::string activation_bytes, before;
    Json activation = Json::object(), original = Json::object(), old = Json::object();
    bool had_observer = false;

    if (refresh_configuration) {
        activation_file = scoped / "activation.json";
        launch_file = scoped / "android-launch.json";
        ordinary(activation_file);
        ordinary(launch_file);
        activation_bytes = read_private_json_bytes(activation_file, 128 * 1024);
        before = read_private_json_bytes(launch_file, 128 * 1024);
        activation = Json::parse(activation_bytes);
        original = Json::parse(before);
        need(activation.size() == 7 && activation.at("schema") == 1 &&
                     activation.at("kind") == "runtime-configuration" &&
                     activation.at("profile") == profile &&
                     canonical_machine_name(profile, activation.at("machine").get<std::string>()) ==
                             machine.name &&
                     activation.at("seed") == seed.string() &&
                     activation.at("manifest_sha256") == sha256_bytes(manifest_bytes) &&
                     activation.at("integrity_audited") == false,
             "exact existing machine configuration required");
        need(original.at("api") == expected_api && original.at("pins").is_array(),
             "matching observer launch API and pins required");
        had_observer = original.contains("ui_observer");
        old = original.value("ui_observer", Json::object());

        if (had_observer) {
            need(portable_ui_observer_binding(old, profile) && old.size() == (a9 ? 2 : 3),
                 "exact previous observer required");
            inside(root, old.at("dex").get<std::string>());
            checked_file(old.at("dex").get<std::string>(), old.at("sha256").get<std::string>(),
                         8 * mib);
        }
    }

    const auto embedded = seed / "observer/classes.dex";
    const bool had_embedded = manifest.contains("ui_observer");

    if (had_embedded) {
        (void)portable_observer_descriptor(seed, manifest);
    } else {
        need(!std::filesystem::exists(embedded) && !std::filesystem::is_symlink(embedded),
             "untracked embedded observer rejected");
    }

    auto observer = prepare();
    need(observer.is_object() && observer.size() == 2, "source-built observer descriptor required");

    if (!a9) {
        observer["profile"] = std::string(profile);
    }

    need(portable_ui_observer_binding(observer, profile), "exact source-built observer required");
    const std::filesystem::path dex = observer.at("dex").get<std::string>();
    inside(root, dex);
    const auto dex_identity = checked_file(dex, observer.at("sha256").get<std::string>(), 8 * mib);
    auto updated = original;

    if (refresh_configuration) {
        updated["ui_observer"] = observer;
        auto &pins = updated["pins"];
        unsigned old_matches = 0, new_matches = 0;

        for (auto it = pins.begin(); it != pins.end();) {
            need(it->is_object() && it->size() == 2 && it->at("path").is_string() &&
                         it->at("sha256").is_string(),
                 "exact launch pin required");

            if (had_observer && it->at("path") == old.at("dex")) {
                need(it->at("sha256") == old.at("sha256") && ++old_matches == 1,
                     "previous observer launch pin differs");
                it = pins.erase(it);
            } else {
                if (it->at("path") == dex.string()) {
                    ++new_matches;
                }

                ++it;
            }
        }

        need((!had_observer || old_matches == 1) && new_matches == 0,
             "observer pin inventory differs");
        add_pin(pins, dex, observer.at("sha256").get<std::string>());
    }

    need(dex != embedded, "source-built observer must be separate from installed payload");
    inside(root / ".local/toolchains/ui-observer", dex);
    std::map<std::filesystem::path, Json> preserved;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(seed)) {
        ordinary(entry.path());

        if (entry.is_regular_file() && entry.path() != manifest_file && entry.path() != embedded) {
            preserved.emplace(entry.path(), cache_file_identity(entry.path()));
        }
    }

    const auto profile_bytes = read_private_json_bytes(seed / "profile.json", 4 * mib);
    const auto stable = [&] {
        stopped();
        need(read_private_json_bytes(seed / "profile.json", 4 * mib) == profile_bytes,
             "observer profile snapshot changed");
        need(checked_file(dex, observer.at("sha256").get<std::string>(), 8 * mib) == dex_identity,
             "observer DEX identity changed");

        for (const auto &[file, identity] : preserved) {
            need(cache_file_identity(file) == identity,
                 "observer publication changed unrelated machine file");
        }
    };
    const auto verify = [&] {
        stable();

        if (had_embedded) {
            checked_file(embedded, manifest.at("ui_observer").at("sha256").get<std::string>(),
                         8 * mib);
        }

        need((!refresh_configuration ||
              (read_private_json_bytes(activation_file, 128 * 1024) == activation_bytes &&
               read_private_json_bytes(launch_file, 128 * 1024) == before)) &&
                     read_private_json_bytes(manifest_file, 128 * 1024) == manifest_bytes,
             "observer configuration source changed");
    };
    verify();
    auto next = manifest;
    next["ui_observer"] = {{"file", "observer/classes.dex"},
                           {"bytes", std::filesystem::file_size(dex)},
                           {"sha256", observer.at("sha256")}};
    const auto next_manifest = next.dump() + "\n";
    auto next_activation = activation;

    if (refresh_configuration) {
        next_activation["manifest_sha256"] = sha256_bytes(next_manifest);
    }

    ensure_private_directory(embedded.parent_path());
    ensure_private_directory(root / ".local/build/tmp");
    auto stage =
            owned_directory(root / ".local/build/tmp" / ("observer-upgrade-" + random_token()));
    const std::array<HostRefreshRecord, 3> records{
            {{manifest_file, manifest_bytes, next_manifest},
             {activation_file, activation_bytes, next_activation.dump() + "\n"},
             {launch_file, before, updated.dump() + "\n"}}};

    for (unsigned i = 0; i < records.size(); ++i) {
        if (!records[i].path.empty()) {
            write_private_json_bytes(stage.path / ("before-" + std::to_string(i) + ".json"),
                                     records[i].before, 128 * 1024);
        }
    }

    copy_sparse(dex, observer.at("sha256").get<std::string>(), stage.path / "new.dex", 0);
    verify();
    const auto read = [&](const auto &file) {
        ordinary(file);

        return read_private_json_bytes(file, 128 * 1024);
    };
    const auto write = [&](const auto &file, const auto &bytes) {
        write_private_json_bytes(file, bytes);
    };
    bool old_moved = false, new_moved = false;
    Json published_identity;
    try {
        if (had_embedded) {
            rename_exclusive(embedded, stage.path / "old.dex");
            old_moved = true;
        }

        rename_exclusive(stage.path / "new.dex", embedded);
        new_moved = true;
        published_identity = checked_file(embedded, observer.at("sha256").get<std::string>(),
                                          8 * mib);
        publish_host_refresh(records, read, write, [&] {
            stable();

            if (old_moved) {
                checked_file(stage.path / "old.dex",
                             manifest.at("ui_observer").at("sha256").get<std::string>(), 8 * mib);
            }

            need(checked_file(embedded, observer.at("sha256").get<std::string>(), 8 * mib) ==
                         published_identity,
                 "published observer identity changed");
        });
    } catch (...) {
        const auto failure = std::current_exception();
        try {
            rollback_host_refresh_payload(
                    records, read,
                    [&] {
                        if (new_moved) {
                            need(checked_file(embedded, observer.at("sha256").get<std::string>(),
                                              8 * mib) == published_identity,
                                 "foreign observer payload; recovery retained");
                            rename_exclusive(embedded, stage.path / "new.dex");
                        }

                        if (old_moved) {
                            checked_file(stage.path / "old.dex",
                                         manifest.at("ui_observer").at("sha256").get<std::string>(),
                                         8 * mib);
                            rename_exclusive(stage.path / "old.dex", embedded);
                        }
                    },
                    stage.retained);
        } catch (...) {
            stage.retained = true;
            throw VoyemuError(
                    "PORTABLE_RUNTIME",
                    "observer rollback incomplete; original metadata/payload retained at " +
                            stage.path.string());
        }

        std::rethrow_exception(failure);
    }

    return {{"profile", profile},
            {"machine", machine.selector()},
            {"observer", observer},
            {"embedded_observer_updated", true},
            {"machine_manifest_sha256", sha256_bytes(next_manifest)},
            {"host_refreshed", refresh_configuration},
            {"guest_started", false}};
}

Json publish_machine_android_system(const std::filesystem::path &root, std::string_view profile,
                                    const MachineIdentity &machine,
                                    const std::string &expected_manifest_sha,
                                    const std::filesystem::path &source,
                                    const std::string &source_sha, std::uint64_t source_bytes,
                                    const std::function<void()> &verify_source,
                                    const Json &disk_tool, const Json &owned_package,
                                    std::uint64_t reserve, const Json &source_evidence,
                                    const Json &camera_package, const Json &prepared_packages) {
    need((profile == qinggan11_731_profile_id ||
          embedded_profile_declares(profile, ProfilePlatform::passion, ProfileBoard::sa8155)) &&
                 machine.valid_for(profile) && sha256_text(expected_manifest_sha) &&
                 sha256_text(source_sha),
         "exact Free A11/Passion system upgrade required");
    ordinary(root);
    const auto seed = seed_directory(root, profile, machine);
    ordinary(seed);
    struct Lock {
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE;

        explicit Lock(const std::filesystem::path &path) {
            file = voyemu::detail::open_private_state_lock(
                    path, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
            BY_HANDLE_FILE_INFORMATION info{};

            if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &info) ||
                (info.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                info.nNumberOfLinks != 1) {
                if (file != INVALID_HANDLE_VALUE) {
                    CloseHandle(file);
                }

                throw VoyemuError("SESSION_LOCK", "system upgrade requires stopped profile");
            }
        }

        ~Lock() {
            CloseHandle(file);
        }
#else
        int file = -1;

        explicit Lock(const std::filesystem::path &path) {
            file = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            struct stat info{};

            if (file < 0 || ::fstat(file, &info) || !S_ISREG(info.st_mode) ||
                info.st_uid != ::geteuid() || info.st_nlink != 1 ||
                ::flock(file, LOCK_EX | LOCK_NB)) {
                if (file >= 0) {
                    ::close(file);
                }

                throw VoyemuError("SESSION_LOCK", "system upgrade requires stopped profile");
            }
        }

        ~Lock() {
            ::close(file);
        }
#endif
        Lock(const Lock &) = delete;
    };
    const auto active = native_current_active_directory(root, profile, machine);
    ensure_private_directory(active);
    Lock launch_lock(active / (std::string(profile) + ".launch.lock")),
            supervisor_lock(active / (std::string(profile) + ".supervisor.lock"));

    const auto stopped = [&] {
        const auto owner_file = active / (std::string(profile) + ".json");

        if (std::filesystem::exists(owner_file)) {
            ordinary(owner_file);
            const auto owner = read_private_json(owner_file, 4096);
            const std::filesystem::path directory = owner.at("directory").get<std::string>();
            need(same_directory_tail(directory,
                                     native_session_directory(root, profile, machine,
                                                              directory.filename().string()),
                                     3),
                 "active session directory differs");
            const auto runtime = read_private_json(directory / "runtime.json");
            need(runtime.at("session_id") == owner.at("session_id") &&
                         runtime.value("stopped", false),
                 "stop selected profile before system upgrade");
        }
    };
    stopped();
    const auto scoped = profile_launch_directory(root, profile, machine);
    ordinary(scoped);
    auto config_lock = owned_directory(scoped.parent_path() /
                                       (scoped.filename().string() + ".configuration.lock"));
    const auto manifest_file = seed / "runtime.json";
    ordinary(manifest_file);
    const auto before = read_private_json_bytes(manifest_file, 128 * 1024);
    need(sha256_bytes(before) == expected_manifest_sha, "system upgrade manifest CAS differs");
    auto manifest = Json::parse(before);
    need(manifest.at("profile") == profile && manifest.at("kind") == "portable-guest-runtime" &&
                 manifest.at("schema") == 1 && manifest.at("android").at("api") == 30,
         "prepared API30 machine required");
    const auto definition = portable_profile_definition(root, profile, machine, seed);
    need(definition.at("id") == profile, "machine profile snapshot differs");
    need(record_declares(definition, profile, ProfilePlatform::passion, ProfileBoard::sa8155) ==
                 !source_evidence.is_null(),
         "Passion system upgrade requires actual cold source evidence");
    Json generation = Json::object();

    if (!source_evidence.is_null()) {
        need(source_evidence.is_object() && source_evidence.size() == 2 &&
                     owned_package.is_null() && camera_package.is_null() &&
                     prepared_packages.is_null(),
             "exact wheel-free Passion source evidence required");
        const auto candidate =
                Json::parse(source_evidence.at("candidate_bytes").get<std::string>());
        const auto source_profile =
                Json::parse(source_evidence.at("profile_bytes").get<std::string>());
        api30_framework_boot::verify(source_profile);
        need(source_profile.at("id") == profile && candidate.at("profile") == profile &&
                     candidate.at("profile_sha256") ==
                             sha256_bytes(source_evidence.at("profile_bytes").get<std::string>()),
             "source evidence profile differs");
        // Original byte hashes are retained; serialization is supplied by the producer.
    }

    const auto data = seed / "android";
    ordinary(data);
    const auto old = manifest.at("android").at("images").at("system.img");
    (void)portable_system_image_file(old, 30);
    need(source_bytes > 0 && source_bytes <= (8ULL << 30) &&
                 std::filesystem::file_size(source) == source_bytes,
         "bounded candidate system required");
    const auto source_identity = checked_file(source, source_sha);
    // Capture all accepted files, including QNX resources and every writable app
    // disk. Only new generation names and runtime.json may change in this operation.
    std::map<std::filesystem::path, std::pair<Json, std::string>> preserved;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(seed)) {
        const auto path = entry.path();
        ordinary(path);

        if (std::filesystem::is_regular_file(path) && path != manifest_file) {
            need(std::filesystem::hard_link_count(path) == 1, "unaliased accepted file required");
            const auto identity = cache_file_identity(path);
            const auto sha = sha256_file(path);
            need(cache_file_identity(path) == identity, "accepted file changed during snapshot");
            preserved.emplace(path, std::make_pair(identity, sha));
        }
    }

    Json package_descriptor = Json::object();
    std::filesystem::path package_source;
    Json package_identity;
    bool package_changed = false;

    if (!owned_package.is_null()) {
        android_owned_package_descriptor(owned_package, false);
        package_source = owned_package.at("file").get<std::string>();
        package_identity = checked_file(package_source,
                                        owned_package.at("sha256").get<std::string>(), 16 * mib);
        need(std::filesystem::file_size(package_source) ==
                     owned_package.at("bytes").get<std::uint64_t>(),
             "owned package source extent differs");
        package_descriptor = owned_package;
        package_descriptor["file"] = "android/packages/" +
                                     owned_package.at("sha256").get<std::string>() + ".apk";
        android_owned_package_descriptor(package_descriptor, true);
        package_changed = manifest.at("android").value("owned_package", Json()) !=
                          package_descriptor;
    }

    Json camera_descriptor = Json::object();
    std::filesystem::path camera_source;
    Json camera_identity;
    bool camera_changed = false;

    if (!camera_package.is_null()) {
        need(profile == qinggan11_731_profile_id, "Camera package requires exact Free A11 profile");
        android_camera_package_descriptor(camera_package, false);
        camera_source = camera_package.at("file").get<std::string>();
        camera_identity = checked_file(camera_source,
                                       camera_package.at("sha256").get<std::string>(), 128 * mib);
        need(std::filesystem::file_size(camera_source) ==
                     camera_package.at("bytes").get<std::uint64_t>(),
             "Camera source extent differs");
        camera_descriptor = camera_package;
        camera_descriptor["file"] = "android/packages/" +
                                    camera_package.at("sha256").get<std::string>() + ".apk";
        android_camera_package_descriptor(camera_descriptor, true);
        camera_changed = manifest.at("android").value("camera_package", Json()) !=
                         camera_descriptor;
    }

    Json prepared_descriptors = Json::array();
    std::vector<std::filesystem::path> prepared_sources;
    std::vector<Json> prepared_identities;
    bool prepared_changed = false;

    if (!prepared_packages.is_null()) {
        need(profile == qinggan11_731_profile_id && camera_package.is_null(),
             "exact Free A11 prepared list required");
        android_prepared_packages_descriptor(prepared_packages, false);

        for (auto package : prepared_packages) {
            const auto file = std::filesystem::path(package.at("file").get<std::string>());
            const auto sha = package.at("sha256").get<std::string>();
            prepared_sources.push_back(file);
            prepared_identities.push_back(checked_file(file, sha, 512 * mib));
            need(std::filesystem::file_size(file) == package.at("bytes").get<std::uint64_t>(),
                 "prepared source extent differs");
            package["file"] = "android/packages/" + sha + ".apk";
            prepared_descriptors.push_back(package);
        }

        android_prepared_packages_descriptor(prepared_descriptors, true);
        prepared_changed = manifest.at("android").value("prepared_packages", Json()) !=
                                   prepared_descriptors ||
                           manifest.at("android").contains("camera_package");
    }

    const auto verify = [&] {
        stopped();
        need(read_private_json_bytes(manifest_file, 128 * 1024) == before,
             "system upgrade manifest CAS differs");
        verify_source();

        if (!package_source.empty()) {
            need(checked_file(package_source, owned_package.at("sha256").get<std::string>(),
                              16 * mib) == package_identity,
                 "candidate owned package changed");
        }

        if (!camera_source.empty()) {
            need(checked_file(camera_source, camera_package.at("sha256").get<std::string>(),
                              128 * mib) == camera_identity,
                 "candidate Camera package changed");
        }

        for (std::size_t i = 0; i < prepared_sources.size(); ++i) {
            need(checked_file(prepared_sources[i],
                              prepared_packages.at(i).at("sha256").get<std::string>(),
                              512 * mib) == prepared_identities[i],
                 "candidate prepared package changed");
        }

        need(checked_file(source, source_sha) == source_identity, "candidate system changed");

        for (const auto &[path, state] : preserved) {
            need(cache_file_identity(path) == state.first && sha256_file(path) == state.second,
                 "accepted application disk or resource changed");
        }

        need(read_private_json_bytes(manifest_file, 128 * 1024) == before,
             "system upgrade manifest CAS differs");
    };
    verify();
    const auto publish_package = [&] {
        if (package_descriptor.empty()) {
            return;
        }

        const auto target = seed / package_descriptor.at("file").get<std::string>();
        need(!std::filesystem::is_symlink(std::filesystem::symlink_status(target.parent_path())),
             "linked owned package directory rejected");
        ensure_private_directory(target.parent_path());
        ordinary(target.parent_path());

        if (std::filesystem::exists(target)) {
            checked_file(target, package_descriptor.at("sha256").get<std::string>(), 16 * mib);
            need(std::filesystem::file_size(target) ==
                         package_descriptor.at("bytes").get<std::uint64_t>(),
                 "existing owned package extent differs");

            return;
        }

        auto pending =
                owned_directory(target.parent_path() / (".package-pending-" + random_token()));
        copy_sparse(package_source, package_descriptor.at("sha256").get<std::string>(),
                    pending.path / "provider.apk", reserve);
        verify();
        rename_exclusive(pending.path / "provider.apk", target);
        checked_file(target, package_descriptor.at("sha256").get<std::string>(), 16 * mib);
#ifndef _WIN32
        const int directory = ::open(target.parent_path().c_str(),
                                     O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        need(directory >= 0, "package directory sync open failed");
        const bool synced = ::fsync(directory) == 0;
        ::close(directory);
        need(synced, "package directory sync failed");
#endif
    };
    const auto publish_camera = [&] {
        if (camera_descriptor.empty()) {
            return;
        }

        const auto target = seed / camera_descriptor.at("file").get<std::string>();
        need(!std::filesystem::is_symlink(std::filesystem::symlink_status(target.parent_path())),
             "linked Camera package directory rejected");
        ensure_private_directory(target.parent_path());
        ordinary(target.parent_path());

        if (std::filesystem::exists(target)) {
            checked_file(target, camera_descriptor.at("sha256").get<std::string>(), 128 * mib);
            need(std::filesystem::file_size(target) ==
                         camera_descriptor.at("bytes").get<std::uint64_t>(),
                 "existing Camera extent differs");

            return;
        }

        auto pending =
                owned_directory(target.parent_path() / (".camera-pending-" + random_token()));
        copy_sparse(camera_source, camera_descriptor.at("sha256").get<std::string>(),
                    pending.path / "camera.apk", reserve);
        verify();
        rename_exclusive(pending.path / "camera.apk", target);
        checked_file(target, camera_descriptor.at("sha256").get<std::string>(), 128 * mib);
#ifndef _WIN32
        const int directory = ::open(target.parent_path().c_str(),
                                     O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        need(directory >= 0, "Camera directory sync open failed");
        const bool synced = ::fsync(directory) == 0;
        ::close(directory);
        need(synced, "Camera directory sync failed");
#endif
    };
    const auto publish_prepared = [&] {
        for (std::size_t i = 0; i < prepared_sources.size(); ++i) {
            const auto &package = prepared_descriptors.at(i);
            const auto target = seed / package.at("file").get<std::string>();
            need(!std::filesystem::is_symlink(
                         std::filesystem::symlink_status(target.parent_path())),
                 "linked prepared package directory rejected");
            ensure_private_directory(target.parent_path());
            ordinary(target.parent_path());

            if (std::filesystem::exists(target)) {
                checked_file(target, package.at("sha256").get<std::string>(), 512 * mib);
                need(std::filesystem::file_size(target) == package.at("bytes").get<std::uint64_t>(),
                     "existing prepared extent differs");
                continue;
            }

            auto pending =
                    owned_directory(target.parent_path() / (".prepared-pending-" + random_token()));
            copy_sparse(prepared_sources[i], package.at("sha256").get<std::string>(),
                        pending.path / "package.apk", reserve);
            verify();
            rename_exclusive(pending.path / "package.apk", target);
            checked_file(target, package.at("sha256").get<std::string>(), 512 * mib);
#ifndef _WIN32
            const int directory = ::open(target.parent_path().c_str(),
                                         O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            need(directory >= 0, "prepared directory sync open failed");
            const bool synced = ::fsync(directory) == 0;
            ::close(directory);
            need(synced, "prepared directory sync failed");
#endif
        }
    };

    if (old.at("sha256") == source_sha) {
        if (!source_evidence.is_null()) {
            const auto evidence = passion_system_evidence(seed, manifest);
            const auto &lineage = manifest.at("android").at("system_upgrade");
            need(lineage.at("source_profile").at("sha256") ==
                                 sha256_bytes(
                                         source_evidence.at("profile_bytes").get<std::string>()) &&
                         lineage.at("candidate").at("sha256") ==
                                 sha256_bytes(
                                         source_evidence.at("candidate_bytes").get<std::string>()),
                 "existing system cold source epoch differs");
        }

        need(old.at("bytes") == source_bytes, "existing system extent differs");

        if (package_changed || camera_changed || prepared_changed) {
            publish_package();
            publish_camera();
            publish_prepared();
            verify();

            if (!package_descriptor.empty()) {
                manifest["android"]["owned_package"] = package_descriptor;
            }

            if (!camera_descriptor.empty()) {
                manifest["android"]["camera_package"] = camera_descriptor;
            }

            if (!prepared_descriptors.empty()) {
                manifest["android"].erase("camera_package");
                manifest["android"]["prepared_packages"] = prepared_descriptors;
            }

            write_private_json(manifest_file, manifest);
        }

        return {{"profile", profile},
                {"machine", machine.selector()},
                {"system_sha256", source_sha},
                {"cached", !package_changed && !camera_changed && !prepared_changed},
                {"applications_preserved", true},
                {"guest_started", false}};
    }

    const auto versions = data / "system-versions";
    need(!std::filesystem::is_symlink(std::filesystem::symlink_status(versions)),
         "linked system generation directory rejected");
    ensure_private_directory(versions);
    ordinary(versions);
    const auto target = versions / source_sha;
    need(!std::filesystem::exists(target) && !std::filesystem::is_symlink(target),
         "system generation already exists; explicit recovery required");
    auto stage = owned_directory(versions / (".system-pending-" + random_token()));
    copy_sparse(source, source_sha, stage.path / "system.img", reserve);
    const auto launch = read_private_json(scoped / "android-launch.json");
    need(launch.at("machine") == data.string(), "system upgrade machine launch differs");
    const std::filesystem::path sdk = launch.at("android_sdk_root").get<std::string>();
    inside(root, sdk);
    ordinary(sdk);
#ifdef _WIN32
    const auto tool = sdk / "emulator/qemu-img.exe";
#else
    const auto tool = sdk / "emulator/qemu-img";
#endif
    need(disk_tool.is_object() && disk_tool.size() == 2 && disk_tool.at("path") == tool.string() &&
                 disk_tool.at("sha256").is_string(),
         "verified system disk tool descriptor required");
    const auto tool_sha = disk_tool.at("sha256").get<std::string>();
    const auto tool_identity = checked_file(tool, tool_sha, 512 * mib);
    ProcessSpec command;
    command.executable = tool;
    command.directory = stage.path;
    command.arguments = {"create", "-f", "qcow2",      "-F",
                         "raw",    "-b", "system.img", "system.img.qcow2"};
    const auto result = execute(command, 10000, 16384);

    if (result.timed_out || result.exit_code) {
        throw VoyemuError("PORTABLE_RUNTIME", "system upgrade overlay creation failed: exit=" +
                                                      std::to_string(result.exit_code) + " " +
                                                      result.output);
    }

    const auto overlay = stage.path / "system.img.qcow2";
    ordinary(overlay);
    need(std::filesystem::is_regular_file(overlay) &&
                 std::filesystem::hard_link_count(overlay) == 1 &&
                 std::filesystem::file_size(overlay) < 16 * mib,
         "small independent system overlay required");
    need(cache_file_identity(tool) == tool_identity && sha256_file(tool) == tool_sha,
         "system disk tool changed");
#ifdef _WIN32
    HANDLE sync = CreateFileW(overlay.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    need(sync != INVALID_HANDLE_VALUE, "system overlay sync open failed");
    const bool synced = FlushFileBuffers(sync) != 0;
    CloseHandle(sync);
    need(synced, "system overlay sync failed");
#else
    const int sync = ::open(overlay.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    need(sync >= 0, "system overlay sync open failed");
    const bool synced = ::fsync(sync) == 0;
    ::close(sync);
    need(synced, "system overlay sync failed");
#endif
    {
        std::ifstream input(overlay, std::ios::binary);
        std::array<unsigned char, 24> header{};
        input.read(reinterpret_cast<char *>(header.data()), header.size());
        need(input.gcount() == 24 && header[0] == 'Q' && header[1] == 'F' && header[2] == 'I' &&
                     header[3] == 0xfb,
             "system overlay QCOW header differs");
        std::uint64_t offset = 0;
        std::uint32_t size = 0;

        for (unsigned i = 8; i < 16; ++i) {
            offset = (offset << 8) | header[i];
        }

        for (unsigned i = 16; i < 20; ++i) {
            size = (size << 8) | header[i];
        }

        need(size == 10 && offset >= 24 && offset <= 65536 &&
                     offset + size <= std::filesystem::file_size(overlay),
             "system overlay backing extent differs");
        std::string backing(size, '\0');
        input.seekg(offset);
        input.read(backing.data(), size);
        need(bool(input) && backing == "system.img", "system overlay relative backing differs");
    }

    if (!source_evidence.is_null()) {
        const auto prefix = "android/system-versions/" + source_sha + "/";
        auto proof = [&](const char *name, const std::string &bytes) {
            write_private_json_bytes(stage.path / name, bytes, 1 << 20);
            const auto file = stage.path / name;

            return Json{{"file", prefix + name},
                        {"bytes", std::filesystem::file_size(file)},
                        {"sha256", sha256_file(file)}};
        };
        generation = {
                {"schema", 1},
                {"source_profile", proof("source-profile.json",
                                         source_evidence.at("profile_bytes").get<std::string>())},
                {"candidate", proof("cold-candidate.json",
                                    source_evidence.at("candidate_bytes").get<std::string>())},
                {"parent_manifest", proof("parent-manifest.json", before)}};
        auto preview = manifest;
        preview["android"]["images"]["system.img"] = {
                {"file", prefix + "system.img"}, {"bytes", source_bytes}, {"sha256", source_sha}};
        preview["android"]["system_upgrade"] = generation;
        const auto candidate =
                Json::parse(source_evidence.at("candidate_bytes").get<std::string>());
        const auto source_profile =
                Json::parse(source_evidence.at("profile_bytes").get<std::string>());

        for (const auto *key : {"display_power", "mcu_property"}) {
            auto descriptor = generation.at("candidate");
            descriptor["schema"] = 1;
            descriptor["capability"] =
                    std::string(key) == "display_power"
                            ? validate_passion_power_candidate(
                                      source_profile, candidate,
                                      candidate.at("android").at("images"),
                                      candidate.at("profile_sha256").get<std::string>())
                            : validate_passion_mcu_candidate(
                                      source_profile, candidate,
                                      candidate.at("android").at("images"),
                                      candidate.at("profile_sha256").get<std::string>());
            preview["android"][key] = descriptor;
        }

        (void)passion_system_evidence(seed, preview, stage.path);
        generation["display_power"] = preview.at("android").at("display_power");
        generation["mcu_property"] = preview.at("android").at("mcu_property");
    }

    verify();
    std::filesystem::remove(stage.path / ".owner.json");
#ifndef _WIN32
    {
        const int directory = ::open(stage.path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        need(directory >= 0, "staged system directory sync open failed");
        const bool synced = ::fsync(directory) == 0;
        ::close(directory);
        need(synced, "staged system directory sync failed");
    }
#endif
    rename_exclusive(stage.path, target);
    stage.retained = true;
    // Sync the immutable generation directory before publishing its manifest name.
#ifndef _WIN32
    const int directory = ::open(versions.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    need(directory >= 0, "system generation directory sync open failed");
    const bool directory_synced = ::fsync(directory) == 0;
    ::close(directory);
    need(directory_synced, "system generation directory sync failed");
#endif
    verify();
    checked_file(target / "system.img", source_sha);
    need(std::filesystem::file_size(target / "system.img") == source_bytes,
         "published system extent differs");
    publish_package();
    publish_camera();
    publish_prepared();
    verify();
    manifest["android"].erase("can_model_receipt");
    manifest["android"]["images"]["system.img"] = {
            {"file", "android/system-versions/" + source_sha + "/system.img"},
            {"bytes", source_bytes},
            {"sha256", source_sha}};

    if (!package_descriptor.empty()) {
        manifest["android"]["owned_package"] = package_descriptor;
    }

    if (!camera_descriptor.empty()) {
        manifest["android"]["camera_package"] = camera_descriptor;
    }

    if (!prepared_descriptors.empty()) {
        manifest["android"].erase("camera_package");
        manifest["android"]["prepared_packages"] = prepared_descriptors;
    }

    if (!generation.empty()) {
        manifest["android"]["display_power"] = generation.at("display_power");
        manifest["android"]["mcu_property"] = generation.at("mcu_property");
        generation.erase("display_power");
        generation.erase("mcu_property");
        manifest["android"]["system_upgrade"] = generation;
        (void)passion_system_evidence(seed, manifest);
    }

    write_private_json(manifest_file, manifest);

    return {{"profile", profile},
            {"machine", machine.selector()},
            {"system_sha256", source_sha},
            {"previous_system_sha256", old.at("sha256")},
            {"applications_preserved", true},
            {"guest_started", false}};
}

static Json publish_machine_qnx_impl(const std::filesystem::path &root, std::string_view profile,
                                     const MachineIdentity &machine,
                                     const std::string &expected_manifest_sha,
                                     const std::filesystem::path &source, const Json &replacement,
                                     const std::function<void()> &verify_source, bool persistent,
                                     const std::filesystem::path &explicit_seed = {}) {
    // The replacement adapter decides: persistent storage, or an MCU projection otherwise.
    const auto requested = qnx_portable_layout(profile, replacement);

    need((persistent ? !requested.persistence.is_null() : !requested.projection.is_null()) &&
                 machine.valid_for(profile) && sha256_text(expected_manifest_sha),
         "accepted QNX upgrade adapter required");
    ordinary(root);
    const auto seed = seed_directory(root, profile, machine, explicit_seed);
    ordinary(seed);
    ordinary(source);
    inside(root / ".local", source);
    struct Lock {
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE;

        explicit Lock(const std::filesystem::path &path) {
            file = voyemu::detail::open_private_state_lock(
                    path, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
            BY_HANDLE_FILE_INFORMATION info{};

            if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &info) ||
                (info.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                info.nNumberOfLinks != 1) {
                if (file != INVALID_HANDLE_VALUE) {
                    CloseHandle(file);
                }

                throw VoyemuError("SESSION_LOCK", "system upgrade requires stopped profile");
            }
        }

        ~Lock() {
            CloseHandle(file);
        }
#else
        int file = -1;

        explicit Lock(const std::filesystem::path &path) {
            file = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            struct stat info{};

            if (file < 0 || ::fstat(file, &info) || !S_ISREG(info.st_mode) ||
                info.st_uid != ::geteuid() || info.st_nlink != 1 ||
                ::flock(file, LOCK_EX | LOCK_NB)) {
                if (file >= 0) {
                    ::close(file);
                }

                throw VoyemuError("SESSION_LOCK", "system upgrade requires stopped profile");
            }
        }

        ~Lock() {
            ::close(file);
        }
#endif
        Lock(const Lock &) = delete;
    };
    const auto seed_locks = root / ".local/build/qnx-upgrades";
    ensure_private_directory(seed_locks);
    Lock seed_lock(seed_locks / (sha256_bytes(seed.generic_string()) + ".lock"));
    const auto active = native_current_active_directory(root, profile, machine);
    ensure_private_directory(active);
    Lock launch_lock(active / (std::string(profile) + ".launch.lock")),
            supervisor_lock(active / (std::string(profile) + ".supervisor.lock"));

    const auto stopped = [&] {
        const auto owner_file = active / (std::string(profile) + ".json");

        if (std::filesystem::exists(owner_file)) {
            ordinary(owner_file);
            const auto owner = read_private_json(owner_file, 4096);
            const std::filesystem::path directory = owner.at("directory").get<std::string>();
            need(same_directory_tail(directory,
                                     native_session_directory(root, profile, machine,
                                                              directory.filename().string()),
                                     3),
                 "active session directory differs");
            const auto runtime = read_private_json(directory / "runtime.json");
            need(runtime.at("session_id") == owner.at("session_id") &&
                         runtime.value("stopped", false),
                 "stop selected profile before system upgrade");
        }
    };
    stopped();
    const auto scoped = profile_launch_directory(root, profile, machine);
    ordinary(scoped);
    auto config_lock = owned_directory(scoped.parent_path() /
                                       (scoped.filename().string() + ".configuration.lock"));
    const auto manifest_file = seed / "runtime.json";
    const auto before = read_private_json_bytes(manifest_file, 128 * 1024);
    need(sha256_bytes(before) == expected_manifest_sha, "QNX upgrade manifest CAS differs");
    auto manifest = Json::parse(before);
    need(manifest.at("profile") == profile && manifest.at("schema") == 1 &&
                 manifest.at("kind") == "portable-guest-runtime" &&
                 manifest.at("android").at("api") == (persistent ? 28 : 30),
         "prepared Free A11 machine required");
    const auto old_layout = qnx_portable_upgrade_source_layout(profile, manifest.at("qnx")),
               layout = qnx_portable_layout(profile, replacement);
    need(old_layout.adapter == layout.adapter &&
                 (persistent ? !old_layout.persistence.is_null() &&
                                       old_layout.persistence == layout.persistence
                             : old_layout.persistence.is_null() && layout.persistence.is_null()),
         "QNX mutable storage cannot be replaced");
    const auto mutable_role = [&](std::string_view name) {
        return persistent && (name == "sysla-gpt-ramdisk-v3.img" || name == "userdata-qnx6.img");
    };
    auto next = replacement;

    if (persistent) {
        for (const auto &name : layout.images) {
            if (mutable_role(name)) {
                need(manifest.at("qnx").at("images").at(name).at("bytes") ==
                             replacement.at("images").at(name).at("bytes"),
                     "persistent QNX extent differs");
                next["images"][name] = manifest.at("qnx").at("images").at(name);
            }
        }
    }

    std::map<std::filesystem::path, std::pair<Json, std::string>> preserved;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(seed)) {
        const auto path = entry.path();
        ordinary(path);

        if (std::filesystem::is_regular_file(path) && path != manifest_file &&
            (path.lexically_relative(seed).begin()->string() != "qnx" ||
             mutable_role(path.filename().string()))) {
            need(std::filesystem::hard_link_count(path) == 1,
                 "unaliased application file required");
            const auto identity = cache_file_identity(path);
            const auto sha = sha256_file(path);
            need(cache_file_identity(path) == identity, "application file changed during snapshot");
            preserved.emplace(path, std::make_pair(identity, sha));
        }
    }

    const auto verify = [&] {
        stopped();
        need(read_private_json_bytes(manifest_file, 128 * 1024) == before,
             "QNX upgrade manifest CAS differs");
        verify_source();

        for (const auto &[path, state] : preserved) {
            need(cache_file_identity(path) == state.first && sha256_file(path) == state.second,
                 "QNX upgrade changed an application file");
        }
    };
    verify();
    // Fixed portable QNX paths are swapped only under both runtime locks. The old
    // nonpersistent images stay available for rollback until the manifest commits.
    const auto temporary = root / ".local/build/tmp";
    ensure_private_directory(temporary);
    auto stage = owned_directory(temporary / ("qnx-upgrade-" + random_token()));
    ensure_private_directory(stage.path / "new");

    for (const auto &name : layout.images) {
        if (mutable_role(name)) {
            continue;
        }

        const auto &image = replacement.at("images").at(name);
        const auto file = source / image.at("file").get<std::string>();
        need(std::filesystem::file_size(file) == image.at("bytes").get<std::uint64_t>(),
             "QNX upgrade source extent differs");
        copy_sparse(file, image.at("sha256").get<std::string>(), stage.path / "new" / name,
                    8ULL << 30);
    }

    verify();
    const auto current = seed / "qnx";
    ordinary(current);
    // Reject unknown or modified QNX files rather than discarding an installation.
    std::map<std::string, std::pair<Json, std::string>> old_files;
    std::set<std::string> seen;

    for (const auto &entry : std::filesystem::directory_iterator(current)) {
        need(entry.is_regular_file() && !entry.is_symlink(), "unknown QNX payload rejected");
        const auto name = entry.path().filename().string();
        need(manifest.at("qnx").at("images").contains(name) && seen.insert(name).second,
             "unknown QNX payload rejected");
        const auto &image = manifest.at("qnx").at("images").at(name);
        const auto sha = mutable_role(name) ? sha256_file(entry.path())
                                            : image.at("sha256").get<std::string>();
        const auto identity = checked_file(entry.path(), sha);
        old_files.emplace(name, std::make_pair(identity, sha));
        need(std::filesystem::file_size(entry.path()) == image.at("bytes").get<std::uint64_t>(),
             "installed QNX payload differs");
    }

    need(seen.size() == old_layout.images.size(), "QNX payload inventory differs");
    auto after = manifest;
    after["qnx"] = next;
    const auto after_bytes = after.dump() + "\n";
    bool old_moved = false, new_moved = false;

    if (persistent) {
        ensure_private_directory(stage.path / "old");
        std::vector<std::string> old_roles, new_roles;
        try {
            for (const auto &name : layout.images) {
                if (mutable_role(name)) {
                    continue;
                }

                rename_exclusive(current / name, stage.path / "old" / name);
                old_roles.push_back(name);
                rename_exclusive(stage.path / "new" / name, current / name);
                new_roles.push_back(name);
            }

            verify();

            for (const auto &name : old_roles) {
                const auto &state = old_files.at(name);
                auto identity = cache_file_identity(stage.path / "old" / name),
                     expected_identity = state.first;

                for (const auto *field : {"ctime_seconds", "ctime_ns", "change_time"}) {
                    identity.erase(field);
                    expected_identity.erase(field);
                }

                need(identity == expected_identity &&
                             sha256_file(stage.path / "old" / name) == state.second,
                     "old QNX boot image changed during publication");
            }

            write_private_json_bytes(manifest_file, after_bytes);
        } catch (...) {
            stage.retained = true;
            const auto failure = std::current_exception();
            const auto actual = read_private_json_bytes(manifest_file, 128 * 1024);
            need(actual == before || actual == after_bytes,
                 "QNX boot upgrade foreign manifest; rollback retained");

            if (actual == after_bytes) {
                write_private_json_bytes(manifest_file, before);
            }

            for (auto it = new_roles.rbegin(); it != new_roles.rend(); ++it) {
                rename_exclusive(current / *it, stage.path / "new" / *it);
            }

            for (auto it = old_roles.rbegin(); it != old_roles.rend(); ++it) {
                rename_exclusive(stage.path / "old" / *it, current / *it);
            }

            stage.retained = false;
            std::rethrow_exception(failure);
        }

        return {{"profile", profile},
                {"machine", machine.selector()},
                {"applications_preserved", true},
                {"persistent_storage_preserved", true},
                {"cluster_ready", true},
                {"guest_started", false}};
    }

    try {
        rename_exclusive(current, stage.path / "old");
        old_moved = true;
        rename_exclusive(stage.path / "new", current);
        new_moved = true;
        verify();

        for (const auto &[name, state] : old_files) {
            need(cache_file_identity(stage.path / "old" / name) == state.first &&
                         sha256_file(stage.path / "old" / name) == state.second,
                 "old QNX image changed during publication");
        }

        write_private_json_bytes(manifest_file, after_bytes);
    } catch (...) {
        stage.retained = true;
        const auto failure = std::current_exception();
        const auto actual = read_private_json_bytes(manifest_file, 128 * 1024);

        if (actual != before && actual != after_bytes) {
            throw VoyemuError("PORTABLE_RUNTIME",
                              "QNX upgrade foreign manifest; rollback retained at " +
                                      stage.path.string());
        }

        if (actual == after_bytes) {
            write_private_json_bytes(manifest_file, before);
        }

        if (new_moved) {
            rename_exclusive(current, stage.path / "new");
        }

        if (old_moved) {
            rename_exclusive(stage.path / "old", current);
        }

        stage.retained = false;
        std::rethrow_exception(failure);
    }

    return {{"profile", profile},
            {"machine", machine.selector()},
            {"applications_preserved", true},
            {"cluster_ready", true},
            {"guest_started", false}};
}

Json publish_machine_qnx(const std::filesystem::path &root, std::string_view profile,
                         const MachineIdentity &machine, const std::string &expected_manifest_sha,
                         const std::filesystem::path &source, const Json &replacement,
                         const std::function<void()> &verify_source) {
    return publish_machine_qnx_impl(root, profile, machine, expected_manifest_sha, source,
                                    replacement, verify_source, false);
}

Json publish_machine_qnx_boot(const std::filesystem::path &root, std::string_view profile,
                              const MachineIdentity &machine,
                              const std::string &expected_manifest_sha,
                              const std::filesystem::path &source, const Json &replacement,
                              const std::function<void()> &verify_source) {
    return publish_machine_qnx_impl(root, profile, machine, expected_manifest_sha, source,
                                    replacement, verify_source, true);
}

Json publish_machine_qnx_boot_at(const std::filesystem::path &root, std::string_view profile,
                                 const std::filesystem::path &seed,
                                 const std::string &expected_manifest_sha,
                                 const std::filesystem::path &source, const Json &replacement,
                                 const std::function<void()> &verify_source) {
    ordinary(seed);
    const auto machine = machine_identity(profile, seed.filename().string());

    return publish_machine_qnx_impl(root, profile, machine, expected_manifest_sha, source,
                                    replacement, verify_source, true, seed);
}
} // namespace voyemu
