#pragma once
#include "check.hpp"
#include "model.hpp"
#include <stdexcept>
namespace voyemu {
struct AndroidSystemBinding {
    std::filesystem::path raw, overlay;
};

inline AndroidSystemBinding android_system_binding(const Json &specification) {
    const std::filesystem::path machine = specification.at("machine").get<std::string>();

    if (!specification.contains("system_binding")) {
        return {std::filesystem::path(specification.value("base", (machine / "base").string())) /
                        "system.img",
                machine / "system.img.qcow2"};
    }

    const auto need = [](bool valid) {
        if (!valid) {
            throw VoyemuError("ANDROID_SYSTEM_BINDING",
                              "exact API30 versioned system binding required");
        }
    };
    const auto &binding = specification.at("system_binding");
    need(specification.value("api", 0) == 30 && binding.is_object() && binding.size() == 3 &&
         binding.contains("raw") && binding.contains("overlay") && binding.contains("sha256"));
    need(binding.at("raw").is_string() && binding.at("overlay").is_string() &&
         binding.at("sha256").is_string());
    const auto sha = binding.at("sha256").get<std::string>();
    need(sha256_text(sha));
    const std::filesystem::path raw = binding.at("raw").get<std::string>(),
                                overlay = binding.at("overlay").get<std::string>();
    need(machine.is_absolute() && machine == machine.lexically_normal() && raw.is_absolute() &&
         raw == raw.lexically_normal() && overlay.is_absolute() &&
         overlay == overlay.lexically_normal());
    const auto directory = machine / "system-versions" / sha;
    need(raw == directory / "system.img" && overlay == directory / "system.img.qcow2");

    return {raw, overlay};
}
} // namespace voyemu
