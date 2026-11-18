#include "selinux_patch.hpp"
#include "error.hpp"
#include "process.hpp"
#include <array>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr std::array<std::string_view, 7> services = {
        "qg.canbus u:object_r:voyemu_canbus_service:s0",
        "qg.power_manager u:object_r:voyemu_oem_service:s0",
        "qg.carsignal u:object_r:voyemu_oem_service:s0",
        "qg.key_manager u:object_r:voyemu_oem_service:s0",
        "dls_tbox_service u:object_r:voyemu_oem_service:s0",
        "qg.system_policy u:object_r:voyemu_oem_service:s0",
        "qg.qgbus_service u:object_r:voyemu_oem_service:s0"};
constexpr std::array<std::string_view, 5> properties = {
        "persist.app. u:object_r:system_prop:s0", "persist.sys.qg. u:object_r:system_prop:s0",
        "persist.clusterCardInfo u:object_r:system_prop:s0",
        "persist.media.metrics.enabled u:object_r:system_prop:s0",
        "ignoreActivation u:object_r:system_prop:s0"};

// The CIL of the CAN simulator domain, appended to the bounded policy of A9 and of A11.
constexpr std::string_view can_simulator_policy =
        R"CANPOLICY(; candidate CAN simulator policy: subject to secilc and AVC validation
(type can_simulator)
(type can_simulator_exec)
(type can_simulator_fixture)
(type can_device)
(type can_simulator_device)
(type can_simulator_socket)
(type can_simulator_observe_socket)
(roletype object_r can_simulator)
(roletype object_r can_simulator_exec)
(roletype object_r can_simulator_fixture)
(roletype object_r can_device)
(roletype object_r can_simulator_device)
(roletype object_r can_simulator_socket)
(roletype object_r can_simulator_observe_socket)
(typeattributeset domain (can_simulator))
(typeattributeset coredomain (can_simulator))
(typeattributeset file_type (can_simulator_exec can_simulator_fixture can_simulator_socket can_simulator_observe_socket))
(typeattributeset exec_type (can_simulator_exec))
(typeattributeset dev_type (can_device can_simulator_device))
(typeattributeset coredomain_socket (can_simulator_socket can_simulator_observe_socket))
(allow init can_simulator_exec (file (read getattr map execute open)))
(allow init can_simulator (process (transition siginh rlimitinh)))
(typetransition init can_simulator_exec process can_simulator)
(allow can_simulator can_simulator_exec (file (read getattr map execute entrypoint open)))
(allow can_simulator can_simulator_fixture (file (read getattr open)))
(allow can_simulator can_simulator_device (chr_file (getattr open read write ioctl)))
(allowx can_simulator can_simulator_device (ioctl chr_file (0x0002 0x0003)))
(allow can_simulator proc (dir (search)))
(allow can_simulator proc_random (dir (search)))
(allow can_simulator proc_random (file (read getattr open)))
(allow can_simulator init (fd (use)))
(allow can_simulator init (unix_stream_socket (read write getattr getopt setopt accept)))
(allow init can_simulator_socket (sock_file (create setattr unlink write)))
(allow init can_simulator_observe_socket (sock_file (create setattr unlink write)))
(allow ueventd can_simulator_device (chr_file (create setattr getattr unlink)))
(allow ueventd can_device (chr_file (create setattr getattr unlink)))
(allow system_app can_simulator_socket (sock_file (write)))
(allow su can_simulator_observe_socket (sock_file (write)))
)CANPOLICY";

std::string one_newline(std::string_view input) {
    while (input.ends_with('\n')) {
        input.remove_suffix(1);
    }

    return std::string(input) + "\n";
}
} // namespace

// OEM vendor_file_contexts SHA256 74dc51b9ead1e5dc3ea3a5e88c539b5beee3c769ae88789506461b18b9c03748
// maps /dev/block/vdh to vdh_device. This synthetic read-only loop uses that label; OEM
// mlstrustedobject and qg_device attributes are not reproduced.
std::string apply_can_early_policy_patch(std::string_view profile, unsigned api,
                                         std::string_view input) {
    if (profile != "free-09-chinese-20260106-6.0.3" || api != 28 ||
        sha256_bytes(input) != "ea017c277007b5ba8401ab214b2d607d72ddd39b7a303db05eb5281cb405fd22") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "exact simulator policy required");
    }

    auto output =
            std::string(input) +
            R"EARLYPOLICY(; candidate early input domain: compile and runtime validation required
