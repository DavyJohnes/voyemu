#pragma once
#include "model.hpp"
#include <set>
#include <vector>
namespace voyemu {
// Metadata-only preflight. Full immutable-file checks still run before activation.
inline void verify_start_root_binding(const std::filesystem::path &program,
                                      const std::vector<Json> &launches, const Json &host) {
    if (!host.is_object() || host.value("graphics", std::string{}) != program.string() ||
        host.contains("frontend")) {
        return;
    }

    std::set<std::string> roots;
    const auto collect = [&](const Json &pins) {
        for (const auto &pin : pins) {
            if (pin.at("path") == program.string()) {
                roots.insert(pin.at("sha256").template get<std::string>());
            }
        }
    };

    for (const auto &launch : launches) {
        collect(launch.at("pins"));

        if (launch.contains("frontend")) {
            collect(launch.at("frontend").at("pins"));
        }
    }

    for (const auto &pin : host.at("pins")) {
        if (pin.at("path") == program.string() &&
            !roots.contains(pin.at("sha256").template get<std::string>())) {
            throw VoyemuError(
                    "HOST_ROOT_LEGACY_BINDING",
                    "historical root program records differ; an authorized binding migration is required");
        }
    }
}
} // namespace voyemu
