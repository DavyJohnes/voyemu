#pragma once
#include "model.hpp"
#include "qnx_adapter.hpp"
#include <optional>
#include <stdexcept>
namespace voyemu {
inline Json qnx_passion_mcu_projection() {
    return {{"hardware_adapter", "sa8155-passion-21480"},
            {"transport", "oem-ipc-topics"},
            {"catalog_fields", 579},
            {"projected_fields", 3},
            {"unsupported_fields", 576},
            {"published_fields", Json::array({"speed", "spd_valid", "soc"})}};
}

// The MCU projection of a qinggan11 adapter: the guest writes the projected fields of the
// qinggan11 catalogue into each OEM packet and keeps the OEM bits of the others. The adapter name
// keeps the equal scopes of the two firmwares distinct.
inline Json qnx_mcu_projection(QnxGuestAdapter adapter) {
    const auto &spec = qnx_adapter(adapter);

    if (spec.platform != QnxGuestPlatform::qinggan11) {
        throw VoyemuError("QNX_MCU_PROJECTION", "adapter has no MCU projection");
    }

    const auto fields = static_cast<unsigned>(catalog(VehicleCatalog::qinggan11).size());

    return {{"hardware_adapter", spec.name},
            {"packet_bytes", 650},
            {"wire_fields", fields},
            {"catalog_fields", fields},
            {"projected_fields", spec.mcu_projected_fields},
            {"unsupported_fields", fields - spec.mcu_projected_fields},
            {"unmapped_wire_bits", "preserved-original-each-call"}};
}

// Whether fields is the projected field count of the adapter.
inline bool qnx_mcu_runtime_scope(const QnxAdapterSpec &spec, std::uint64_t fields) {
    return spec.platform == QnxGuestPlatform::qinggan11 && fields == spec.mcu_projected_fields;
}

// A qinggan11 request names its scope by the projected field count; adapter, when given, selects
// among adapters whose layouts project the same count.
inline Json qnx_mcu_request_projection(std::uint64_t packet_bytes, std::uint64_t wire_fields,
                                       std::uint64_t projected_fields, bool passion_topics = false,
                                       std::string_view adapter = {}) {
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
            if ((adapter.empty() || spec.name == adapter) &&
                qnx_mcu_runtime_scope(spec, projected_fields)) {
                return qnx_mcu_projection(spec.adapter);
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
            return scope == passion && scope.at("catalog_fields").is_number_integer() &&
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

    for (const auto *key : {"packet_bytes", "wire_fields", "catalog_fields", "projected_fields",
                            "unsupported_fields"}) {
        if (!scope.contains(key) || !scope.at(key).is_number_integer()) {
            return std::nullopt;
        }
    }

    for (const auto &spec : qnx_adapter_registry) {
        if (spec.platform != QnxGuestPlatform::qinggan11) {
            continue;
        }

        if (scope == qnx_mcu_projection(spec.adapter)) {
            return Json{{"fields", scope.at("projected_fields")},
                        {"mcu_projection", scope},
                        {"full_wire_verified", false}};
        }
    }

    return std::nullopt;
}
} // namespace voyemu