(type can_early_inputs)
(type can_early_inputs_exec)
(type can_early_input_file)
(type can_early_data_file)
(type can_early_token_dir)
(type vdh_device)
(type can_early_marker_file)
(type can_early_ready_prop)
(roletype object_r can_early_inputs)
(roletype object_r can_early_inputs_exec)
(roletype object_r can_early_input_file)
(roletype object_r can_early_data_file)
(roletype object_r can_early_token_dir)
(roletype object_r vdh_device)
(roletype object_r can_early_marker_file)
(roletype object_r can_early_ready_prop)
(typeattributeset domain (can_early_inputs))
(typeattributeset coredomain (can_early_inputs))
(typeattributeset file_type (can_early_inputs_exec can_early_input_file can_early_data_file can_early_marker_file can_early_token_dir))
(typeattributeset data_file_type (can_early_data_file))
(typeattributeset core_data_file_type (can_early_data_file))
(typeattributeset exec_type (can_early_inputs_exec))
(typeattributeset dev_type (vdh_device))
(typeattributeset property_type (can_early_ready_prop))
(typeattributeset extended_core_property_type (can_early_ready_prop))
(allow init can_early_inputs_exec (file (read getattr map execute open)))
(allow init can_early_inputs (process (transition siginh rlimitinh)))
(typetransition init can_early_inputs_exec process can_early_inputs)
(allow can_early_inputs can_early_inputs_exec (file (read getattr map execute entrypoint open)))
(allow can_early_inputs self (capability (chown)))
(allow can_early_inputs can_early_input_file (file (read getattr open)))
(allow can_early_inputs system_app_data_file (dir (getattr search write add_name)))
(typetransition can_early_inputs system_app_data_file dir "files" can_early_data_file)
(typetransition can_early_inputs system_app_data_file dir "databases" can_early_data_file)
(typetransition can_early_inputs can_early_data_file dir "databases" can_early_data_file)
(allow can_early_inputs can_early_token_dir (dir (getattr search)))
(allow can_early_inputs can_early_data_file (dir (create open read getattr setattr search write add_name remove_name)))
(allow can_early_inputs can_early_data_file (file (create open read write getattr setattr link unlink)))
(allow system_app can_early_data_file (dir (open read getattr search write add_name remove_name)))
(allow system_app can_early_data_file (file (create open read write append map getattr setattr link unlink rename lock)))
(allow can_early_inputs block_device (dir (open read getattr search write add_name remove_name)))
(typetransition can_early_inputs block_device file "voyemu-early.owner" can_early_marker_file)
(typetransition can_early_inputs block_device file "voyemu-early.owner.new" can_early_marker_file)
(allow can_early_inputs loop_device (blk_file (open read getattr ioctl link)))
(allowx can_early_inputs loop_device (ioctl blk_file (0x125e 0x4c00 0x4c05)))
(allow can_early_inputs vdh_device (blk_file (open read getattr ioctl link)))
(allow init loop_device (blk_file (relabelfrom)))
(allow init vdh_device (blk_file (relabelto)))
(allowx can_early_inputs vdh_device (ioctl blk_file (0x125e 0x4c00 0x4c05)))
(allow can_early_inputs loop_control_device (chr_file (open read write getattr ioctl)))
(allowx can_early_inputs loop_control_device (ioctl chr_file (0x4c82)))
(allow can_early_inputs can_early_marker_file (file (create open read write getattr setattr link unlink)))
(allow can_early_inputs property_socket (sock_file (write)))
(allow can_early_inputs init (unix_stream_socket (connectto)))
(allow can_early_inputs can_early_ready_prop (property_service (set)))
(allow can_simulator can_early_ready_prop (file (read open getattr map)))
(allow can_simulator exported3_system_prop (file (read open getattr map)))
; Fixed init-created CAN sockets and read-only simulator inspection.
(allow init can_simulator_socket (unix_stream_socket (create bind)))
(allow init can_simulator_observe_socket (unix_stream_socket (create bind)))
(allow can_simulator can_simulator_socket (unix_stream_socket (listen accept read write getattr getopt setopt shutdown)))
(allow can_simulator can_simulator_observe_socket (unix_stream_socket (listen accept read write getattr getopt setopt shutdown)))
(allow system_app can_simulator_socket (unix_stream_socket (connectto)))
(allow su can_simulator_observe_socket (unix_stream_socket (connectto)))
(allow su can_early_inputs_exec (file (read open getattr)))
(allow su can_simulator_exec (file (read open getattr)))
(allow su can_simulator_fixture (file (read open getattr)))
(allow su can_early_input_file (file (read open getattr)))
(allow su can_early_ready_prop (file (read open getattr map)))
(allow su can_early_data_file (dir (getattr search)))
(allow su can_early_data_file (file (read open getattr map)))
(allow su vdh_device (blk_file (getattr)))
(allow system_app can_early_token_dir (dir (getattr search)))
(allow can_early_inputs can_early_ready_prop (file (read open getattr map)))
(allow su can_device (chr_file (open read write getattr ioctl)))
(allowx su can_device (ioctl chr_file (0x0001)))
(allow system_app can_device (chr_file (open read write getattr ioctl)))
(allowx system_app can_device (ioctl chr_file (0x0001)))
(allow installd can_early_data_file (dir (getattr search open read)))
(allow installd can_early_data_file (file (getattr)))
)EARLYPOLICY";

    if (sha256_bytes(output) !=
        "2879712e7a6a12f262ab346fbe61b7f5b992e652e10d93edf8eb44a6c7b84a77") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "early policy output differs");
    }

    return output;
}

