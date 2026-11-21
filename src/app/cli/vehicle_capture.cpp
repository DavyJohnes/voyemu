#include "vehicle_capture.hpp"
#include "check.hpp"
#include "cli_messages.hpp"
#include "deps.hpp"
#include "error.hpp"
#include "fetch.hpp"
#include "java_toolchain.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "project_files.hpp"
#include "qnx_adapter.hpp"
#include "qnx_originals_directory.hpp"
#include "qnx_qinggan11.hpp"
#include "qnx_update_import.hpp"
#include "quickboot.hpp"
#include <fstream>
#include <regex>
#include <set>
namespace voyemu {
namespace {
bool sha(std::string_view value) {
    return sha256_text(value);
}

void ordinary_qnx(const std::filesystem::path &path) {
    for (const auto &at : os::ancestors(path)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
            throw VoyemuError("QNX_ORIGINALS_LINK", "linked input ancestor rejected");
        }
    }
}

Json native_qnx_originals(const std::filesystem::path &root, std::string_view profile,
                          const Json &config, const std::filesystem::path &definition,
                          const std::string &definition_pin, const std::filesystem::path &archive,
                          const std::set<std::string> &cached, std::uint64_t reserve_bytes) {
    const auto archive_pin = config.at("update").at("sha256").get<std::string>();

    if (!sha(archive_pin) || sha256_file(archive) != archive_pin) {
        throw VoyemuError("QNX_ORIGINALS_UPDATE_SHA",
                          "owner archive differs from explicit profile pin");
    }

    const auto host = std::string(os::dependency_system);
    const auto manifest_pin = project_file_sha256(root, "src/app/toolchain.json", 256 * 1024);
    const auto codec_definition = project_file_json(root, "src/app/toolchain.json", 256 * 1024)
                                          .at("qnxUpdateCodec")
                                          .at(host);
    const auto relative = std::filesystem::path(codec_definition.at("file").get<std::string>());

    if (relative.empty() || relative.has_root_name() || relative.has_root_directory() ||
        relative.lexically_normal() != relative || relative.generic_string().starts_with("../") ||
        relative == "..") {
        throw VoyemuError("QNX_ORIGINALS_CODEC_PATH", "project-relative installed codec required");
    }

    const auto codec = root / relative;
    ordinary_qnx(codec);
    const auto codec_pin = codec_definition.at("sha256").get<std::string>();

    // deps installs the codec with the Android SDK tools.
    if (!std::filesystem::exists(codec) && !std::filesystem::is_symlink(codec)) {
        throw cli::LocalizedError("QNX_ORIGINALS_CODEC_SHA", "error.deps_missing",
                                  {{"profile", std::string(profile)}});
    }

    if (!sha(codec_pin) || !std::filesystem::is_regular_file(codec) ||
        std::filesystem::hard_link_count(codec) != 1 || sha256_file(codec) != codec_pin) {
        throw VoyemuError("QNX_ORIGINALS_CODEC_SHA",
                          "pinned ready codec required; run voyemu deps");
    }

    const auto temporary = root / ".local/build/tmp";
    ensure_private_directory(temporary);
    const auto stage = temporary / ("native-qnx-originals-" + random_token());
    const auto receipt = import_qnx_update(archive, archive_pin, config.at("originals"), codec,
                                           codec_pin, stage, reserve_bytes);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{stage};

    if (sha256_file(definition) != definition_pin ||
        project_file_sha256(root, "src/app/toolchain.json", 256 * 1024) != manifest_pin) {
        throw VoyemuError("QNX_ORIGINALS_CHANGED", "profile or codec manifest changed");
    }

    const auto directory = root / ".local/profiles" / std::string(profile) / "qnx";
    ordinary_qnx(directory);
    ensure_private_directory(directory);
    std::vector<std::string> missing;

    for (auto it = config.at("originals").begin(); it != config.at("originals").end(); ++it) {
        const auto target = directory / it.key();
        ordinary_qnx(target);

        if (cached.contains(it.key())) {
            if (!std::filesystem::is_regular_file(target) ||
                std::filesystem::hard_link_count(target) != 1 ||
                sha256_file(target) != it.value().at("sha256").get<std::string>()) {
                throw VoyemuError("QNX_ORIGINALS_CHANGED", "cached original changed during import");
            }
        } else {
            if (std::filesystem::exists(target)) {
                throw VoyemuError("QNX_ORIGINALS_CHANGED", "new original appeared during import");
            }

            missing.push_back(it.key());
        }
    }

    std::vector<std::string> moved;
    try {
        for (const auto &name : missing) {
            std::filesystem::rename(stage / name, directory / name);
            moved.push_back(name);
        }

        write_private_json(directory / ("update-import-" + random_token() + ".json"), receipt);
    } catch (...) {
        for (auto at = moved.rbegin(); at != moved.rend(); ++at) {
            std::error_code ignored;

            if (std::filesystem::is_regular_file(directory / *at) &&
                sha256_file(directory / *at) ==
                        config.at("originals").at(*at).at("sha256").get<std::string>()) {
                std::filesystem::rename(directory / *at, stage / *at, ignored);
            }
        }

        throw;
    }

    return {{"schema", 1},
            {"profile", profile},
            {"originals_materialized", missing.size()},
            {"cached", false},
            {"native", true},
            {"vehicle_modified", false},
            {"archive_sha256", archive_pin}};
}
} // namespace

