#pragma once
#include "download.hpp"
#include "model.hpp"
namespace voyemu {
struct OriginalInput {
    std::string name, file, sha256, remote, package;
};
Json nxp_acquisition_plan(const Json &definition);
std::vector<OriginalInput> original_input_plan(const Json &profile);
Json verify_original_inputs(const std::filesystem::path &root, std::string_view profile,
                            const std::vector<OriginalInput> &inputs);
// Local owner archive acquisition only; no downloads, Java, device or guest operations.
Json import_owner_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                 const std::filesystem::path &update,
                                 std::uint64_t reserve_bytes = 20ULL << 30);
// Downloads the development kernel of descriptor, android.development_kernel of profile.json: the
// CI-built release asset pinned by URL and SHA-256, verified into portable_development_kernel_file,
// which it returns. An asset that cannot be downloaded is DEVELOPMENT_KERNEL_ASSET_MISSING.
std::filesystem::path install_development_kernel(const std::filesystem::path &root,
                                                 const Json &descriptor, bool offline,
                                                 std::uint64_t reserve_bytes,
                                                 const DownloadTransport &transport = {});
Json fetch_native_inputs(const std::filesystem::path &root, std::string_view profile,
                         bool offline = false, const std::string &serial = "",
                         std::uint64_t reserve_bytes = 20ULL << 30,
                         const std::filesystem::path &jdk = {},
                         const std::filesystem::path &update = {});
} // namespace voyemu
