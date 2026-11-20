#pragma once
#include "model.hpp"
#include "qnx_adapter.hpp"
#include <optional>
#include <stdexcept>
namespace voyemu {
inline Json qnx_passion_mcu_projection() {
    return {{"hardware_adapter", "sa8155-passion-21480"},
            {"transport", "oem-ipc-topics"},
            {"normalized_fields", 579},
            {"projected_fields", 3},
            {"unsupported_fields", 576},
            {"published_fields", Json::array({"speed", "spd_valid", "soc"})}};
}

// The MCU projection of a qinggan11 adapter at its current scope, or at the given earlier scope.
// Without settings feedback the projection names its adapter and rejects changes of the fields it
// does not project.
inline Json qnx_mcu_projection(QnxGuestAdapter adapter, unsigned fields = 0) {
    const auto &spec = qnx_adapter(adapter);

    if (spec.platform != QnxGuestPlatform::qinggan11) {
        throw VoyemuError("QNX_MCU_PROJECTION", "adapter has no MCU projection");
    }

    const unsigned projected = fields ? fields : spec.mcu_projected_fields;

    if (spec.mcu_settings_feedback) {
        return {{"packet_bytes", 650},
                {"wire_fields", 736},
                {"normalized_fields", 579},
                {"projected_fields", projected},
                {"unsupported_fields", 579 - projected},
                {"unmapped_wire_bits", "preserved-original-each-call"}};
    }

    return {{"hardware_adapter", spec.name},
            {"settings_feedback_supported", false},
            {"unsupported_field_policy", "reject_changes_from_initial_state"},
            {"packet_bytes", 650},
            {"wire_fields", 736},
            {"normalized_fields", 579},
            {"projected_fields", projected},
            {"unsupported_fields", 579 - projected},
            {"unmapped_wire_bits", "preserved-original-each-call"}};
}

// Whether fields is the current or the earlier accepted runtime scope of the adapter.
inline bool qnx_mcu_runtime_scope(const QnxAdapterSpec &spec, std::uint64_t fields) {
    return spec.platform == QnxGuestPlatform::qinggan11 &&
           (fields == spec.mcu_projected_fields ||
            (spec.mcu_previous_projected_fields && fields == spec.mcu_previous_projected_fields));
}

inline Json qnx_mcu_request_projection(std::uint64_t packet_bytes, std::uint64_t wire_fields,
                                       std::uint64_t projected_fields,
                                       bool passion_topics = false) {
    if (passion_topics) {
        if (packet_bytes || wire_fields || projected_fields != 3) {
            throw VoyemuError("QNX_MCU_PROJECTION", "unsupported Passion topic scope");
        }
        return qnx_passion_mcu_projection();
    }

    if (!packet_bytes && !wire_fields && !projected_fields) {
        return nullptr;
    }

    if (packet_bytes == 650 && wire_fields == 736) {
        for (const auto &spec : qnx_adapter_registry) {
            if (qnx_mcu_runtime_scope(spec, projected_fields)) {
                return qnx_mcu_projection(spec.adapter, static_cast<unsigned>(projected_fields));
            }
        }
    }

    throw VoyemuError("QNX_MCU_PROJECTION", "unsupported guest scope");
}

inline std::optional<Json> qnx_mcu_delivery_scope(const Json &ack, const Json &expected = Json()) {
    if (!ack.is_object()) {
        return std::nullopt;
    }

    const auto projection = ack.find("mcu_projection");
    if (projection != ack.end() && projection->is_object() &&
        projection->contains("hardware_adapter") &&
        projection->at("hardware_adapter") == "sa8155-passion-21480") {
        const auto passion = qnx_passion_mcu_projection();
        const auto matches_passion = [&passion](const Json &scope) {
            return scope == passion && scope.at("normalized_fields").is_number_integer() &&
                   scope.at("projected_fields").is_number_integer() &&
                   scope.at("unsupported_fields").is_number_integer();
        };
        if (!matches_passion(expected) || !matches_passion(*projection)) {
            return std::nullopt;
        }
        return Json{{"fields", 3}, {"mcu_projection", passion}, {"full_wire_verified", false}};
    }

    if (!expected.is_null() &&
        (!qnx_mcu_delivery_scope(Json{{"mcu_projection", expected}}) ||
         !ack.contains("mcu_projection") || ack.at("mcu_projection") != expected)) {
        return std::nullopt;
    }

    if (!ack.contains("mcu_projection")) {
        return Json{{"fields", 579}};
    }

    const auto &scope = ack.at("mcu_projection");

    if (!scope.is_object()) {
        return std::nullopt;
    }

    for (const auto *key : {"packet_bytes", "wire_fields", "normalized_fields", "projected_fields",
                            "unsupported_fields"}) {
        if (!scope.contains(key) || !scope.at(key).is_number_integer()) {
            return std::nullopt;
        }
    }

    for (const auto &spec : qnx_adapter_registry) {
        if (spec.platform != QnxGuestPlatform::qinggan11) {
            continue;
        }

        if (scope == qnx_mcu_projection(spec.adapter) ||
            (spec.mcu_previous_projected_fields &&
             scope == qnx_mcu_projection(spec.adapter, spec.mcu_previous_projected_fields))) {
            return Json{{"fields", scope.at("projected_fields")},
                        {"mcu_projection", scope},
                        {"full_wire_verified", false}};
        }
    }

    return std::nullopt;
}
} // namespace voyemu
