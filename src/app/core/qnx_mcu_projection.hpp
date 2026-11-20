#pragma once
#include "model.hpp"
#include <optional>
#include <stdexcept>
namespace voyemu {
inline Json qnx_qinggan11_mcu_projection() {
    return {{"packet_bytes", 650},       {"wire_fields", 736},
            {"normalized_fields", 579},  {"projected_fields", 462},
            {"unsupported_fields", 117}, {"unmapped_wire_bits", "preserved-original-each-call"}};
}

inline constexpr unsigned qnx_qinggan11_older_previous_projected_fields = 34;
inline constexpr unsigned qnx_qinggan11_older_projected_fields = 56;

inline Json
qnx_qinggan11_older_mcu_projection(unsigned fields = qnx_qinggan11_older_projected_fields) {
    return {{"hardware_adapter", "sa8155-qinggan11-611"},
            {"settings_feedback_supported", false},
            {"unsupported_field_policy", "reject_changes_from_initial_state"},
            {"packet_bytes", 650},
            {"wire_fields", 736},
            {"normalized_fields", 579},
            {"projected_fields", fields},
            {"unsupported_fields", 579 - fields},
            {"unmapped_wire_bits", "preserved-original-each-call"}};
}

inline Json qnx_mcu_request_projection(std::uint64_t packet_bytes, std::uint64_t wire_fields,
                                       std::uint64_t projected_fields) {
    if (!packet_bytes && !wire_fields && !projected_fields) {
        return nullptr;
    }

    if (packet_bytes == 650 && wire_fields == 736 &&
        (projected_fields == qnx_qinggan11_older_previous_projected_fields ||
         projected_fields == qnx_qinggan11_older_projected_fields)) {
        return qnx_qinggan11_older_mcu_projection(static_cast<unsigned>(projected_fields));
    }

    if (packet_bytes != 650 || wire_fields != 736 || projected_fields != 462) {
        throw VoyemuError("QNX_MCU_PROJECTION", "unsupported guest scope");
    }

    return qnx_qinggan11_mcu_projection();
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

    if (scope == qnx_qinggan11_older_mcu_projection() ||
        scope ==
                qnx_qinggan11_older_mcu_projection(qnx_qinggan11_older_previous_projected_fields)) {
        return Json{{"fields", scope.at("projected_fields")},
                    {"mcu_projection", scope},
                    {"full_wire_verified", false}};
    }

    if (ack.at("mcu_projection") != qnx_qinggan11_mcu_projection()) {
        return std::nullopt;
    }

    return Json{{"fields", 462},
                {"mcu_projection", qnx_qinggan11_mcu_projection()},
                {"full_wire_verified", false}};
}
} // namespace voyemu