std::string apply_can_early_file_context_patch(std::string_view profile, unsigned api,
                                               std::string_view input) {
    if (profile != "free-09-chinese-20260106-6.0.3" || api != 28 ||
        sha256_bytes(input) != "b140f7f850f0279e8935053b948784708c9c62de4aeb59019a7c07ef0d9f5813") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "exact simulator file contexts required");
    }

    auto output = std::string(input) +
                  R"EARLYCTX(/system/bin/voyemu-early-inputs u:object_r:can_early_inputs_exec:s0
/system/etc/voyemu/qg-settings-guest\.db u:object_r:can_early_input_file:s0
/system/etc/voyemu/hybrid-nvram\.img u:object_r:can_early_input_file:s0
/data/user_de/0/com\.qinggan\.providers\.settings/files/databases(/.*)? u:object_r:can_early_data_file:s0
/dev/block/vdh u:object_r:vdh_device:s0
/dev/block/voyemu-early\.owner(\.new)? u:object_r:can_early_marker_file:s0
/private u:object_r:can_early_token_dir:s0
/private/configs u:object_r:can_early_token_dir:s0
/private/configs/token u:object_r:can_early_token_dir:s0
)EARLYCTX";

    if (sha256_bytes(output) !=
        "8bd8062c3b8108bdc34573f78d403691e747298a986a3464ace94721b02ad1d7") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "early file contexts output differs");
    }

    return output;
}

std::string apply_can_early_property_patch(std::string_view profile, unsigned api,
                                           std::string_view input) {
    if (profile != "free-09-chinese-20260106-6.0.3" || api != 28 ||
        sha256_bytes(input) != "734cfb54ff72018f7c198a4f558a59428836dc0abe20da6cc68cc77c4526101e") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "exact A9 property contexts required");
    }

    auto output =
            std::string(input) +
            "sys.voyemu.early.ready u:object_r:can_early_ready_prop:s0 exact string\nsys.voyemu.early.probe u:object_r:exported3_system_prop:s0 exact string\n";

    if (sha256_bytes(output) !=
        "bd2a3eafde0f3d98966d590dcc70047f9fa38e9812f3660a0326182e0f43540d") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "early property output differs");
    }

    return output;
}

std::string apply_can_simulator_policy_patch(std::string_view profile, unsigned api,
                                             std::string_view input) {
    if (profile != "free-09-chinese-20260106-6.0.3" || api != 28 ||
        sha256_bytes(input) != "3df9140132bf65a7e6e59d8fec735595feee3ed3b32d57d7f6b4a10b82cb903e") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "exact A9 bounded policy required");
    }

    auto output = std::string(input) + std::string(can_simulator_policy);

    if (sha256_bytes(output) !=
        "ea017c277007b5ba8401ab214b2d607d72ddd39b7a303db05eb5281cb405fd22") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "simulator policy output differs");
    }

    return output;
}

std::string apply_can_simulator_context_patch(std::string_view profile, unsigned api,
                                              std::string_view input) {
    if (profile != "free-09-chinese-20260106-6.0.3" || api != 28 ||
        sha256_bytes(input) != "88e8b4bf854406de5a5464ea37f0996487ae60e7e12ae1302fc9119e9b46456c") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "exact A9 API28 file contexts required");
    }

    auto output = std::string(input) +
                  "/system/bin/voyemu-can-model u:object_r:can_simulator_exec:s0\n"
                  "/system/etc/voyemu/can-model\\.fixture u:object_r:can_simulator_fixture:s0\n"
                  "/dev/cis_can u:object_r:can_device:s0\n"
                  "/dev/cis_can_model u:object_r:can_simulator_device:s0\n"
                  "/dev/socket/voyemu_can_model u:object_r:can_simulator_socket:s0\n"
                  "/dev/socket/voyemu_can_observe u:object_r:can_simulator_observe_socket:s0\n";

    if (sha256_bytes(output) !=
        "b140f7f850f0279e8935053b948784708c9c62de4aeb59019a7c07ef0d9f5813") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "simulator file contexts output differs");
    }

    return output;
}

