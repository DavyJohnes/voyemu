#include "cli_commands.hpp"

namespace voyemu::cli {
int test_command(const Arguments &args) {
    if (args.root.empty()) {
        throw VoyemuError("TEST_ROOT", "explicit runtime root required");
    }

    Json input;

    if (!args.request_file.empty() && args.request_file != "-") {
        input = voyemu::read_private_json(args.request_file);
    } else {
        std::string body;
        char c;

        while (std::cin.get(c)) {
            if (body.size() >= 128 * 1024) {
                throw VoyemuError("TEST_ARGUMENT", "JSON input exceeds 128 KiB");
            }

            body += c;
        }

        input = Json::parse(body, nullptr, false);

        if (input.is_discarded()) {
            throw VoyemuError("TEST_ARGUMENT", "malformed JSON input");
        }
    }

    voyemu::api::Api api(args.root, voyemu::os::executable_path());
    auto response = api.call(input);
    std::cout << response.dump() << '\n';

    return voyemu::api::result_exit_code(response);
}

int list_command(const Arguments &args, const Emit &emit) {
    auto bundled = voyemu::embedded_profile_names();
    std::set<std::string> names(bundled.begin(), bundled.end());

    if (!args.root.empty()) {
        for (const auto &name : voyemu::native_profile_names(args.root)) {
            names.insert(name);
        }
    }

    emit(Json(std::vector<std::string>(names.begin(), names.end())));

    return 0;
}

namespace {
// The free space that deps and fetch keep beyond what each download or extraction needs; build
// keeps the larger reserve of machine preparation.
constexpr std::uint64_t preparation_margin = 2ULL << 30, build_margin = 20ULL << 30;
} // namespace

int fetch_command(const Arguments &args, const Emit &emit) {
    auto result = voyemu::fetch_native_inputs(args.root, args.profile, args.offline, args.serial,
                                              preparation_margin, args.jdk, args.update);

    // The summary names the profile by its title.
    result["profile_name"] = voyemu::profile_display_name(args.root, args.profile);
    emit(result);

    return 0;
}

int deps_command(const Arguments &args, const Emit &emit) {
    // An unknown profile fails as in fetch, before any download.
    if (!args.profile.empty() &&
        !std::filesystem::exists(args.root / "profiles" / args.profile / "profile.json")) {
        (void)voyemu::embedded_profile(args.profile);
    }

    // Each step ends with one line: already installed when the step reported nothing, installed
    // after its downloads and extractions.
    progress::Steps steps;
    const auto step = [&steps](std::string_view name, const auto &work, bool new_block = true) {
        steps.begin(new_block);
        auto value = work();
        steps.end(name);

        return value;
    };

    // Hashing every file of an installed SDK takes seconds.
    progress::block();

    if (std::filesystem::exists(args.root / ".local/toolchains/native-sdk")) {
        progress::report("sdk_verify");
    }

    auto result = step(
            "sdk",
            [&] {
                return voyemu::install_native_dependencies(args.root, args.offline,
                                                           preparation_margin);
            },
            false);

    // While the QNX engine of this host can be neither downloaded nor taken from the cache, deps
    // reports it unavailable and machines run without the cluster.
    try {
        result["qnx_host"] = step("qnx_host", [&] {
            return voyemu::install_qnx_host(args.root, args.offline, preparation_margin);
        });
    } catch (const std::runtime_error &error) {
        if (!std::string_view(error.what()).starts_with("QNX_HOST_ASSET_MISSING:")) {
            throw;
        }

        result["qnx_host"] = {{"status", "unavailable"}, {"reason", error.what()}};
    }

    if (!args.profile.empty() &&
        voyemu::portable_runtime_available(args.root, args.profile,
                                           voyemu::machine_identity(args.profile, {}))) {
        result["guest_base"] = step("guest_base", [&] {
            return voyemu::prepare_portable_base(args.root, args.profile, args.offline,
                                                 preparation_margin);
        });
    }

    // The Java runtime of preparation; while its asset is neither published nor cached and no
    // recipe JDK links it, deps reports it unavailable.
    try {
        result["java_runtime"] =
                step("java_runtime", [&] {
                    return voyemu::install_java_runtime(
                            args.root, voyemu::embedded_toolchain_manifest().at("javaRuntime"),
                            args.offline, preparation_margin);
                }).string();
    } catch (const std::runtime_error &error) {
        const std::string_view message = error.what();

        if (!message.starts_with("JAVA_RUNTIME_ASSET_MISSING:") &&
            !message.starts_with("DOWNLOAD_OFFLINE:")) {
            throw;
        }

        result["java_runtime"] = {{"status", "unavailable"}, {"reason", error.what()}};
    }

    // Without --jdk the Java helper runs on the Java runtime; while its asset or that of the helper
    // can be neither downloaded nor built, deps reports preparation unavailable.
    try {
        result["java_preparation"] =
                step("java_tools", [&] {
                    return voyemu::ensure_java_preparation_tools(args.root, args.jdk, args.offline,
                                                                 preparation_margin);
                }).receipt;
        result["preparation_tools_ready"] = true;
    } catch (const std::exception &error) {
        const std::string_view message = error.what();

        if (!args.jdk.empty() || !(message.starts_with("JAVA_HELPER_ASSET_MISSING:") ||
                                   message.starts_with("JAVA_RUNTIME_ASSET_MISSING:") ||
                                   message.starts_with("DOWNLOAD_OFFLINE:"))) {
            throw;
        }

        result["java_preparation"] = {{"status", "unavailable"}, {"reason", error.what()}};
        result["preparation_tools_ready"] = false;
    }

    // The CI-built guest Java and UI observer that build reads; while that asset cannot be
    // downloaded, build compiles them on a JDK.
    steps.begin();
    const auto guest_java = voyemu::install_android_java(
            args.root, voyemu::embedded_toolchain_manifest().at("androidJava"), args.offline,
            preparation_margin);

    // Without the asset install_android_java reported that build compiles the guest Java.
    if (!guest_java.empty()) {
        steps.end("guest_java");
    }

    result["android_java"] = guest_java.empty() ? Json{{"status", "unavailable"}}
                                                : Json(guest_java.string());
    emit(result);

    return 0;
}

int build_command(const Arguments &args, const Emit &emit) {
    // Without an installable QNX engine the default build prepares the machine without the
    // cluster; an explicit --cluster=on stops at the engine.
    const auto unavailable = args.cluster_explicit || !args.multimedia
                                     ? std::string{}
                                     : voyemu::qnx_host_unavailable(args.root);
    const bool cluster = args.cluster && unavailable.empty();
    const auto started = std::chrono::steady_clock::now();

    // An unknown profile fails as in fetch, before any line of the build.
    if (!std::filesystem::exists(args.root / "profiles" / args.profile / "profile.json")) {
        (void)voyemu::embedded_profile(args.profile);
    }

    const auto target = args.root / "machines" / args.machine.name;
    const auto machine = progress::path_text(target);

    // The header names the machine, its subsystems and the space that preparation keeps free.
    progress::block();
    progress::report("build_plan", {{"machine", machine},
                                    {"scope", message(progress::language(),
                                                      cluster ? "progress.scope_both"
                                                              : "progress.scope_android")}});
    progress::report("build_space",
                     {{"required", progress::size_text(build_margin, progress::language())},
                      {"free", progress::size_text(std::filesystem::space(args.root).available,
                                                   progress::language())}});

    // Without hardware acceleration the cold boot of the cluster takes hours.
    if (cluster && voyemu::host_qnx_engine != voyemu::QnxEngine::hvf) {
        progress::report("build_tcg");
    }

    auto result = voyemu::build_profile(
            args.root, args.profile, args.machine, args.offline, args.jdk,
            [] {
                return interrupted.load();
            },
            cluster, args.multimedia, voyemu::os::executable_path(), args.reuse_qnx,
            args.reuse_qnx_sha256, args.reuse_qnx_source);

    if (!unavailable.empty()) {
        result["qnx_host"] = {{"status", "unavailable"},
                              {"reason", "QNX_HOST_ASSET_MISSING: " + unavailable}};
    }

    // The summary names the machine as the header did, and the time the build took.
    result["machine_seed"] = machine;
    result["build_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::steady_clock::now() - started)
                                      .count();
    emit(result);

    return (cluster && args.multimedia
                    ? result.value("profile_ready", false)
                    : result.value("machine_ready", false) &&
                              (args.multimedia ? result.value("android_ready", false)
                                               : result.value("cluster_ready", false)))
                   ? 0
                   : 3;
}

int check_command(const Arguments &args, const Emit &emit) {
    const auto prepared = voyemu::profile_launch_directory(args.root, args.profile, args.machine);
    Json portable, activation = Json::object();
    bool foreign_host = false, current_seed = false;

    if (voyemu::portable_runtime_available(args.root, args.profile, args.machine,
                                           args.machine_path)) {
        portable = voyemu::inspect_portable_runtime(args.root, args.profile, args.machine,
                                                    args.machine_path);
        const auto seed = args.machine_path.empty() ? args.root / "machines" / args.machine.name
                                                    : args.machine_path;

        if (voyemu::ordinary_native_launch_file(prepared / "activation.json")) {
            activation = voyemu::read_private_json(prepared / "activation.json");
            const auto manifest_sha = voyemu::sha256_file(seed / "runtime.json");
            foreign_host = voyemu::portable_configuration_from_other_host(
                    activation, args.root, args.profile, args.machine, seed, manifest_sha);
            current_seed = activation.value("kind", std::string{}) == "runtime-configuration" &&
                           activation.value("profile", std::string{}) == args.profile &&
                           voyemu::canonical_machine_name(
                                   args.profile, activation.value("machine", std::string{})) ==
                                   args.machine.name &&
                           activation.value("seed", std::string{}) == seed.string() &&
                           activation.value("manifest_sha256", std::string{}) == manifest_sha;
        }
    }

    if (!foreign_host) {
        if (voyemu::ordinary_native_launch_file(prepared / "android-launch.json") &&
            !(current_seed && activation.value("android_rebind_pending", false))) {
            auto launch = voyemu::read_private_json(prepared / "android-launch.json");
            voyemu::normalize_ready_android_adb(launch);
            voyemu::audit_launch_pins(launch);
        }

        if ((portable.is_null() || portable.contains("qnx")) &&
            voyemu::ordinary_native_launch_file(prepared / "cluster-launch.json") &&
            !(current_seed && activation.value("cluster_rebind_pending", false))) {
            voyemu::audit_launch_pins(voyemu::read_private_json(prepared / "cluster-launch.json"));
        }
    }

    Json output{{"profile", args.profile},
                {"root", args.root.string()},
                {"native_runtime", true},
                {"android_launch_spec",
                 !portable.is_null() ||
                         voyemu::ordinary_native_launch_file(prepared / "android-launch.json")},
                {"cluster_launch_spec",
                 portable.is_null()
                         ? voyemu::ordinary_native_launch_file(prepared / "cluster-launch.json")
                         : portable.contains("qnx")},
                {"runs_started", false}};

    if (const auto unavailable = voyemu::qnx_host_unavailable(args.root); !unavailable.empty()) {
        output["qnx_host"] = {{"status", "unavailable"},
                              {"reason", "QNX_HOST_ASSET_MISSING: " + unavailable}};
    }

    for (const auto &bundled : voyemu::embedded_profile_catalog()) {
        if (bundled.at("id") == args.profile) {
            output["preparation"] = voyemu::declared_patch_plan(bundled.at("definition"));
        }
    }

    emit(output);

    return output["android_launch_spec"].get<bool>() ? 0 : 3;
}
} // namespace voyemu::cli
