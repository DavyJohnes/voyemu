#pragma once
#include "error.hpp"
#include "qnx_mcu_projection.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace voyemu {
// Explicit simulator fixture; no claim of physical calibration or OEM enum meaning.
inline void validate_qnx_qinggan11_initial_state(const Json &values) {
    if (!values.is_object() ||
        (!values.empty() && (values.size() != 1 || !values.contains("eps_calibrationFlag") ||
                             !values.at("eps_calibrationFlag").is_number_integer() ||
                             values.at("eps_calibrationFlag").get<std::int64_t>() < 0 ||
                             values.at("eps_calibrationFlag").get<std::uint64_t>() > 3))) {
        throw VoyemuError("QNX_QINGGAN11_INITIAL_STATE", "invalid initial state");
    }
}

inline Json
qnx_qinggan11_simulated_initial_state(const Json &values,
                                      std::string_view adapter = "sa8155-qinggan11-731") {
    validate_qnx_qinggan11_initial_state(values);

    if (std::none_of(qnx_adapter_registry.begin(), qnx_adapter_registry.end(),
                     [&](const QnxAdapterSpec &spec) {
                         return spec.name == adapter &&
                                spec.platform == QnxGuestPlatform::qinggan11;
                     })) {
        throw VoyemuError("QNX_QINGGAN11_INITIAL_ADAPTER", "unknown initial adapter");
    }

    if (values.empty()) {
        return nullptr;
    }

    return {{"hardware_adapter", adapter},
            {"stage", "after-normalized-projection"},
            {"kind", "simulated-fixture"},
            {"fields",
             {{"eps_calibrationFlag",
               {{"wire_index", 185},
                {"start_bit", 465},
                {"width", 2},
                {"value", values.at("eps_calibrationFlag")}}}}}};
}

// The raw_oem that a guest of the named MCU layout definition receives: an unprojected field
// carries its initial value, the only value the guest accepts for it, while the model keeps the
// logical value. Projected fields pass unchanged.
inline Json qnx_qinggan11_guest_state(const Json &definition, const Json &raw_oem) {
    auto result = raw_oem;

    for (const auto &row : definition.at("fields")) {
        if (row.contains("unprojected")) {
            result[row.at("name").get<std::string>()] = row.at("initial");
        }
    }

    return result;
}

