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

// The raw_oem that a guest of the named MCU layout definition receives: a field without a wire
// position carries its unsupported_initial, the only value the guest accepts for it, while the
// model keeps the logical value. Projected fields pass unchanged.
inline Json qnx_qinggan11_guest_state(const Json &definition, const Json &raw_oem) {
    auto result = raw_oem;

    for (const auto &row : definition.at("fields")) {
        if (!row.contains("wire")) {
            result[row.at("name").get<std::string>()] = row.at("unsupported_initial");
        }
    }

    return result;
}

// Versioned source data is tied to genuine parser and named engine DWARF.
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
    need(definition.at("schema") == 1 && definition.at("map_sha256") == map_sha256 &&
         definition.at("packet_bytes") == 650 && definition.at("wire_fields") == 736 &&
         definition.at("projected_fields") == projected_count &&
         definition.at("source_parser_sha256") == std::string(spec->mcu_parser_sha256) &&
         definition.at("source_engine_sha256") == std::string(spec->mcu_engine_sha256));
    const auto &fields = definition.at("fields");
    const auto &model = catalog();
    const auto initial = initial_state().at("raw_oem");
    need(fields.is_array() && fields.size() == 579 && model.size() == 579);
    std::array<bool, 5200> used{};
    std::array<bool, 737> indices{};
    unsigned projected = 0;
    std::string result =
            "/* Generated from versioned Free A11 named source layout; regenerate through qnx_guest_build. */\nstatic const Qinggan11McuField qinggan11_fields[579]={\n";

    for (unsigned i = 0; i < 579; i++) {
        const auto &row = fields.at(i);
        const auto &common = model.at(i);
        need(row.at("position") == i && row.at("name") == common.name &&
             row.at("normalized_width") == common.width &&
             row.at("unsupported_initial") == initial.at(common.name));
        unsigned bit = 65535, width = 0;
        std::uint64_t unknown_input = 0, unknown_wire = 0;

        if (row.contains("wire")) {
            const auto &wire = row.at("wire");
            const auto id = wire.at("index").get<unsigned>();
            bit = wire.at("start_bit").get<unsigned>();
            width = wire.at("width").get<unsigned>();
            need(id >= 1 && id <= 736 && !indices[id] && wire.at("order") == 1 && width >= 1 &&
                 width <= 32 && bit < 5200 && width <= 5200 - bit);
            indices[id] = true;

            // A row names the engine item its parser case writes, or none when no parser case of
            // this firmware reads the wire field; the vehicle still sends it.
            if (wire.contains("named_consumer")) {
                need(wire.at("named_consumer").is_string() && wire.at("case_address").is_string() &&
                     wire.at("output_bit").is_number_unsigned() &&
                     wire.at("output_width") == width);
            } else {
                need(!wire.contains("case_address") && !wire.contains("output_bit") &&
                     !wire.contains("output_width"));
            }

            // The name of the OEM simulator for the wire field when it differs from the model name.
            need(!wire.contains("oem_name") ||
                 (wire.at("oem_name").is_string() && wire.at("oem_name") != common.name));

            // An OEM slot wider than its normalized field whose engine tests the wire all-ones.
            if (wire.contains("unknown")) {
                const auto &unknown = wire.at("unknown");
                unknown_input = unknown.at("input").get<std::uint64_t>();
                unknown_wire = unknown.at("wire").get<std::uint64_t>();
                need(unknown.size() == 2 && unknown_input < (std::uint64_t{1} << common.width) &&
                     unknown_wire != 0 && unknown_wire != unknown_input &&
                     unknown_wire < (std::uint64_t{1} << width));
            }

            for (unsigned b = bit; b < bit + width; b++) {
                need(!used[b]);
                used[b] = true;
            }

            projected++;
        }

        result += "{" + std::to_string(bit) + "," + std::to_string(width) + "," +
                  row.at("unsupported_initial").dump() + "U," + std::to_string(unknown_input) +
                  "U," + std::to_string(unknown_wire) + "U},\n";
    }

    need(projected == projected_count);

    if (!initial_native.empty()) {
        need(definition.at("native_initial_fields") ==
             Json{{"eps_calibrationFlag", {{"index", 185}, {"start_bit", 465}, {"width", 2}}}});
        need(!indices[185] && !used[465] && !used[466]);
    }

    return result + "};\nstatic const unsigned qinggan11_projected_fields=" +
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
