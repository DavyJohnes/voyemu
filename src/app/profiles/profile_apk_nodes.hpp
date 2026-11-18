#pragma once
#include "apk_build.hpp"
namespace voyemu {
struct ProfileBinaryApkNode {
    std::string profile, name;
    ApkBuildRequest request;
};
// Binds the two currently supported binary-only profile nodes to original APK pins
// and a native qg-runtime producer receipt matching the current profile source tree.
// Does not compile, install, run TypeScript, or declare the complete machine build ready.
ProfileBinaryApkNode plan_profile_binary_apk_node(const std::filesystem::path &root,
                                                  std::string_view profile, std::string_view node,
                                                  const std::filesystem::path &qg_runtime_build,
                                                  const std::filesystem::path &compiler,
                                                  const std::string &compiler_sha256);
ApkBuildDraft build_profile_binary_apk_node(ProfileBinaryApkNode node,
                                            const std::filesystem::path &zipalign,
                                            const std::string &zipalign_sha256,
                                            const ApkSigningIdentity &identity,
                                            const std::string &base_certificate_sha256,
                                            const std::filesystem::path &destination,
                                            std::uint64_t reserve_bytes = 12ULL << 30);
} // namespace voyemu
