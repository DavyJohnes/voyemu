#pragma once
#include "error.hpp"
#include "qnx_mcu_projection.hpp"
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

    if (adapter != "sa8155-qinggan11-731" && adapter != "sa8155-qinggan11-611") {
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

// Versioned source data is tied to genuine parser and named engine DWARF.
inline std::string qnx_qinggan11_mcu_layout(const Json &definition,
                                            const Json &initial_native = Json::object()) {
    validate_qnx_qinggan11_initial_state(initial_native);
    const bool older = definition.value("hardware_adapter", std::string{}) ==
                       "sa8155-qinggan11-611";
    const unsigned projected_count = older ? qnx_qinggan11_older_projected_fields : 462U;
    auto need = [](bool okay) {
        if (!okay) {
            throw VoyemuError("QNX_QINGGAN11_MCU_LAYOUT", "invalid MCU layout");
        }
    };
    need(definition.at("schema") == 1 &&
         definition.at("hardware_adapter") ==
                 (older ? "sa8155-qinggan11-611" : "sa8155-qinggan11-731") &&
         definition.at("map_sha256") == map_sha256 && definition.at("packet_bytes") == 650 &&
         definition.at("wire_fields") == 736 &&
         definition.at("projected_fields") == projected_count &&
         definition.at("source_parser_sha256") ==
                 (older ? "a830d876d9091190096025f4f4eb81294078b3002d955283b3a9dec296414f05"
                        : "6c381808b343adba849fa22f1efd18547de90cba7f3f3b43e43970a7f7222f39") &&
         definition.at("source_engine_sha256") ==
                 (older ? "faeb580b46c5c92d7ecd5b707be15b85874c838471f5cb9ca010b9a90e9ae195"
                        : "20a4ec0855a4e1955156dd13ec8e6731af0c28a0a5344db91be7d83e7a93d74a"));
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

        if (row.contains("wire")) {
            const auto &wire = row.at("wire");
            const auto id = wire.at("index").get<unsigned>();
            bit = wire.at("start_bit").get<unsigned>();
            width = wire.at("width").get<unsigned>();
            need(id >= 1 && id <= 736 && !indices[id] && wire.at("order") == 1 && width >= 1 &&
                 width <= 32 && bit < 5200 && width <= 5200 - bit &&
                 wire.at("output_width") == width);
            indices[id] = true;

            for (unsigned b = bit; b < bit + width; b++) {
                need(!used[b]);
                used[b] = true;
            }

            projected++;
        }

        result += "{" + std::to_string(bit) + "," + std::to_string(width) + "," +
                  row.at("unsupported_initial").dump() + "U},\n";
    }

    need(projected == projected_count);

    if (!initial_native.empty()) {
        need(definition.at("native_initial_fields") ==
             Json{{"eps_calibrationFlag", {{"index", 185}, {"start_bit", 465}, {"width", 2}}}});
        need(!indices[185] && !used[465] && !used[466]);
    }

    return result + "};\nstatic const unsigned qinggan11_projected_fields=" +
           std::to_string(projected_count) +
           "U;\nstatic const unsigned qinggan11_settings_feedback_enabled=" + (older ? "0" : "1") +
           "U;\nstatic const unsigned qinggan11_calibration_fixture_enabled=" +
           (initial_native.empty() ? "0" : "1") +
           "U;\nstatic const unsigned qinggan11_calibration_fixture=" +
           (initial_native.empty() ? "0" : initial_native.at("eps_calibrationFlag").dump()) +
           "U;\n";
}
} // namespace voyemu
