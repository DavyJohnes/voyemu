#pragma once
#include "model.hpp"
namespace voyemu {
// Starts the OEM rpcd with the asynchronous MCU transport, so a delayed host RPC never holds its
// MCU_COM and RPCD_MAIN threads; shared by the qinggan09 and qinggan11 runtimes.
std::string qnx_native_mcu_async_runtime(std::string_view source);
std::string qnx_qinggan09_runtime(std::string_view source, std::string_view persistent_runtime = {},
                                  bool cluster_ui = false);
// Enable the Kanzi Gauge observer on the direct Gauge launch of the owned QNX runtime.
std::string qnx_kanzi_gauge_runtime(std::string_view runtime);
// Source preparation of the qinggan11 adapters; ordinary build dispatch uses the same pipeline.
Json prepare_qnx_qinggan11_images(const std::filesystem::path &root, std::string_view profile,
                                  const std::filesystem::path &fresh_directory);
// Compatibility entry for explicit source-only developer preparation.
Json prepare_qnx_qinggan11_developer_images(const std::filesystem::path &root,
                                            std::string_view profile,
                                            const std::filesystem::path &fresh_directory);
// Developer-only. The supplemental recovery is not a Passion original: the profile declares it in
// donor_inputs of its profile-qnx.json.
Json prepare_qnx_passion_developer_images(const std::filesystem::path &root,
                                          std::string_view profile,
                                          const std::filesystem::path &fresh_directory);
// The hardware adapter that a profile-qnx.json publishes; its status tells an incomplete
// definition.
std::string qnx_profile_adapter(const Json &definition);
// Whether a profile-qnx.json declares the Kanzi observer in the Gauge: gauge_observer "kanzi".
bool qnx_kanzi_gauge_observer(const Json &definition);
// An original of another profile that a profile-qnx.json declares in donor_inputs by profile, file
// and SHA-256; the file lies in the QNX originals directory of that profile.
struct QnxDonorInput {
    std::filesystem::path file;
    std::string sha256;
};
QnxDonorInput qnx_donor_input(const std::filesystem::path &root, const Json &definition,
                              std::string_view file);
// Four original firmware partitions + shipped sources + pinned upstream tools only.
// Produces images, never starts a VM or declares runtime acceptance.
Json prepare_qnx_images(const std::filesystem::path &root, std::string_view profile,
                        const std::filesystem::path &fresh_directory, bool offline = false);
} // namespace voyemu
