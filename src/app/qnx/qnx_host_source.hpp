#pragma once
#include "check.hpp"
#include "download.hpp"
#include "owner_lock.hpp"
#include "process.hpp"
#include "qnx_host_build_diagnostics.hpp"
#include <chrono>
#include <fstream>
#include <map>
#include <thread>
#include <vector>
// The parts of the macOS and Linux engine source builds that do not depend on the host: the
// engine directory and its receipt, pinned downloads and the logged build steps. Each recipe keeps
// only its toolchain and build steps.
namespace voyemu {
// Writes value to path.
inline void qnx_host_text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path);

    file << value;
    Check{"QNX_HOST"}(bool(file), "write failed: " + path.string());
}

// The engine that the source recipe component builds on platform ("macos" or "linux"), installed
// under .local/toolchains in a directory named by the recipe pin.
struct QnxHostSourceEngine {
    const Json &component;
    std::filesystem::path root;
    std::string pin;
    std::filesystem::path parent, target, binary;

    QnxHostSourceEngine(const std::filesystem::path &root_directory, const Json &recipe,
                        const std::string &platform)
        : component(recipe), root(root_directory), pin(sha256_bytes(recipe.dump())),
          parent(root / ".local/toolchains"),
          target(parent / ("qnx-host-" + platform + "-11.1.2-" + pin.substr(0, 12))),
          binary(target / "qemu-system-aarch64") {}

    Json result(bool cached) const {
        return Json{{"schema", 1},
                    {"ready", true},
                    {"cached", cached},
                    {"executable", binary.string()},
                    {"directory", target.string()},
                    {"version", component.at("version")},
                    {"guest_verified", false}};
    }

    // Whether the engine is installed; an installed engine must match its receipt.
    bool installed() const {
        constexpr Check require{"QNX_HOST"};

        if (!std::filesystem::exists(target)) {
            return false;
        }

        require(std::filesystem::is_regular_file(std::filesystem::symlink_status(binary)) &&
                        std::filesystem::hard_link_count(binary) == 1,
                "ordinary installed engine required");
        const auto receipt = read_private_json(target / "qnx-host.json");
        require(receipt.at("component_sha256") == pin &&
                        receipt.at("executable_sha256") == sha256_file(binary),
                "installed engine differs from receipt");

        return true;
    }

    // Holds the installation of every source-built engine of this checkout.
    std::unique_ptr<os::FileLock> lock() const {
        ensure_private_directory(parent);
        auto held = owner_lock(parent / "qnx-host.lock");
        Check{"QNX_HOST"}(held->held(), "installation already owned");

        return held;
    }

    // The pinned download of spec, at most 512 MiB, in the shared engine download cache.
    std::filesystem::path download(const Json &spec, bool offline, std::uint64_t reserve) const {
        return download_pinned({spec.at("url").get<std::string>(),
                                spec.at("sha256").get<std::string>(), 512ULL << 20},
                               root / ".local/toolchains/downloads/qnx-host", offline, reserve)
                .file;
    }

    // Records the receipt of executable, already copied into payload, and publishes payload as the
    // engine directory.
    Json publish(const std::filesystem::path &payload,
                 const std::filesystem::path &executable) const {
        write_private_json(payload / "qnx-host.json",
                           {{"schema", 1},
                            {"component_sha256", pin},
                            {"executable_sha256", sha256_file(executable)},
                            {"system_only_runtime", true},
                            {"recipe", component},
                            {"guest_verified", false}});
        Check{"QNX_HOST"}(!std::filesystem::exists(target), "destination appeared during build");
        std::filesystem::rename(payload, target);

        return result(false);
    }
};

// The build steps of one source build in a private stage below .local/build/tmp. Each step logs
// to its own numbered file; a step longer than five minutes runs bounded by its deadline, the disk
// reserve and the log size; a failed build keeps bounded diagnostics in .local/build.
class QnxHostSourceSteps {
public:
    std::map<std::string, std::string> env;

    QnxHostSourceSteps(const std::filesystem::path &root, std::uint64_t reserve,
                       int default_timeout, std::size_t output_limit)
        : QnxHostSourceSteps(root, reserve, default_timeout, output_limit, random_token()) {}

    const std::filesystem::path &stage() const {
        return stage_;
    }

    std::string run(const std::filesystem::path &program, std::vector<std::string> args,
                    const std::filesystem::path &cwd) {
        return run(program, std::move(args), cwd, default_timeout_);
    }

    std::string run(const std::filesystem::path &program, std::vector<std::string> args,
                    const std::filesystem::path &cwd, int timeout) {
        constexpr Check require{"QNX_HOST"};
        ProcessSpec spec;
        spec.executable = program;
        spec.arguments = std::move(args);
        spec.directory = cwd;
        spec.environment = env;
        spec.output = stage_ / (std::to_string(++step_) + ".log");

        if (timeout > 300000) {
            auto child = ChildProcess::launch(spec);
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(timeout);

            while (child.alive()) {
                require(std::chrono::steady_clock::now() < deadline, "build deadline exceeded");
                require(std::filesystem::space(stage_).available >= reserve_,
                        "build disk reserve reached");
                require(!std::filesystem::exists(spec.output) ||
                                std::filesystem::file_size(spec.output) < (16ULL << 20),
                        "build log exceeds bound");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            require(child.exit_code() == 0,
                    "build failed; diagnostics: " + cleanup_.logs().string());

            return std::string{};
        }

        const auto response = execute(spec, timeout, output_limit_);
        qnx_host_text(spec.output, response.output);
        require(!response.timed_out && response.exit_code == 0,
                "build step " + std::to_string(step_) +
                        " failed; diagnostics: " + cleanup_.logs().string());

        return response.output;
    }

private:
    QnxHostSourceSteps(const std::filesystem::path &root, std::uint64_t reserve,
                       int default_timeout, std::size_t output_limit, const std::string &token)
        : stage_(private_stage(root / ".local/build/tmp" / ("qnx-host-" + token))),
          cleanup_(stage_, root / ".local/build" / ("qnx-host-" + token)), reserve_(reserve),
          default_timeout_(default_timeout), output_limit_(output_limit) {}

    static std::filesystem::path private_stage(std::filesystem::path path) {
        ensure_private_directory(path);

        return path;
    }

    std::filesystem::path stage_;
    // Preserve bounded diagnostics on failure; expanded trees are always reclaimed.
    QnxHostBuildStage cleanup_;
    std::uint64_t reserve_;
    int default_timeout_;
    std::size_t output_limit_;
    unsigned step_ = 0;
};
} // namespace voyemu
