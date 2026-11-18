#pragma once
#include "machine_path.hpp"
#include "profile_runtime.hpp"
namespace voyemu {
inline void append_start_machine_identity(Json &output, const Json &status,
                                          const MachineIdentity &machine,
                                          const std::filesystem::path &owned_seed) {
    if (machine.selected) {
        for (const auto *key : {"machine", "machine_seed", "machine_manifest_sha256"}) {
            output[key] = status.at(key);
        }

        return;
    }

    if (owned_seed.empty()) {
        return;
    }

    const auto selected = inspect_machine_path(owned_seed);
    output["machine"] = "";
    output["machine_seed"] = selected.seed.string();
    output["machine_manifest_sha256"] = sha256_file(selected.seed / "runtime.json");
}
} // namespace voyemu
