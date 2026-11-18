#pragma once
#include "model.hpp"
namespace voyemu {
// Starts the OEM rpcd with the asynchronous MCU transport, so a delayed host RPC never holds its
// MCU_COM and RPCD_MAIN threads; shared by the Free A9 and Free A11 runtimes.
std::string qnx_native_mcu_async_runtime(std::string_view source);
std::string qnx_qinggan09_runtime(std::string_view source, std::string_view persistent_runtime = {},
                                  bool cluster_ui = false);
// Enable the Kanzi Gauge observer on the direct Gauge launch of the owned QNX runtime.
std::string qnx_kanzi_gauge_runtime(std::string_view runtime);
// Exact Free A11 7.3.1 source preparation; ordinary build dispatch uses the same guarded pipeline.
Json prepare_qnx_qinggan11_images(const std::filesystem::path &root, std::string_view profile,
                                  const std::filesystem::path &fresh_directory);
// Compatibility entry for explicit source-only developer preparation.
Json prepare_qnx_qinggan11_developer_images(const std::filesystem::path &root,
                                            std::string_view profile,
                                            const std::filesystem::path &fresh_directory);
// Explicit owner-captured supplemental recovery is not a Passion original. Developer-only.
Json prepare_qnx_passion_developer_images(const std::filesystem::path &root,
                                          const std::filesystem::path &supplemental_recovery,
                                          const std::filesystem::path &fresh_directory);
// The hardware adapter that a profile-qnx.json publishes; its status tells an incomplete
// definition.
std::string qnx_profile_adapter(const Json &definition);
// Four original firmware partitions + shipped sources + pinned upstream tools only.
// Produces images, never starts a VM or declares runtime acceptance.
Json prepare_qnx_images(const std::filesystem::path &root, std::string_view profile,
                        const std::filesystem::path &fresh_directory, bool offline = false);
} // namespace voyemu