// A11 API30 CAN simulator/early-input transforms; addition bytes mirror the A9 recipes.
std::string apply_qinggan11_can_simulator_context_patch(std::string_view profile, unsigned api,
                                                        std::string_view input) {
    if ((profile != "free-11-chinese-20260731-7.3.1" &&
         profile != "free-11-chinese-20250710-6.11.1") ||
        api != 30 ||
        sha256_bytes(input) != "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "exact API30 platform file contexts required");
    }

    auto base = std::string(input);

    while (base.ends_with('\n')) {
        base.pop_back();
    }

    auto output = base + R"QINGGAN11CTX(
/system/bin/voyemu-can-model u:object_r:can_simulator_exec:s0
/system/etc/voyemu/can-model\.fixture u:object_r:can_simulator_fixture:s0
/dev/cis_can u:object_r:can_device:s0
/dev/cis_can_model u:object_r:can_simulator_device:s0
/dev/socket/voyemu_can_model u:object_r:can_simulator_socket:s0
/dev/socket/voyemu_can_observe u:object_r:can_simulator_observe_socket:s0
)QINGGAN11CTX";

    if (sha256_bytes(output) !=
        "7fc4c203185a6036dafacc401472a152111b9d313702f28befb1327b5dab1f5e") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "API30 simulator contexts output differs");
    }

    return output;
}

std::string apply_qinggan11_can_early_context_patch(std::string_view profile, unsigned api,
                                                    std::string_view input) {
    if ((profile != "free-11-chinese-20260731-7.3.1" &&
         profile != "free-11-chinese-20250710-6.11.1") ||
        api != 30 ||
        sha256_bytes(input) != "7fc4c203185a6036dafacc401472a152111b9d313702f28befb1327b5dab1f5e") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH",
                          "exact patched API30 simulator contexts required");
    }

    auto base = std::string(input);

    while (base.ends_with('\n')) {
        base.pop_back();
    }

    auto output = base + R"QINGGAN11ECTX(
/system/bin/voyemu-early-inputs u:object_r:can_early_inputs_exec:s0
/system/etc/voyemu/qg-settings-guest\.db u:object_r:can_early_input_file:s0
/system/etc/voyemu/hybrid-nvram\.img u:object_r:can_early_input_file:s0
/data/user_de/0/com\.qinggan\.providers\.settings/files/databases(/.*)? u:object_r:can_early_data_file:s0
/private u:object_r:can_early_token_dir:s0
/private/configs u:object_r:can_early_token_dir:s0
/private/configs/token u:object_r:can_early_token_dir:s0
)QINGGAN11ECTX";

    if (sha256_bytes(output) !=
        "873da6472b242a86d657900bdd90d4fa458dbde615c75ea406c6806f06ce8ecc") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "API30 early contexts output differs");
    }

    return output;
}

std::string apply_qinggan11_can_simulator_policy_patch(std::string_view profile, unsigned api,
                                                       std::string_view input) {
    if ((profile != "free-11-chinese-20260731-7.3.1" &&
         profile != "free-11-chinese-20250710-6.11.1") ||
        api != 30 ||
        sha256_bytes(input) != "5aa168a2fbb99353449d52a1d05c6baf5a900b4244de381e66173de7ef2028f9") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH",
                          "exact bounded API30 Free A11 service policy required");
    }

    auto base = std::string(input);

    while (base.ends_with('\n')) {
        base.pop_back();
    }

    auto output = base + "\n" + std::string(can_simulator_policy);

    if (sha256_bytes(output) !=
        "da0a10c128b5c52c67e52795090755fd97fc5d02999c26d150bd9afa27d6fdb1") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "API30 simulator policy output differs");
    }

    return output;
}

std::string apply_qinggan11_can_early_policy_patch(std::string_view profile, unsigned api,
                                                   std::string_view input) {
    if ((profile != "free-11-chinese-20260731-7.3.1" &&
         profile != "free-11-chinese-20250710-6.11.1") ||
        api != 30 ||
        sha256_bytes(input) != "da0a10c128b5c52c67e52795090755fd97fc5d02999c26d150bd9afa27d6fdb1") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH",
                          "exact patched API30 simulator policy required");
    }

    auto output =
            std::string(input) +
            R"QINGGAN11ECIL(; candidate early input domain: compile and runtime validation required
