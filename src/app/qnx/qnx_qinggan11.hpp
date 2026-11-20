#pragma once
#include "model.hpp"
#include "qnx_adapter.hpp"
#include <array>
namespace voyemu {
struct QnxQinggan11Original {
    const char *file;
    std::uint64_t bytes;
    const char *sha256;
};
inline constexpr std::array qnx_qinggan11_originals{
        QnxQinggan11Original{"hyp_la.img", 7340032,
                             "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8"},
        QnxQinggan11Original{"ifs2_la.img", 268435456,
                             "0e5598e15589c7816af65e2ee7257e240f62651fc142bf94129e08a1ce27c1c2"},
        QnxQinggan11Original{"system_la.img", 1073741824,
                             "4e611fab0db36483422a8ce9face07b5848085a21a974e35c4efa20adc812188"}};
inline constexpr std::array qnx_qinggan11_611_originals{
        QnxQinggan11Original{"hyp_la.img", 7340032,
                             "dbb3bc2c6f9b0308e38627cf6a3e3b6757ba98c4f5413334526c26d812ab08f0"},
        QnxQinggan11Original{"ifs2_la.img", 268435456,
                             "fe181358fb9f827bc9df2d63e7ba0fad4f5fb0264b3bfa43f9fa6d0b47bc3ad7"},
        QnxQinggan11Original{"system_la.img", 1073741824,
                             "f93893afd4dccea26099b11fa84a1c03684d9f4279eeca55d8852b71315837e1"}};

inline bool qnx_qinggan11_611_originals_match(const Json &originals) {
    if (!originals.is_object() || originals.size() != qnx_qinggan11_611_originals.size()) {
        return false;
    }

    for (const auto &pin : qnx_qinggan11_611_originals) {
        if (!originals.contains(pin.file)) {
            return false;
        }

        const auto &value = originals.at(pin.file);

        if (!value.is_object() || value.value("file", std::string{}) != pin.file ||
            value.value("sha256", std::string{}) != pin.sha256 || !value.contains("bytes") ||
            !value.at("bytes").is_number_unsigned() ||
            value.at("bytes").get<std::uint64_t>() != pin.bytes) {
            return false;
        }
    }

    return true;
}

// This owner OTA embeds devb-ram inside ifs_disk.img and does not need an unrelated recovery.
inline bool qnx_qinggan11_originals_match(const Json &originals) {
    if (!originals.is_object() || originals.size() != qnx_qinggan11_originals.size()) {
        return false;
    }

    for (const auto &pin : qnx_qinggan11_originals) {
        if (!originals.contains(pin.file)) {
            return false;
        }

        const auto &value = originals.at(pin.file);

        if (!value.is_object() || value.value("file", std::string{}) != pin.file ||
            value.value("sha256", std::string{}) != pin.sha256 || !value.contains("bytes") ||
            !value.at("bytes").is_number_unsigned() ||
            value.at("bytes").get<std::uint64_t>() != pin.bytes) {
            return false;
        }
    }

    return true;
}

// The pinned originals of a qinggan11 adapter: the table whose host image is the one of its
// registry entry.
inline const std::array<QnxQinggan11Original, 3> &
qnx_qinggan11_adapter_originals(QnxGuestAdapter adapter) {
    const auto hyp = qnx_adapter(adapter).hyp_sha256;

    for (const auto *table : {&qnx_qinggan11_originals, &qnx_qinggan11_611_originals}) {
        if (hyp == (*table)[0].sha256) {
            return *table;
        }
    }

    throw VoyemuError("QNX_GUEST_ADAPTER", "adapter has no qinggan11 originals");
}

// Whether profile-qnx.json originals declare exactly the pinned originals of the adapter.
inline bool qnx_qinggan11_adapter_originals_match(QnxGuestAdapter adapter, const Json &originals) {
    const auto &pins = qnx_qinggan11_adapter_originals(adapter);

    if (!originals.is_object() || originals.size() != pins.size()) {
        return false;
    }

    for (const auto &pin : pins) {
        if (!originals.contains(pin.file)) {
            return false;
        }

        const auto &value = originals.at(pin.file);

        if (!value.is_object() || value.value("file", std::string{}) != pin.file ||
            value.value("sha256", std::string{}) != pin.sha256 || !value.contains("bytes") ||
            !value.at("bytes").is_number_unsigned() ||
            value.at("bytes").get<std::uint64_t>() != pin.bytes) {
            return false;
        }
    }

    return true;
}

// OEM file guards and patches of a qinggan11 adapter; the registry holds its sizes and addresses.
void validate_qnx_qinggan11_ram_driver(QnxGuestAdapter adapter, std::string_view driver);
std::string prepare_qnx_qinggan11_theme_scene(QnxGuestAdapter adapter, std::string_view original);
std::string prepare_qnx_qinggan11_theme_manager(QnxGuestAdapter adapter, std::string_view original);
std::string prepare_qnx_qinggan11_runtime(std::string_view owned_runtime,
                                          std::string_view padded_observer_sha256);
std::string prepare_qnx_qinggan11_display_config(std::string_view original_display,
                                                 std::string_view owned_configuration);
} // namespace voyemu
