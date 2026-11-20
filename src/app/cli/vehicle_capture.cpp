#include "vehicle_capture.hpp"
#include "check.hpp"
#include "deps.hpp"
#include "error.hpp"
#include "java_toolchain.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "project_files.hpp"
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

    if (profile == "passion-11-russian-20260403-2.1.4.80") {
        const Json
                expected_originals = Json::parse(
                        R"PASSION({"mifs_hyp_la.img":{"file":"mifs_hyp_la.img","bytes":2254664,"sha256":"1e7c487aae4f4353621f8d0301b352cc974e9037d114ff3ec2556d048512ad28","acquisition":{"kind":"raw-tar-member","member":"otaPack/mifs_hyp_la.img"}},"mifs_recovery_hyp_la.img":{"file":"mifs_recovery_hyp_la.img","bytes":2330096,"sha256":"8376e6340f719d8a4b6e572fee7a675dd7bd6e88e14c589ba2eba27ee60c239b","acquisition":{"kind":"raw-tar-member","member":"otaPack/mifs_recovery_hyp_la.img"}},"ifs2_la.img":{"file":"ifs2_la.img","bytes":200925932,"sha256":"1e942ab83e59ab78ff0096f474d3677ba7e83f6a00f86a1ebe9f0ae755a3e157","acquisition":{"kind":"raw-tar-member","member":"otaPack/ifs2_la.img"}},"ifs2_recovery_la.img":{"file":"ifs2_recovery_la.img","bytes":32080412,"sha256":"1635c921b73544411a08028005b906e677d029c047d2a6e0a041598c9847709c","acquisition":{"kind":"raw-tar-member","member":"otaPack/ifs2_recovery_la.img"}},"system_la.img":{"file":"system_la.img","bytes":2684354560,"sha256":"9e6395255f9bc51d926fd5ab604612d779de93da59ba71832ee580c8534f42ec","acquisition":{"kind":"raw-tar-member","member":"otaPack/system_la.img"}}})PASSION"),
                expected_update = Json::parse(
                        R"PASSION({"source":"owner-update","container":"zip-stored-tgz","sha256":"a1b651223453ed23d1835a70a5cac64c43247a21165ccd88d56a21cc3215ddb9","nested_member":"h53y-cdc-8155-H53_8155_OTA_1.4.0_release-soc_ota-user-secure-2.1.4.80-20260403174306.tgz","nested_sha256":"00d41ae4ff1283040e97276d3b287daf381b2ee9843a7b0f3977ec5b7a06eae4"})PASSION");

        if (!config.contains("schema") || config.at("schema") != 1 || !config.contains("status") ||
            config.at("status") != "incomplete" || !config.contains("hardware_adapter") ||
            config.at("hardware_adapter") != "sa8155-passion-21480" ||
            !config.contains("originals") || config.at("originals") != expected_originals ||
            !config.contains("update") || config.at("update") != expected_update) {
            throw VoyemuError(
                    "QNX_ORIGINALS_PROFILE",
                    "exact incomplete Passion owner-update five-role descriptor required");
        }

        const auto definition_pin = sha256_file(definition);
        Json epochs = Json::object();

        for (auto it = expected_originals.begin(); it != expected_originals.end(); ++it) {
            const auto file = root / ".local/profiles" / std::string(profile) / "qnx" / it.key();
            ordinary_qnx(file);

            if (!std::filesystem::exists(file)) {
                throw VoyemuError(
                        "QNX_ORIGINALS_MISSING",
                        "run voyemu fetch PROFILE --source=OWNER_ZIP for the pinned Passion owner archive");
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

    if (profile == "free-11-chinese-20250710-6.11.1" &&
        (config.value("hardware_adapter", std::string{}) != "sa8155-qinggan11-611" ||
         !qnx_qinggan11_611_originals_match(config.at("originals")))) {
        throw VoyemuError("QNX_ORIGINALS_PROFILE", "exact 6.11 adapter and originals required");
    }

    if (config.at("schema") != 1 ||
        (config.at("originals").size() != 4 &&
         !(config.value("hardware_adapter", std::string{}) == "sa8155-qinggan11-731" &&
           qnx_qinggan11_originals_match(config.at("originals"))) &&
         !(profile == "free-11-chinese-20250710-6.11.1" &&
           config.value("hardware_adapter", std::string{}) == "sa8155-qinggan11-611" &&
           qnx_qinggan11_611_originals_match(config.at("originals"))))) {
        throw VoyemuError("QNX_ORIGINALS_PROFILE", "four pinned originals required");
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
        throw VoyemuError(
                "QNX_ORIGINALS_MISSING",
                "provide the four profile IMG inputs or own update.zip under .local/profiles/PROFILE");
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
