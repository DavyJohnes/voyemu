#pragma once
#include "qnx_gpt.hpp"
#include "qnx_passion.hpp"
#include <string>
#include <string_view>
namespace voyemu {
// Developer-only B candidate: actual QEMU RAM [40000000,31c100000).
// This does not configure QEMU, enable services, modify policy, or run a guest.
QnxPassionPlacement qnx_passion_11713_placement();
struct QnxPassion11713Bootstrap {
    std::string startup, early_l2;
};
void validate_qnx_passion_original_fdt_layout(std::string_view fdt);
std::string qnx_passion_11713_fdt(std::string_view original_fdt);
// Same4096B early table: only already-backed IFS-B prefix identity leaves.
std::string qnx_passion_11713_ifs2_early_map(std::string_view old_table);
QnxPassion11713Bootstrap prepare_qnx_passion_11713_bootstrap(std::string_view original);
std::string prepare_qnx_passion_11713_runtime(std::string_view original_runtime);
void validate_qnx_passion_11713_ifs2_names(std::string_view original);
void validate_qnx_passion_11713_sync_dma(std::string_view original);
std::string adapt_qnx_passion_11713_startupmgr(std::string_view original,
                                               std::string_view default_reader,
                                               std::string_view source_built_reader);
std::string assemble_qnx_passion_11713_kernel(std::string_view original, std::string_view packed,
                                              std::string_view host_sha256);
// Explicit developer-only single-4K OWN storage candidate; original policy/ECU unchanged.
void validate_qnx_passion_11713_bootdev(std::string_view original);
void validate_qnx_passion_11713_config_device(std::string_view original_manager);
QnxPassion11713Bootstrap prepare_qnx_passion_11713_config_bootstrap(std::string_view original);
std::string adapt_qnx_passion_11713_config_startupmgr(std::string_view original,
                                                      std::string_view reader,
                                                      std::string_view large_reader);
QnxIfs2Gpt qnx_passion_11713_config_gpt(std::uint64_t ifs_bytes);
void validate_qnx_passion_11713_config_gpt(const QnxIfs2Gpt &, std::uint64_t ifs_bytes);
std::string qnx_passion_11713_blank_config();
std::string assemble_qnx_passion_11713_config_kernel(std::string_view original,
                                                     std::string_view packed,
                                                     std::string_view host_sha256);
void validate_qnx_passion_11713_ifs_integrity_source(std::string_view original_manager);
} // namespace voyemu
