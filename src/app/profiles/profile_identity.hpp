#pragma once
#include "model.hpp"
#include <string>
#include <string_view>

namespace voyemu {
// Logical identity owns selection and state; the embedded reference selects guest adapters.
inline std::string runtime_profile_reference(const Json &definition,
                                             std::string_view verified_profile = {}) {
    if (!definition.is_object()) {
        return {};
    }

    const auto id = definition.value("id", std::string(verified_profile));

    if (id.empty() || (!verified_profile.empty() && id != verified_profile)) {
        return {};
    }

    if (!definition.contains("capture") || !definition.at("capture").is_object()) {
        return id;
    }

    const auto &capture = definition.at("capture");

    if (!capture.contains("referenceProfile")) {
        return id;
    }

    if (!capture.contains("profile") || capture.at("profile") != id ||
        !capture.at("referenceProfile").is_string()) {
        return {};
    }

    const auto reference = capture.at("referenceProfile").get<std::string>();

    return reference.empty() ? std::string{} : reference;
}
} // namespace voyemu
