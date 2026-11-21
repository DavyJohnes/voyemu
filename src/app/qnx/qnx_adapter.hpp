#pragma once
#include "error.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace voyemu {
// One element per hardware_adapter value of profile-qnx.json. Only the registry below binds an
// element to its adapter string and to the exact facts of the pinned OEM build it adapts; code
// takes the element as a parameter and reads those facts from the registry.
enum class QnxGuestAdapter {
    sa8155_qinggan09,
    sa8155_qinggan11_731,
    sa8155_passion_21480,
    sa8155_qinggan11_611
};

enum class QnxGuestPlatform {
    qinggan09,
    qinggan11,
    passion
};

struct QnxAdapterSpec {
    QnxGuestAdapter adapter;
    // hardware_adapter of profile-qnx.json and the key of qnxGuestModules in toolchain.json.
    std::string_view name;
    QnxGuestPlatform platform;
    // Pinned OEM host image: hyp_la.img, or mifs_hyp_la.img on Passion.
    std::string_view hyp_sha256;
    std::uint64_t hyp_bytes;
    // The facts below describe a qinggan11 OEM build and are zero or empty elsewhere.
    // Header word at 0x3024: end of the host LZO stream, counted from 0x3000.
    std::uint32_t host_stream_end;
    // Startup word at 0x32d9c that the build is checked against; 0 when it has no such check.
    std::uint32_t startup_fdt_size;
    // ImageFS bytes at the start of ifs2_la.img.
    std::uint32_t imagefs_extent;
    // OEM theme scene library and theme manager sizes in system_la.img.
    std::uint64_t theme_scene_bytes, theme_manager_bytes;
    // Bytes 201 and 257 of devb-ram, inside the p_paddr of its two PT_LOAD segments; {0, 0} when
    // the build does not check them.
    std::array<std::uint8_t, 2> ram_driver_paddr;
    // Named MCU fields that the guest projects, an earlier accepted runtime scope and an earlier
    // scope that only a stopped QNX upgrade reads; 0 for none.
    unsigned mcu_projected_fields, mcu_previous_projected_fields, mcu_upgrade_source_fields;
    // Whether the OEM MCU engine reports settings back to the guest.
    bool mcu_settings_feedback;
    // Named MCU field layout in src/guest/qnx and the OEM parser and engine it was derived from.
    std::string_view mcu_layout_file, mcu_parser_sha256, mcu_engine_sha256;
    // Extra compile definition of the guest modules; empty for none.
    std::string_view guest_define;
    // Whether the launcher embeds mcu_layout_file (src/app/project_files.cmake), so that
    // preparation checks it against initial_native_state before the guest build and the MCU
    // delivery of a session reads it.
    bool mcu_layout_embedded;
    // Whether the guest sets the theme skin through the OEM setters and its theme receipt reports
    // the requested theme and skin getters.
    bool theme_receipt_skin;
};

inline constexpr std::array qnx_adapter_registry{
        QnxAdapterSpec{QnxGuestAdapter::sa8155_qinggan09,
                       "sa8155-qinggan09",
                       QnxGuestPlatform::qinggan09,
                       "0af64f396f7de8dafe3488f21ec7e4cd989b3d02d3de0412aec020fdb1424b89",
                       7340032,
                       0,
                       0,
                       0,
                       0,
                       0,
                       {0, 0},
                       0,
                       0,
                       0,
                       false,
                       {},
                       {},
                       {},
                       {},
                       false,
                       false},
        QnxAdapterSpec{QnxGuestAdapter::sa8155_qinggan11_731,
                       "sa8155-qinggan11-731",
                       QnxGuestPlatform::qinggan11,
                       "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8",
                       7340032,
                       0x2d45a8,
                       0,
                       147839040,
                       207048,
                       116824,
                       {0, 0},
                       462,
                       0,
                       0,
                       true,
                       "native_mcu_qinggan11.json",
                       "6c381808b343adba849fa22f1efd18547de90cba7f3f3b43e43970a7f7222f39",
                       "20a4ec0855a4e1955156dd13ec8e6731af0c28a0a5344db91be7d83e7a93d74a",
                       {},
                       true,
                       false},
        QnxAdapterSpec{QnxGuestAdapter::sa8155_passion_21480,
                       "sa8155-passion-21480",
                       QnxGuestPlatform::passion,
                       "1e7c487aae4f4353621f8d0301b352cc974e9037d114ff3ec2556d048512ad28",
                       2254664,
                       0,
                       0,
                       0,
                       0,
                       0,
                       {0, 0},
                       0,
                       0,
                       0,
                       false,
                       {},
                       {},
                       {},
                       {},
                       false,
                       false},
        QnxAdapterSpec{QnxGuestAdapter::sa8155_qinggan11_611,
                       "sa8155-qinggan11-611",
                       QnxGuestPlatform::qinggan11,
                       "dbb3bc2c6f9b0308e38627cf6a3e3b6757ba98c4f5413334526c26d812ab08f0",
                       7340032,
                       0x2d46d0,
                       0x095dc000,
                       147830848,
                       207032,
                       116616,
                       {0x50, 0x8b},
                       462,
                       34,
                       33,
                       false,
                       "native_mcu_qinggan11_611.json",
                       "a830d876d9091190096025f4f4eb81294078b3002d955283b3a9dec296414f05",
                       "faeb580b46c5c92d7ecd5b707be15b85874c838471f5cb9ca010b9a90e9ae195",
                       "-DQGFX_QINGGAN11_611=1",
                       true,
                       true}};

inline const QnxAdapterSpec &qnx_adapter(QnxGuestAdapter adapter) {
    for (const auto &spec : qnx_adapter_registry) {
        if (spec.adapter == adapter) {
            return spec;
        }
    }

    throw VoyemuError("QNX_GUEST_ADAPTER", "unknown guest adapter");
}

// The registry entry of a hardware_adapter string of profile-qnx.json.
inline const QnxAdapterSpec &qnx_adapter(std::string_view name) {
    for (const auto &spec : qnx_adapter_registry) {
        if (spec.name == name) {
            return spec;
        }
    }

    throw VoyemuError("QNX_GUEST_ADAPTER", "unknown guest adapter");
}
} // namespace voyemu