(type can_early_inputs)
(type can_early_inputs_exec)
(type can_early_input_file)
(type can_early_data_file)
(type can_early_token_dir)
(type can_early_ready_prop)
(roletype object_r can_early_inputs)
(roletype object_r can_early_inputs_exec)
(roletype object_r can_early_input_file)
(roletype object_r can_early_data_file)
(roletype object_r can_early_token_dir)
(roletype object_r can_early_ready_prop)
(typeattributeset domain (can_early_inputs))
(typeattributeset coredomain (can_early_inputs))
(typeattributeset file_type (can_early_inputs_exec can_early_input_file can_early_data_file can_early_token_dir))
(typeattributeset data_file_type (can_early_data_file))
(typeattributeset core_data_file_type (can_early_data_file))
(typeattributeset exec_type (can_early_inputs_exec))
(typeattributeset property_type (can_early_ready_prop))
(typeattributeset extended_core_property_type (can_early_ready_prop))
(allow init can_early_inputs_exec (file (read getattr map execute open)))
(allow init can_early_inputs (process (transition siginh rlimitinh)))
(typetransition init can_early_inputs_exec process can_early_inputs)
(allow can_early_inputs can_early_inputs_exec (file (read getattr map execute entrypoint open)))
(allow can_early_inputs self (capability (chown)))
(allow can_early_inputs can_early_input_file (file (read getattr open)))
(allow can_early_inputs system_app_data_file (dir (getattr search write add_name)))
(typetransition can_early_inputs system_app_data_file dir "files" can_early_data_file)
(typetransition can_early_inputs system_app_data_file dir "databases" can_early_data_file)
(typetransition can_early_inputs can_early_data_file dir "databases" can_early_data_file)
(allow can_early_inputs can_early_token_dir (dir (getattr search)))
(allow can_early_inputs can_early_data_file (dir (create open read getattr setattr search write add_name remove_name)))
(allow can_early_inputs can_early_data_file (file (create open read write getattr setattr link unlink)))
(allow system_app can_early_data_file (dir (open read getattr search write add_name remove_name)))
(allow system_app can_early_data_file (file (create open read write append map getattr setattr link unlink rename lock)))
(allow can_early_inputs property_socket (sock_file (write)))
(allow can_early_inputs init (unix_stream_socket (connectto)))
(allow can_early_inputs can_early_ready_prop (property_service (set)))
(allow can_simulator can_early_ready_prop (file (read open getattr map)))
(allow can_simulator exported3_system_prop (file (read open getattr map)))
; Fixed init-created CAN sockets and read-only simulator inspection.
(allow init can_simulator_socket (unix_stream_socket (create bind)))
(allow init can_simulator_observe_socket (unix_stream_socket (create bind)))
(allow can_simulator can_simulator_socket (unix_stream_socket (listen accept read write getattr getopt setopt shutdown)))
(allow can_simulator can_simulator_observe_socket (unix_stream_socket (listen accept read write getattr getopt setopt shutdown)))
(allow system_app can_simulator_socket (unix_stream_socket (connectto)))
(allow su can_simulator_observe_socket (unix_stream_socket (connectto)))
(allow su can_early_inputs_exec (file (read open getattr)))
(allow su can_simulator_exec (file (read open getattr)))
(allow su can_simulator_fixture (file (read open getattr)))
(allow su can_early_input_file (file (read open getattr)))
(allow su can_early_ready_prop (file (read open getattr map)))
(allow su can_early_data_file (dir (getattr search)))
(allow su can_early_data_file (file (read open getattr map)))
(allow system_app can_early_token_dir (dir (getattr search)))
(allow can_early_inputs can_early_ready_prop (file (read open getattr map)))
(allow su can_device (chr_file (open read write getattr ioctl)))
(allowx su can_device (ioctl chr_file (0x0001)))
(allow system_app can_device (chr_file (open read write getattr ioctl)))
(allowx system_app can_device (ioctl chr_file (0x0001)))
(allow installd can_early_data_file (dir (getattr search open read)))
(allow installd can_early_data_file (file (getattr)))
)QINGGAN11ECIL";

    if (sha256_bytes(output) !=
        "123f8180bca7499a8b837f5c4cd5b97e11ca216fa4a370179d1dc7624f9dd7b1") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "API30 early policy output differs");
    }

    return output;
}

