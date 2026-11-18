#pragma once
#include "model.hpp"
#include "process.hpp"
#include "program_modes.hpp"
#include <set>
namespace voyemu {
inline void audit_launch_pins(const Json &specification) {
    if (specification.value("schema", 0) != 1 || !specification.contains("pins") ||
        !specification.at("pins").is_array()) {
        throw VoyemuError("LAUNCH_SCHEMA", "prepared configuration with input pins required");
    }

    std::set<std::string> current_programs;
    auto own_backend = [&](const Json &backend) {
        if (backend.contains("executable")) {
            const auto program = backend.at("executable").get<std::string>();

            if (backend.value("backend", std::string{}) == "voyemu" ||
                builtin_program_name(program)) {
                current_programs.insert(program);
            }
        }
    };
    own_backend(specification);

    for (const auto *key : {"frontend", "graphics"}) {
        if (specification.contains(key)) {
            own_backend(specification.at(key));
        }
    }

    auto pins = [&](const Json &source) {
        if (!source.contains("pins")) {
            return;
        }

        for (const auto &pin : source.at("pins")) {
            const auto file = std::filesystem::path(pin.at("path").get<std::string>());

            if (current_programs.contains(file.string())) {
                continue;
            }

            const auto expected = pin.at("sha256").get<std::string>();

            if (!file.is_absolute() || expected.size() != 64 ||
                !std::filesystem::is_regular_file(file) || sha256_file(file) != expected) {
                throw VoyemuError("LAUNCH_PIN",
                                  "input integrity mismatch: " + file.filename().string());
            }
        }
    };
    pins(specification);

    if (specification.contains("frontend")) {
        pins(specification.at("frontend"));
    }
}
} // namespace voyemu