Json materialize_qnx_originals(const std::filesystem::path &root, std::string_view profile,
                               const std::filesystem::path &jdk, bool offline,
                               std::uint64_t reserve_bytes) {
    if (reserve_bytes > (20ULL << 30)) {
        throw VoyemuError("QNX_ORIGINALS_RESERVE_BOUND", "reserve exceeds production bound");
    }

    if (!root.is_absolute() || std::filesystem::canonical(root) != root ||
        !valid_profile_name(profile)) {
        throw VoyemuError("QNX_ORIGINALS_ARGUMENT", "canonical root/profile required");
    }

    const auto definition = root / "profiles" / std::string(profile) / "profile-qnx.json";
    const auto config = read_private_json(definition, 128 * 1024);
    // The guest adapter of profile-qnx.json selects the originals its OEM build needs.
    const auto adapter_name = config.value("hardware_adapter", std::string{});
    const auto *adapter = adapter_name.empty() ? nullptr : &qnx_adapter(adapter_name);

    if (adapter && adapter->platform == QnxGuestPlatform::passion) {
        // The five owner-update roles come from the profile; the host image is the one the
        // Passion adapter is built for.
        const auto originals = config.value("originals", Json{});
        const auto update = config.value("update", Json{});
        bool roles = originals.is_object() && originals.size() == 5;

        for (const auto *role : {"mifs_hyp_la.img", "mifs_recovery_hyp_la.img", "ifs2_la.img",
                                 "ifs2_recovery_la.img", "system_la.img"}) {
            const auto entry = roles ? originals.value(role, Json{}) : Json{};
            roles = roles && entry.is_object() && entry.value("file", Json{}) == role &&
                    entry.value("bytes", Json{}).is_number_unsigned() &&
                    entry.value("sha256", Json{}).is_string() &&
                    sha(entry.at("sha256").get<std::string>());
        }

        if (config.value("schema", 0) != 1 || config.value("status", Json{}) != "incomplete" ||
            config.value("hardware_adapter", Json{}) != "sa8155-passion-21480" || !roles ||
            originals.at("mifs_hyp_la.img").at("sha256") !=
                    std::string(qnx_adapter(QnxGuestAdapter::sa8155_passion_21480).hyp_sha256) ||
            originals.at("mifs_hyp_la.img").at("bytes") !=
                    qnx_adapter(QnxGuestAdapter::sa8155_passion_21480).hyp_bytes ||
            !update.is_object() || update.value("source", Json{}) != "owner-update" ||
            !update.value("sha256", Json{}).is_string() ||
            !sha(update.at("sha256").get<std::string>())) {
            throw VoyemuError(
                    "QNX_ORIGINALS_PROFILE",
                    "exact incomplete Passion owner-update five-role descriptor required");
        }

        const auto definition_pin = sha256_file(definition);
        Json epochs = Json::object();

        for (auto it = originals.begin(); it != originals.end(); ++it) {
            const auto file = root / ".local/profiles" / std::string(profile) / "qnx" / it.key();
            ordinary_qnx(file);

            if (!std::filesystem::exists(file)) {
                throw cli::LocalizedError("QNX_ORIGINALS_MISSING", "error.originals_missing",
                                          {{"profile_name", profile_display_name(root, profile)},
                                           {"profile", std::string(profile)},
                                           {"archive", "update.zip"}});
            }

            const auto epoch = cache_file_identity(file);

            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
                std::filesystem::hard_link_count(file) != 1 ||
                std::filesystem::file_size(file) != it.value().at("bytes").get<std::uint64_t>() ||
                sha256_file(file) != it.value().at("sha256").get<std::string>()) {
                throw VoyemuError("QNX_ORIGINALS_CACHED_SHA",
                                  "Passion cached owner original differs; preserved");
            }

            if (cache_file_identity(file) != epoch) {
                throw VoyemuError("QNX_ORIGINALS_CHANGED",
                                  "Passion owner original changed during verification");
            }

            epochs[file.string()] = epoch;
        }

        if (sha256_file(definition) != definition_pin) {
            throw VoyemuError("QNX_ORIGINALS_CHANGED",
                              "Passion profile changed during verification");
        }

        for (auto it = epochs.begin(); it != epochs.end(); ++it) {
            if (cache_file_identity(std::filesystem::path(it.key())) != it.value()) {
                throw VoyemuError("QNX_ORIGINALS_CHANGED",
                                  "Passion owner cache changed during verification");
            }
        }

        return {{"schema", 1},    {"originals_materialized", 0}, {"cached", true},
                {"native", true}, {"vehicle_modified", false},   {"originals_verified", 5}};
    }

    if (config.at("schema") != 1 ||
        (adapter && adapter->platform == QnxGuestPlatform::qinggan11
                 ? !qnx_qinggan11_adapter_originals_match(adapter->adapter, config.at("originals"))
                 : config.at("originals").size() != 4)) {
        throw VoyemuError("QNX_ORIGINALS_PROFILE",
                          "pinned originals of the declared adapter required");
    }

    bool missing = false;
    const auto definition_pin = sha256_file(definition);
    std::set<std::string> cached;
    const std::set<std::string> roles{"hyp_la.img", "ifs2_la.img", "system_la.img",
                                      "ifs2_recovery_la.img"};
    const auto inputs = qnx_originals_directory(root, profile).parent_path();

    for (auto it = config.at("originals").begin(); it != config.at("originals").end(); ++it) {
        if (!roles.contains(it.key()) || it.value().at("file") != it.key() ||
            !sha(it.value().at("sha256").get<std::string>())) {
            throw VoyemuError("QNX_ORIGINALS_ROLE", "fixed pinned IMG roles required");
        }

        const auto file = inputs / "qnx" / it.key();
        ordinary_qnx(file);

        if (std::filesystem::exists(file)) {
            if (!std::filesystem::is_regular_file(file) ||
                std::filesystem::hard_link_count(file) != 1 ||
                std::filesystem::file_size(file) > (4ULL << 30) ||
                sha256_file(file) != it.value().at("sha256").get<std::string>() ||
                (it.value().contains("bytes") &&
                 std::filesystem::file_size(file) != it.value().at("bytes").get<std::uint64_t>())) {
                throw VoyemuError("QNX_ORIGINALS_CACHED_SHA",
                                  "existing owner original differs; preserved");
            }

            cached.insert(it.key());
        } else {
            missing = true;
        }
    }

    if (!missing) {
        return {{"schema", 1}, {"originals_materialized", 0}, {"cached", true}};
    }

    const auto archive = inputs / "update.zip";

    if (!std::filesystem::is_regular_file(archive) || std::filesystem::is_symlink(archive) ||
        std::filesystem::hard_link_count(archive) != 1 ||
        std::filesystem::file_size(archive) > 16ULL << 30) {
        throw cli::LocalizedError("QNX_ORIGINALS_MISSING", "error.originals_missing",
                                  {{"profile_name", profile_display_name(root, profile)},
                                   {"profile", std::string(profile)},
                                   {"archive", "update.zip"}});
    }

    if (config.contains("update")) {
        return native_qnx_originals(root, profile, config, definition, definition_pin, archive,
                                    cached, reserve_bytes);
    }

    const auto archive_pin = sha256_file(archive);
    const auto tools = ensure_java_preparation_tools(root, jdk, offline);
    const auto cp = java_preparation_classpath(root, tools.runtime);