std::string apply_qinggan11_can_early_property_patch(std::string_view profile, unsigned api,
                                                     std::string_view input) {
    if ((profile != "free-11-chinese-20260731-7.3.1" &&
         profile != "free-11-chinese-20250710-6.11.1") ||
        api != 30 ||
        sha256_bytes(input) != "50c13ef43f195b1b94c5e1ed85f3a33578220e6074d9d244414ae73c9837d2db") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH",
                          "exact patched API30 Free A11 7.3.1 property contexts required");
    }

    auto output =
            std::string(input) +
            "sys.voyemu.early.ready u:object_r:can_early_ready_prop:s0 exact string\nsys.voyemu.early.probe u:object_r:exported3_system_prop:s0 exact string\n";

    if (sha256_bytes(output) !=
        "6618819c1a2cf856486d66e270459aeccfe03d8d7837c9cc946940cd5a104493") {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "API30 early property output differs");
    }

    return output;
}

std::string apply_selinux_context_patch(std::string_view id, unsigned api, std::string_view input,
                                        bool qinggan11_731) {
    if (api == 30 && id == "selinux-hash-api30") {
        const std::string_view pin =
                qinggan11_731
                        ? (sha256_bytes(input) ==
                                           "4bf19f6c8144488d129d7f280d66c3ffb0354a95fffcb6c0de6e6a413908efdc"
                                   ? "4bf19f6c8144488d129d7f280d66c3ffb0354a95fffcb6c0de6e6a413908efdc"
                                   : std::string_view{"d312218f439e3f970934791cdfa572078b5d78b21a3d5eeddff3f2a1c5b6e364"})
                        : std::string_view{
                                  "f1e41e034fbb8e72dbafd180f86188cee6e04facde570982f199160079bfe04f"};

        if (sha256_bytes(input) != pin) {
            throw VoyemuError("SELINUX_PATCH_MISMATCH",
                              "exact patched API30 CIL followed by mapping required");
        }

        return std::string(pin) + "\n";
    }

    if (api == 30 && id == "keymanager-contexts-api30") {
        if (sha256_bytes(input) !=
            "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959") {
            throw VoyemuError("SELINUX_PATCH_MISMATCH",
                              "exact API30 KeyManager context input required");
        }

        auto output = apply_selinux_context_patch("selinux-service-contexts-api30", api, input);
        output +=
                "qg.key_manager u:object_r:voyemu_cockpit_service:s0\nqg.power_manager u:object_r:voyemu_cockpit_service:s0\nqg.cluster.service u:object_r:voyemu_cockpit_service:s0\nqg.system_policy u:object_r:voyemu_cockpit_service:s0\nqg.qgbus_service u:object_r:voyemu_cockpit_service:s0\nqg.carsignal u:object_r:voyemu_cockpit_service:s0\nqg.audio_policy u:object_r:voyemu_cockpit_service:s0\n";

        if (sha256_bytes(output) !=
            "d73bcfe6f82f18f52e40e86d55f2a1163d2b526a0143bee9322d0ea5da368061") {
            throw VoyemuError("SELINUX_PATCH_MISMATCH", "KeyManager context output differs");
        }

        return output;
    }

    if (api == 30 && (id == "selinux-services-api30" || id == "selinux-service-contexts-api30")) {
        const bool policy = id == "selinux-services-api30";
        const std::string_view input_pin =
                policy ? "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17"
                       : "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959";

        if (sha256_bytes(input) != input_pin) {
            throw VoyemuError("SELINUX_PATCH_MISMATCH",
                              "exact API30 platform service policy/context required");
        }

        const std::string_view addition =
                policy ? R"RULES(
; voyemu API30 cockpit services
(type voyemu_cockpit_service)
(roletype object_r voyemu_cockpit_service)
(typeattributeset service_manager_type (voyemu_cockpit_service))
(allow system_app voyemu_cockpit_service (service_manager (add find)))
)RULES"
                       : R"CONTEXTS(qg.canbus u:object_r:voyemu_cockpit_service:s0
qg.setting u:object_r:voyemu_cockpit_service:s0
qg.display_manager u:object_r:voyemu_cockpit_service:s0
multidisplay u:object_r:voyemu_cockpit_service:s0
multiwindow u:object_r:voyemu_cockpit_service:s0
)CONTEXTS";
        auto output = std::string(input) + std::string(addition);

        if (policy && qinggan11_731) {
            output += "(allow platform_app voyemu_cockpit_service (service_manager (find)))\n";
        }

        const std::string_view output_pin =
                policy ? (qinggan11_731
                                  ? "b07971ed43c0ccfb97e69af4b251f58a0e021cb93c1e68d3ccbfc664845bb55d"
                                  : "ff445a22bb8718bed947ca97f05d091d0e62a87bb309165cc2c67945e7bd2d14")
                       : "ac366650a5d9e011d299845155d1319a1f35ec92e107cc1cded0973fa945f1b5";

        if (sha256_bytes(output) != output_pin) {
            throw VoyemuError("SELINUX_PATCH_MISMATCH", "API30 service output differs");
        }

        return output;
    }

    if (api != 28) {
        throw VoyemuError("SELINUX_PATCH_API", "Android API 28 required");
    }

    if (id == "selinux-hash-a9") {
        return sha256_bytes(input) + "\n";
    }

    if (id == "selinux-service-contexts-a9") {
        for (const auto entry : services) {
            if (input.find(entry) != std::string_view::npos) {
                throw VoyemuError("SELINUX_PATCH_MISMATCH", "service context already exists");
            }
        }

        auto result = one_newline(input);

        for (const auto entry : services) {
            result += entry;
            result += '\n';
        }

        return result;
    }

    if (id == "selinux-property-contexts-a9") {
        std::set<std::string> prefixes;
        std::istringstream lines{std::string(input)};
        std::string line;

        while (std::getline(lines, line)) {
            std::istringstream words(line);
            std::string prefix;
            words >> prefix;
            prefixes.insert(prefix);
        }

        for (const auto entry : properties) {
            if (prefixes.contains(std::string(entry.substr(0, entry.find(' '))))) {
                throw VoyemuError("SELINUX_PATCH_MISMATCH", "property prefix already exists");
            }
        }

        auto result = one_newline(input);

        for (const auto entry : properties) {
            result += entry;
            result += '\n';
        }

        return result;
    }

    throw VoyemuError("SELINUX_PATCH_NOT_FOUND", "unsupported context recipe");
}

