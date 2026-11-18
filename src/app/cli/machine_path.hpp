#pragma once
#include "cli_messages.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
namespace voyemu {
struct MachinePathSelection {
    std::filesystem::path seed;
    std::string profile;
};

inline MachinePathSelection inspect_machine_path(const std::filesystem::path &value) {
    const auto seed = std::filesystem::absolute(value).lexically_normal();

    for (const auto &path : os::ancestors(seed)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) {
            throw cli::LocalizedError("MACHINE_PATH", "error.machine_linked");
        }
    }

    if (!std::filesystem::is_directory(seed) || std::filesystem::canonical(seed) != seed) {
        throw cli::LocalizedError("MACHINE_PATH", "error.machine_missing");
    }

    const auto file = seed / "runtime.json";

    if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file) ||
        std::filesystem::hard_link_count(file) != 1) {
        throw cli::LocalizedError("MACHINE_PATH", "error.machine_runtime");
    }

    const auto manifest = read_private_json(file, 128 * 1024);
    const auto profile = manifest.value("profile", std::string{});

    if (!valid_profile_name(profile) ||
        manifest.value("kind", std::string{}) != "portable-guest-runtime" ||
        manifest.value("schema", 0) != 1) {
        throw cli::LocalizedError("MACHINE_PATH", "error.machine_manifest");
    }

    return {seed, profile};
}

inline std::string machine_path_name(const std::filesystem::path &root,
                                     const MachinePathSelection &selected) {
    (void)root;
    const auto name = selected.seed.filename().string();

    if (!valid_profile_name(name)) {
        throw cli::LocalizedError("MACHINE_NAME", "error.machine_name");
    }

    return name;
}

inline bool machine_selector_matches(const std::filesystem::path &root,
                                     const MachinePathSelection &selected,
                                     std::string_view selector) {
    return selector == machine_path_name(root, selected) ||
           (selector.empty() && selected.seed == root / "machines" / selected.profile);
}

inline bool machine_session_matches(const std::filesystem::path &root,
                                    const MachinePathSelection &selected, const Json &status) {
    if (status.contains("machine_seed")) {
        return status.at("machine_seed").is_string() &&
               std::filesystem::path(status.at("machine_seed").get<std::string>()) == selected.seed;
    }

    return !status.contains("machine") && selected.seed == root / "machines" / selected.profile;
}
} // namespace voyemu
