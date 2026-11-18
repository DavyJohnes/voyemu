#pragma once
#include "check.hpp"
#include "model.hpp"
#include <stdexcept>
namespace voyemu {
inline std::string can_model_candidate_hash(const Json &c, const std::string &profile,
                                            const std::string &profile_sha,
                                            const std::string &system_sha) {
    auto need = [](bool v) {
        if (!v) {
            throw VoyemuError("CAN_MODEL_UNAVAILABLE", "invalid cold installed helper proof");
        }
    };
    auto pin = [](const std::string &v) {
        return sha256_text(v);
    };

    need(pin(profile_sha) && pin(system_sha) && c.is_object() && c.value("schema", 0) == 1 &&
         c.value("kind", std::string()) == "android-candidate" &&
         c.value("profile", std::string()) == profile &&
         c.value("profile_sha256", std::string()) == profile_sha && c.value("stopped", false) &&
         c.value("prepared_VM", false) && c.value("cold_boot_verified", false) &&
         c.contains("old_machine_modified") && c.at("old_machine_modified") == false);
    need(c.at("android").at("images").at("system.img").at("sha256") == system_sha &&
         c.at("installed").is_array() && c.at("checks").is_array());
    const std::string artifact = "component:can-model-observer",
                      path = "/system/bin/voyemu-can-model-observer";
    std::string sha;
    unsigned installs = 0, checks = 0;

    for (const auto &row : c.at("installed")) {
        if (row.value("artifact", std::string()) == artifact ||
            row.value("target", std::string()) == path) {
            ++installs;
            need(row.value("artifact", std::string()) == artifact &&
                 row.value("target", std::string()) == path &&
                 row.value("mode", std::string()) == "0755" && row.contains("mutable") &&
                 row.at("mutable") == false && row.value("package", std::string()).empty());
            sha = row.value("sha256", std::string());
            need(pin(sha));
        }
    }

    need(installs == 1);

    for (const auto &row : c.at("checks")) {
        if (row.value("id", std::string()) == "artifact:" + artifact + ":" + path) {
            ++checks;
            need(row.value("passed", false) && row.value("expected", std::string()) == sha &&
                 row.value("actual", std::string()) == sha);
        }
    }

    need(checks == 1);

    return sha;
}
} // namespace voyemu