std::string apply_passion_selinux_patch(std::string_view profile, unsigned api, std::string_view id,
                                        std::string_view input) {
    if (profile != "passion-11-russian-20260403-2.1.4.80" || api != 30) {
        throw VoyemuError("SELINUX_PATCH_PROFILE", "exact Passion API30 profile required");
    }

    std::string_view source_pin, output_pin, addition;

    if (id == "services") {
        source_pin = "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17";
        output_pin = "008fade9e6f183e228e7210a7bc9c30750c45f83ecfdb932ad92f61f0eeaadc4";
        addition =
                "\n; voyemu Passion API30 services\n(type voyemu_car_service)\n(roletype object_r voyemu_car_service)\n(type voyemu_nexus_service)\n(roletype object_r voyemu_nexus_service)\n(type voyemu_mega_car_service)\n(roletype object_r voyemu_mega_car_service)\n(typeattributeset service_manager_type (voyemu_car_service voyemu_nexus_service voyemu_mega_car_service))\n(allow system_app voyemu_car_service (service_manager (add find)))\n(allow platform_app voyemu_car_service (service_manager (find)))\n(allow system_app voyemu_nexus_service (service_manager (add find)))\n(allow platform_app voyemu_nexus_service (service_manager (find)))\n(allow system_app voyemu_mega_car_service (service_manager (add find)))\n(allow platform_app voyemu_mega_car_service (service_manager (find)))\n";
    } else if (id == "service-contexts") {
        source_pin = "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959";
        output_pin = "25c66b18abdb1bf102f5012387d54336c12a67c80e4ab540b90fdb863c1b82b6";
        addition =
                "car_service u:object_r:voyemu_car_service:s0\ncom.mega.NexusService u:object_r:voyemu_nexus_service:s0\ncom.mega.car.CarService u:object_r:voyemu_mega_car_service:s0\n";
    } else if (id == "property-contexts") {
        source_pin = "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e";
        output_pin = "1f322fc1fdea910bee266cc00126d2e8429b82a4a283250f12655e5d49b83d2c";
        addition = "boot.car_service_created u:object_r:system_prop:s0 exact bool\n";
    } else if (id == "policy-hash") {
        source_pin = "ef85f42a4dc4627987b0a8d8573a0795ee0fcbb882665635c72aba63be3236fc";
        output_pin = "243e1ba72d2c8e67417aa9443424c2063732ca932ce9add8530e4fdc907c2f10";
    } else {
        throw VoyemuError("SELINUX_PATCH_NOT_FOUND", "unsupported Passion policy recipe");
    }

    if (sha256_bytes(input) != source_pin) {
        throw VoyemuError(
                "SELINUX_PATCH_MISMATCH",
                "exact Passion cleanbase policy/context or patched CIL plus mapping required");
    }

    auto output = id == "policy-hash" ? std::string(source_pin) + "\n"
                                      : std::string(input) + std::string(addition);

    if (sha256_bytes(output) != output_pin) {
        throw VoyemuError("SELINUX_PATCH_MISMATCH", "Passion policy output differs");
    }

    return output;
}