// Versioned source data is tied to genuine parser and named engine DWARF. A row describes one
// field of the qinggan11 catalogue at its position: its OEM descriptor in wire and, when the guest
// does not write it, the owner of its bits in unprojected.
inline std::string qnx_qinggan11_mcu_layout(const Json &definition,
                                            const Json &initial_native = Json::object()) {
    validate_qnx_qinggan11_initial_state(initial_native);
    auto need = [](bool okay) {
        if (!okay) {
            throw VoyemuError("QNX_QINGGAN11_MCU_LAYOUT", "invalid MCU layout");
        }
    };
    const auto adapter = definition.value("hardware_adapter", std::string{});
    const auto spec = std::find_if(qnx_adapter_registry.begin(), qnx_adapter_registry.end(),
                                   [&](const QnxAdapterSpec &item) {
                                       return item.name == adapter &&
                                              item.platform == QnxGuestPlatform::qinggan11;
                                   });
    need(spec != qnx_adapter_registry.end());
    const unsigned projected_count = spec->mcu_projected_fields;
    const auto &model = catalog(VehicleCatalog::qinggan11);
    const auto count = static_cast<unsigned>(model.size());
    need(definition.at("schema") == 1 && definition.at("map_sha256") == qinggan11_map_sha256 &&
         definition.at("packet_bytes") == 650 && definition.at("wire_fields") == count &&
         definition.at("projected_fields") == projected_count &&
         definition.at("source_parser_sha256") == std::string(spec->mcu_parser_sha256) &&
         definition.at("source_engine_sha256") == std::string(spec->mcu_engine_sha256));
    const auto &fields = definition.at("fields");
    const auto initial = initial_state(VehicleCatalog::qinggan11).at("raw_oem");
    need(fields.is_array() && fields.size() == count);
    std::array<bool, 5200> used{};
    unsigned projected = 0;
    std::string result =
            "/* Generated from versioned Free A11 named source layout; regenerate through qnx_guest_build. */\nstatic const Qinggan11McuField qinggan11_fields[" +
            std::to_string(count) + "]={\n";

    for (unsigned i = 0; i < count; i++) {
        const auto &row = fields.at(i);
        const auto &field = model.at(i);
        need(row.at("position") == i && row.at("name") == field.name &&
             row.at("width") == field.width && row.at("initial") == initial.at(field.name));
        const auto &wire = row.at("wire");
        const auto bit = wire.at("start_bit").get<unsigned>();
        need(wire.at("index") == field.index && wire.at("width") == field.width &&
             wire.at("order") == 1 && bit < 5200 && field.width <= 5200 - bit);

        // A row names the engine item its parser case writes, or none when no parser case of
        // this firmware reads the wire field; the vehicle still sends it.
        if (wire.contains("named_consumer")) {
            need(wire.at("named_consumer").is_string() && wire.at("case_address").is_string() &&
                 wire.at("output_bit").is_number_unsigned() &&
                 wire.at("output_width") == field.width);
        } else {
            need(!wire.contains("case_address") && !wire.contains("output_bit") &&
                 !wire.contains("output_width"));
        }

        // The name of the OEM simulator for the wire field when it differs from the catalogue name.
        need(!wire.contains("oem_name") ||
             (wire.at("oem_name").is_string() && wire.at("oem_name") != field.name));

        for (unsigned b = bit; b < bit + field.width; b++) {
            need(!used[b]);
            used[b] = true;
        }

        // The settings feedback of the OEM engine owns bytes 250..259, the simulated calibration
        // fixture owns eps_calibrationFlag, and a guest value has 32 bits.
        bool written = true;

        if (row.contains("unprojected")) {
            const auto &owner = row.at("unprojected");
            need((owner == "settings_feedback" && spec->mcu_settings_feedback && bit >= 2000 &&
                  bit + field.width <= 2080) ||
                 (owner == "calibration_fixture" && field.name == "eps_calibrationFlag" &&
                  field.index == 185 && bit == 465 && field.width == 2) ||
                 (owner == "wider_than_guest_value" && field.width > 32));
            written = false;
        } else {
            need(field.width <= 32 &&
                 (!spec->mcu_settings_feedback || bit + field.width <= 2000 || bit >= 2080));
            projected++;
        }

        result += "{" + std::to_string(written ? bit : 65535) + "," +
                  std::to_string(written ? field.width : 0) + "," + row.at("initial").dump() +
                  "U},\n";
    }

    need(projected == projected_count);
    // Each wheel button keeps its qinggan09 field index 322..331 and names the catalogue position
    // of the field of the same name.
    const auto &buttons = catalog(VehicleCatalog::qinggan09);
    result += "};\nstatic const unsigned short qinggan11_wheel_fields[10]={";

    for (unsigned i = 0; i < 10; i++) {
        const auto &name = buttons.at(321 + i).name;
        const auto at = std::find_if(model.begin(), model.end(), [&](const Field &field) {
            return field.name == name;
        });
        need(name.starts_with("swm_") && at != model.end() && at->width == 1 &&
             !fields.at(at - model.begin()).contains("unprojected"));
        result += std::to_string(at - model.begin()) + (i < 9 ? "," : "");
    }

    auto position = [&](std::string_view name) {
        const auto at = std::find_if(model.begin(), model.end(), [&](const Field &field) {
            return field.name == name;
        });
        need(at != model.end());

        return std::to_string(at - model.begin());
    };

    return result + "};\nstatic const unsigned qinggan11_speed=" + position("speed") +
           "U, qinggan11_speed10x=" + position("speed10x") + "U, qinggan11_soc=" + position("soc") +
           "U;\nstatic const unsigned qinggan11_projected_fields=" +
           std::to_string(projected_count) +
           "U;\nstatic const unsigned qinggan11_settings_feedback_enabled=" +
           (spec->mcu_settings_feedback ? "1" : "0") +
           "U;\nstatic const unsigned qinggan11_calibration_fixture_enabled=" +
           (initial_native.empty() ? "0" : "1") +
           "U;\nstatic const unsigned qinggan11_calibration_fixture=" +
           (initial_native.empty() ? "0" : initial_native.at("eps_calibrationFlag").dump()) +
           "U;\n";
}
} // namespace voyemu