#ifdef _WIN32
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    const auto java = tools.runtime.jdk / "bin" / ("java" + suffix),
               stage = root / ".local/build/tmp" / ("qnx-originals-transport-" + random_token());
    ensure_private_directory(stage);

    if (sha256_file(java) != tools.runtime.java_sha256) {
        throw VoyemuError("QNX_ORIGINALS_JAVA", "installed JDK changed");
    }

    write_private_json(stage / "request.json", {{"schema", 1},
                                                {"root", root.string()},
                                                {"profile", profile},
                                                {"profile_sha256", definition_pin},
                                                {"update_sha256", archive_pin}});
    ProcessSpec command;
    command.executable = java;
    command.directory = root;
    command.arguments = {"-Xmx1g",
                         "-Djava.io.tmpdir=" + stage.string(),
                         "-cp",
                         cp,
                         "com.voyemu.prepare.QnxOriginalsMain",
                         (stage / "request.json").string(),
                         (stage / "result.json").string()};
    command.environment = {{"JDK_JAVA_OPTIONS", ""},
                           {"JAVA_TOOL_OPTIONS", ""},
                           {"_JAVA_OPTIONS", ""},
                           {"CLASSPATH", ""},
                           {"TMPDIR", stage.string()}};
    const auto run = execute(command, 600000, 1 << 20);
    std::ofstream log(stage / "helper.log", std::ios::binary);
    log << run.output;
    log.close();

    if (run.timed_out || run.exit_code) {
        throw VoyemuError("QNX_ORIGINALS_FAILED",
                          "originals preserved; inspect " + (stage / "helper.log").string());
    }

    if (sha256_file(java) != tools.runtime.java_sha256 ||
        sha256_file(definition) != definition_pin || sha256_file(archive) != archive_pin ||
        java_preparation_classpath(root, tools.runtime) != cp) {
        throw VoyemuError("QNX_ORIGINALS_CHANGED", "owner inputs/tools changed");
    }

    const auto result = read_private_json(stage / "result.json", 128 * 1024);

    if (result.at("schema") != 1 || result.at("profile") != profile ||
        result.at("vehicle_modified") != false) {
        throw VoyemuError("QNX_ORIGINALS_RESPONSE", "invalid import result");
    }

    return result;
}