BoundedCanbusPolicy apply_bounded_canbus_policy(unsigned api, std::string_view source,
                                                std::string_view oem_plat,
                                                std::string_view oem_vendor) {
    if (api != 28 && api != 30) {
        throw VoyemuError("SELINUX_PATCH_API", "Android API 28 or 30 required");
    }

    // API 30 policies carry the cockpit services of selinux-services-api30 instead of the A9
    // canbus services.
    const bool canbus = api == 28;
    constexpr std::string_view canbus_marker = "; voyemu A9 canbus",
                               permissive = "(typepermissive su)";
    const std::string_view bounded_marker = canbus ? "; voyemu A9 bounded su"
                                                   : "; voyemu API30 bounded su";
    const std::string mapping = "_" + std::to_string(api) + "_0";

    if (source.find(canbus_marker) != std::string_view::npos ||
        source.find(bounded_marker) != std::string_view::npos ||
        source.find(permissive) == std::string_view::npos) {
        throw VoyemuError("SELINUX_PATCH_MISMATCH",
                          "original permissive policy without markers required");
    }

    constexpr std::string_view canbus_rules[] = {
            "(type voyemu_canbus_service)",
            "(type voyemu_oem_service)",
            "(typeattributeset service_manager_type (voyemu_canbus_service voyemu_oem_service))",
            "(allow system_app voyemu_canbus_service (service_manager (add find)))",
            "(allow system_app voyemu_oem_service (service_manager (add find)))",
            "(allow platform_app voyemu_canbus_service (service_manager (find)))",
            "(allow platform_app voyemu_oem_service (service_manager (find)))",
            "(allow priv_app voyemu_canbus_service (service_manager (find)))"};
    constexpr std::string_view carveouts[] = {
            "(allow su system_data_file (file (relabelfrom)))",
            "(allow su system_app_data_file (file (relabelto setattr unlink open write append)))",
            "(allow su wallpaper_file (file (relabelto setattr open write append)))",
            "(allow su system_server (binder (call)))",
            "(allow su traced_enabled_prop (property_service (set)))"};
    auto cil = one_newline(source);

    if (canbus) {
        cil += "\n" + std::string(canbus_marker) + "\n";

        for (const auto rule : canbus_rules) {
            cil += rule;
            cil += '\n';
        }
    }

    std::set<std::string> known;
    const std::regex declaration(R"(\((?:type|typealias|typeattribute) ([\w-]+)\))");

    for (auto it = std::sregex_iterator(cil.begin(), cil.end(), declaration);
         it != std::sregex_iterator(); ++it) {
        known.insert((*it)[1].str());
    }

    std::string applied_rules;
    std::size_t applied = 0, skipped = 0;
    // The vendor policy names platform types through the mapping of api, where X_28_0 or X_30_0
    // is the attribute of X alone; unversioned names are platform attributes or vendor types.
    auto take = [&](std::string_view policy, const std::regex &allow, bool mapped) {
        std::istringstream lines{std::string(policy)};
        std::string line;

        while (std::getline(lines, line)) {
            std::smatch match;

            if (!std::regex_search(line, match, allow)) {
                continue;
            }

            auto target = match[1].str();

            if (mapped && target.ends_with(mapping)) {
                target.resize(target.size() - mapping.size());
            }

            // self names the subject itself, a valid target in every CIL policy.
            if (target == "self" || known.contains(target)) {
                applied_rules += "(allow su " + target + " " + match.suffix().str();
                applied_rules += '\n';
                ++applied;
            } else {
                ++skipped;
            }
        }
    };
    take(oem_plat, std::regex(R"(^\(allow su ([\w-]+) (?=\())"), false);
    take(oem_vendor, std::regex("^\\(allow su" + mapping + R"( ([\w-]+) (?=\())"), true);

    cil.erase(cil.find(permissive), permissive.size());
    cil = one_newline(cil) + "\n" + std::string(bounded_marker) + "\n" + applied_rules;

    for (const auto rule : carveouts) {
        cil += rule;
        cil += '\n';
    }

    return {std::move(cil), applied, skipped};
}
} // namespace voyemu
