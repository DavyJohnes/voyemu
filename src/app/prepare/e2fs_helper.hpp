#pragma once
#include "deps.hpp"
#include "download.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "project_files.hpp"
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
namespace voyemu {
// Host key of the e2fsprogs.helpers pins in toolchain.json.
inline const char *e2fs_helper_host() {
    return os::host_windows               ? "win32-x64"
           : os::platform_name == "macOS" ? "darwin-arm64"
                                          : "linux-x64";
}

// Builds the helpers of this host into output with src/app/prepare/e2fsprogs_build.cmake of
// project, run by the pinned CMake of the host; the recipe uses the pinned Zig and e2fsprogs
// tarball and runs configure and make, which a Windows host lacks.
inline void build_e2fs_helpers(const std::filesystem::path &project,
                               const std::filesystem::path &output) {
    if (!project_source_checkout(project)) {
        throw VoyemuError("E2FS_HELPER", "the helper recipe needs the project sources of a source "
                                         "checkout; publish the release asset");
    }

    if (os::host_windows) {
        throw VoyemuError(
                "E2FS_HELPER",
                "the helper recipe needs a POSIX shell and make; publish the release asset");
    }

    const bool macos = os::platform_name == "macOS";
    const auto host = macos ? "macosBuild" : "linuxBuild",
               binary = macos ? "CMake.app/Contents/bin/cmake" : "bin/cmake";
    ProcessSpec spec;
    spec.executable = project /
                      embedded_toolchain_manifest()
                              .at("optionalTools")
                              .at(host)
                              .at("cmake")
                              .at("directory")
                              .get<std::string>() /
                      binary;
    spec.arguments = {std::string("-DVOYEMU_E2FS_TARGET=") + e2fs_helper_host(),
                      "-DVOYEMU_E2FS_OUTPUT=" + output.string(), "-P",
                      (project / "src/app/prepare/e2fsprogs_build.cmake").string()};
    spec.directory = project;
    const auto result = execute_artifact_preparation(spec, 1200000, 1 << 20);

    if (result.timed_out || result.exit_code != 0) {
        throw VoyemuError("E2FS_HELPER", "helper recipe failed: " + result.output);
    }
}

// Installs the image helper name of this host, pinned in helpers (e2fsprogs.helpers of
// toolchain.json), into .local/toolchains/e2fs-helpers/<sha256> and returns its path. It downloads
// the CI-built release asset; while that asset cannot be downloaded, build produces the helper
// from the recipe into the given directory instead. Both paths are checked against the same pin,
// and an installed helper is checked on every call.
inline std::filesystem::path
install_e2fs_helper(const std::filesystem::path &root, const Json &helpers, const std::string &name,
                    bool offline, std::uint64_t reserve, const DownloadTransport &transport = {},
                    const std::function<void(const std::filesystem::path &)> &build = {}) {
    const auto &pin = helpers.at(e2fs_helper_host()).at(name);
    const auto sha = pin.at("sha256").get<std::string>();
    const auto folder = root / ".local/toolchains/e2fs-helpers" / sha;

    const auto helper = folder / (name + std::string(os::executable_suffix));
    const auto pinned = [&](const std::filesystem::path &file) {
        return std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) &&
               std::filesystem::hard_link_count(file) == 1 && sha256_file(file) == sha;
    };

    if (std::filesystem::exists(std::filesystem::symlink_status(helper))) {
        if (!pinned(helper)) {
            throw VoyemuError("E2FS_HELPER",
                              "installed helper differs from its pin: " + helper.string());
        }

        return helper;
    }

    ensure_private_directory(folder);
    const auto temporary = folder / (".part-" + random_token());

    try {
        const auto blob = download_pinned({pin.at("url").get<std::string>(), sha, 32ULL << 20},
                                          root / ".local/toolchains/downloads/e2fsprogs", offline,
                                          reserve, transport)
                                  .file;
        std::filesystem::copy_file(blob, temporary);
        logging::write(logging::Level::debug, "prepare", "e2fs helper downloaded",
                       {{"helper", name}, {"sha256", sha}});
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();

        // Only an asset that is not published falls back; a network failure stays an error.
        if (offline || !download_unpublished(std::current_exception())) {
            throw;
        }

        logging::write(logging::Level::info, "prepare", "e2fs helper built from the recipe",
                       {{"helper", name}, {"sha256", sha}, {"download", message}});
        const auto job = root / ".local/build/jobs" / ("e2fs-helper-" + random_token());
        struct Job {
            std::filesystem::path path;

            ~Job() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } owned{job};
        ensure_private_directory(job);

        if (build) {
            build(job);
        } else {
            build_e2fs_helpers(root, job);
        }

        std::filesystem::copy_file(job / (name + "-" + e2fs_helper_host()), temporary);
    }

    std::filesystem::permissions(temporary,
                                 std::filesystem::perms::owner_all |
                                         std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec,
                                 std::filesystem::perm_options::replace);

    if (!pinned(temporary)) {
        std::filesystem::remove(temporary);

        throw VoyemuError("E2FS_HELPER", "copied helper differs from its pin");
    }

    std::filesystem::rename(temporary, helper);

    return helper;
}
} // namespace voyemu