Json capture_vehicle_inputs(const std::filesystem::path &root, std::string_view profile,
                            const std::string &serial, const std::filesystem::path &jdk,
                            bool offline) {
    if (!root.is_absolute() || std::filesystem::canonical(root) != root ||
        !valid_profile_name(profile) || offline || serial.starts_with("emulator-") ||
        !std::regex_match(serial, std::regex(R"(^[A-Za-z0-9][A-Za-z0-9_.:-]*$)"))) {
        throw VoyemuError("VEHICLE_CAPTURE_ARGUMENT",
                          "canonical root and explicit physical serial required");
    }

    const auto directory = root / "profiles" / std::string(profile),
               capture = directory / "capture.json", draft = directory / "profile.json";

    for (const auto &file : {capture, draft}) {
        if (!file.is_absolute() || std::filesystem::is_symlink(file) ||
            !std::filesystem::is_regular_file(file) ||
            std::filesystem::file_size(file) > 128 * 1024) {
            throw VoyemuError("VEHICLE_CAPTURE_PROFILE",
                              "bounded capture.json and profile.json required");
        }
    }

    const auto capture_pin = sha256_file(capture), draft_pin = sha256_file(draft);
    install_native_dependencies(root, offline);
    const auto tools = ensure_java_preparation_tools(root, jdk, offline);
    const auto classpath = java_preparation_classpath(root, tools.runtime);
#ifdef _WIN32
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    const auto adb = tools.sdk / "platform-tools" / ("adb" + suffix),
               java = tools.runtime.jdk / "bin" / ("java" + suffix),
               stage = root / ".local/build/tmp" / ("vehicle-capture-transport-" + random_token());
    ensure_private_directory(stage);

    if (sha256_file(java) != tools.runtime.java_sha256) {
        throw VoyemuError("VEHICLE_CAPTURE_JAVA", "installed JDK changed");
    }

    const auto adb_pin = sha256_file(adb);
    write_private_json(stage / "request.json", {{"schema", 1},
                                                {"root", root.string()},
                                                {"profile", profile},
                                                {"serial", serial},
                                                {"adb", adb.string()},
                                                {"adb_sha256", adb_pin},
                                                {"capture_sha256", capture_pin},
                                                {"draft_sha256", draft_pin}});
    ProcessSpec command;
    command.executable = java;
    command.directory = root;
    command.arguments = {"-Xmx1g",
                         "-Djava.io.tmpdir=" + stage.string(),
                         "-cp",
                         classpath,
                         "com.voyemu.prepare.VehicleCaptureMain",
                         (stage / "request.json").string(),
                         (stage / "result.json").string()};
    command.environment = {{"JDK_JAVA_OPTIONS", ""},   {"JAVA_TOOL_OPTIONS", ""},
                           {"_JAVA_OPTIONS", ""},      {"CLASSPATH", ""},
                           {"TMPDIR", stage.string()}, {"ANDROID_TMP", stage.string()}};
    const auto result = execute(command, 3600000, 1 << 20);
    std::ofstream log(stage / "helper.log", std::ios::binary);
    log << result.output;
    log.close();

    if (result.timed_out || result.exit_code != 0) {
        throw VoyemuError("VEHICLE_CAPTURE_FAILED",
                          "partial receipt retained; inspect " + (stage / "helper.log").string());
    }

    if (sha256_file(java) != tools.runtime.java_sha256 ||
        java_preparation_classpath(root, tools.runtime) != classpath ||
        sha256_file(adb) != adb_pin || sha256_file(capture) != capture_pin) {
        throw VoyemuError("VEHICLE_CAPTURE_CHANGED", "capture tools/manifest changed");
    }

    const auto response = read_private_json(stage / "result.json", 128 * 1024);

    if (response.at("schema") != 1 || response.at("profile") != profile ||
        response.at("vehicle_modified") != false || response.at("runtime_accepted") != false) {
        throw VoyemuError("VEHICLE_CAPTURE_RESPONSE", "bounded capture result required");
    }

    return response;
}
} // namespace voyemu
