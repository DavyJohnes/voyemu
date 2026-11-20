#pragma once
#include "model.hpp"
#include "qnx_adapter.hpp"
#include <optional>
#include <stdexcept>
namespace voyemu {
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
                                       std::uint64_t projected_fields) {
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
