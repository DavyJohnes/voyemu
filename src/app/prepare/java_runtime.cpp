#include "java_runtime.hpp"
#include "e2fs_helper.hpp"
#include "progress.hpp"
#include "project_files.hpp"
#include "sdk_archive.hpp"
namespace voyemu {
namespace {
#ifdef _WIN32
constexpr char executable_suffix[] = ".exe";
#else
constexpr char executable_suffix[] = "";
#endif
} // namespace

std::filesystem::path java_developer_jdk(const std::filesystem::path &root) {
#ifdef _WIN32
    const auto host = "windowsBuild";
#elif defined(__APPLE__)
    const auto host = "macosBuild";
#else
    const auto host = "linuxBuild";
#endif
    auto home = root / embedded_toolchain_manifest()
                               .at("optionalTools")
                               .at(host)
                               .at("jdk")
                               .at("directory")
                               .get<std::string>();
#ifdef __APPLE__
    home /= "Contents/Home";
#endif

    return home;
}

void build_java_runtimes(const std::filesystem::path &root, const std::filesystem::path &output) {
    if (!project_source_checkout(root)) {
        throw VoyemuError("JAVA_RUNTIME_ASSET_MISSING",
                          "the java-runtime asset cannot be downloaded and this installation has "
                          "no project sources for its recipe");
    }

    const auto jdk = java_developer_jdk(root);
    const auto javac = jdk / "bin" / (std::string("javac") + executable_suffix);

    if (!std::filesystem::is_regular_file(javac)) {
        throw VoyemuError("JAVA_RUNTIME_ASSET_MISSING",
                          "the java-runtime asset cannot be downloaded and the pinned JDK of the "
                          "developer tools that links it is not installed: " +
                                  jdk.string());
    }

    const auto classes = output / "recipe";
    ProcessSpec spec;
    spec.executable = javac;
    spec.arguments = {"--release",
                      "25",
                      "-d",
                      classes.string(),
                      (root / "src/prepare/java/com/voyemu/prepare/Json.java").string(),
                      (root / "src/prepare/JavaRuntimeBuild.java").string()};
    spec.directory = root;
    auto result = execute_artifact_preparation(spec, 300000, 1 << 20);

    if (result.timed_out || result.exit_code != 0) {
        throw VoyemuError("JAVA_RUNTIME", "runtime recipe javac failed: " + result.output);
    }

    spec.executable = jdk / "bin" / (std::string("java") + executable_suffix);
    spec.arguments = {"-cp",
                      classes.string(),
                      "com.voyemu.prepare.JavaRuntimeBuild",
                      root.string(),
                      (root / ".local/toolchains/downloads/java-runtime").string(),
                      (output / "asset").string()};
    result = execute_artifact_preparation(spec, 1200000, 1 << 20);

    if (result.timed_out || result.exit_code != 0) {
        throw VoyemuError("JAVA_RUNTIME", "runtime recipe failed: " + result.output);
    }
}

std::filesystem::path
install_java_runtime(const std::filesystem::path &root, const Json &pins, bool offline,
                     std::uint64_t reserve, const DownloadTransport &transport,
                     const std::function<void(const std::filesystem::path &)> &build) {
    const std::string host = e2fs_helper_host(), asset = "java-runtime-" + host + ".zip";
    const auto &pin = pins.at("assets").at(host);
    const auto sha = pin.at("sha256").get<std::string>();
    const auto cache = root / ".local/toolchains/java-runtime", folder = cache / sha;
    const auto java = folder / "bin" / (std::string("java") + executable_suffix);

    if (std::filesystem::exists(std::filesystem::symlink_status(folder))) {
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(java))) {
            throw VoyemuError("JAVA_RUNTIME",
                              "installed runtime lacks bin/java: " + folder.string());
        }

        return java;
    }

    ensure_private_directory(cache);
    const auto job = root / ".local/build/jobs" / ("java-runtime-" + random_token());
    struct Job {
        std::filesystem::path path;

        ~Job() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } owned{job};
    const DownloadPin download{pin.at("url").get<std::string>(),
                               sha,
                               256ULL << 20,
                               DownloadEncoding::raw,
                               asset,
                               "java_runtime",
                               pins.value("version", std::string{})};
    const auto names = download_names(download);
    std::filesystem::path archive;

    try {
        archive = download_pinned(download, root / ".local/toolchains/downloads/java-runtime",
                                  offline, reserve, transport)
                          .file;
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();

        // Only an asset that is not published falls back; a network failure stays an error.
        if (offline || !download_unpublished(std::current_exception())) {
            throw;
        }

        logging::write(logging::Level::debug, "prepare", "java runtime download failed",
                       {{"host", host}, {"sha256", sha}, {"download", message}});
        progress::report("recipe", names);
        ensure_private_directory(job);

        if (build) {
            build(job);
        } else {
            build_java_runtimes(root, job);
        }

        archive = job / "asset" / asset;

        if (!std::filesystem::is_regular_file(archive) || sha256_file(archive) != sha) {
            throw VoyemuError("JAVA_RUNTIME", "runtime built from the recipe differs from its pin");
        }
    }

    const auto staging = cache / (".part-" + random_token());

    try {
        progress::report("extract", names);
        extract_sdk_archive(archive, sha, staging, reserve, 1ULL << 30, true);

        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(
                    staging / "bin" / (std::string("java") + executable_suffix)))) {
            throw VoyemuError("JAVA_RUNTIME", "runtime archive lacks bin/java");
        }

        std::filesystem::rename(staging, folder);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        throw;
    }

    return java;
}
} // namespace voyemu
