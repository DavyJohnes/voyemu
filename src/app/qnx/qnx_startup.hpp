#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace voyemu {
// Layout-only numeric guard; callers authenticate their exact OEM model/SHA first.
std::uint64_t qnx_a11_ram_identity_mapping(std::string_view original);
// Missing platform CPU name uses the existing OEM Unknown fallback.
void qnx_a11_cpu_name_guard(std::string_view original);
// Guard the actual SMP8 GIC aperture as Device memory in the OEM page tables.
std::uint64_t qnx_a11_gic_device_mapping(std::string_view original);
// Layout-only guard for two source-owned absent-storage query wrappers.
void qnx_a11_boot_storage_query_guard(std::string_view original);
// Preserve the OEM read-cookie error contract when physical misc storage is absent.
std::uint32_t qnx_a11_misc_failure_guard(std::string_view original);
// First stage only: guarded SA8155 startup adaptation, before host IFS packing.
// Does not claim a bootable kernel or modify the embedded compressed stream.
std::string prepare_qnx_startup(std::string_view original, std::string_view sha256);
std::string prepare_qnx_qinggan11_startup(std::string_view original, std::string_view sha256);
std::string prepare_qnx_qinggan11_older_startup(std::string_view original, std::string_view pin);
std::string assemble_qnx_qinggan11_older_kernel(std::string_view original, std::string_view pin,
                                                std::string_view packed);
std::string assemble_qnx_qinggan11_kernel(std::string_view original, std::string_view sha256,
                                          std::string_view packed_host_ifs);
std::string assemble_qnx_kernel(std::string_view original, std::string_view sha256,
                                std::string_view packed_host_ifs);
} // namespace voyemu
