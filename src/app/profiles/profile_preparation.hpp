#pragma once
#include "model.hpp"
namespace voyemu {
// Materializes declarative metadata from SHA-bound JSON recipe sources.
Json profile_patch_metadata(const std::filesystem::path &root, std::string_view profile);
struct ProfilePreparationArtifacts {
    Json result, receipt;
    std::filesystem::path base, sdk, system_inputs, vendor_inputs, work, transport;
};
// Logical clean-image inputs required by profile patches, native guestLibs and platform signing.
std::vector<std::string> native_profile_android_inputs(const std::filesystem::path &root,
                                                       std::string_view profile);
// Space floor for bounded clean-image extraction; exact NXP API28 uses its approved build reserve.
std::uint64_t native_profile_android_input_reserve(const Json &profile);
// Space floor for native executables; only the exact registered 6.11 profile uses its approved
// budget.
std::uint64_t native_profile_executable_reserve(const Json &profile, std::string_view name);
// Read-only lookup of a complete pinned extraction containing the requested paths.
// Returns an empty path on a cache miss; never creates or relabels an extraction.
std::filesystem::path cached_native_android_inputs(const std::filesystem::path &cache,
                                                   const std::string &image_sha256,
                                                   const std::vector<std::string> &paths,
                                                   std::string_view logical_partition = {});
// Real artifact preparation only. Uses installed JDK25+, existing native producers/Java helper;
// never launches a VM, installs APKs or declares a prepared machine ready.
ProfilePreparationArtifacts prepare_profile_artifacts(const std::filesystem::path &root,
                                                      std::string_view profile,
                                                      bool offline = false,
                                                      const std::filesystem::path &jdk = {});
} // namespace voyemu
