#pragma once
#include "error.hpp"
#include "process.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
namespace voyemu {
inline std::string a9_policy_identity(std::string_view platform, std::string_view mapping) {
    if (platform.empty() || mapping.empty()) {
        throw VoyemuError("SELINUX_POLICY", "complete platform and mapping required");
    }

    return sha256_bytes(std::string(platform) + std::string(mapping)) + "\n";
}

inline void a9_policy_binary(std::string_view bytes, unsigned version) {
    const auto word = [&](std::size_t at) {
        if (at + 4 > bytes.size()) {
            throw VoyemuError("SELINUX_POLICY", "truncated binary");
        }

        unsigned value = 0;

        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<unsigned char>(bytes[at + i]) << (8 * i);
        }

        return value;
    };

    if (bytes.size() < 24 || bytes.size() > 16 * 1024 * 1024 || word(0) != 0xf97cff8c ||
        word(4) != 8 || bytes.substr(8, 8) != "SE Linux" || word(16) != version || version < 30 ||
        version > 33) {
        throw VoyemuError("SELINUX_POLICY", "compiled policy header differs");
    }
}

inline void a9_policy_required_names(std::string_view bytes) {
    for (std::string_view name :
         {"can_early_inputs", "can_early_inputs_exec", "can_early_input_file",
          "can_early_data_file", "can_early_token_dir", "vdh_device", "can_early_marker_file",
          "can_early_ready_prop", "can_simulator", "can_simulator_exec", "can_simulator_fixture",
          "can_device", "can_simulator_device", "can_simulator_socket",
          "can_simulator_observe_socket", "voyemu_canbus_service", "voyemu_oem_service"}) {
        bool found = false;

        for (auto at = bytes.find(name); at != std::string_view::npos;
             at = bytes.find(name, at + 1)) {
            if (at < 16) {
                continue;
            }

            unsigned length = 0;

            for (unsigned i = 0; i < 4; ++i) {
                length |= static_cast<unsigned char>(bytes[at - 16 + i]) << (8 * i);
            }

            if (length == name.size()) {
                found = true;
                break;
            }
        }

        if (!found) {
            throw VoyemuError("SELINUX_POLICY",
                              "compiled adapted symbol missing: " + std::string(name));
        }
    }
}
} // namespace voyemu
