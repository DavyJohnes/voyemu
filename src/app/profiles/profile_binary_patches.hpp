#pragma once
#include "model.hpp"
#include "zip_archive.hpp"
#include <functional>
#include <map>
namespace voyemu {
struct ProfileBinaryRemoval {
    std::string entry, sha256;
};
struct ProfileBinaryPatch {
    std::filesystem::path source;
    std::string source_sha256;
    std::vector<ProfileBinaryRemoval> removals;
    std::vector<ZipAddition> additions;
    Json provenance;
    std::function<void()> revalidate;
};
// Partial steps only: MusicConcate still needs add-qg-runtime; QGIME variants also
// need add-qg-runtime and ime-display-routing. No ready machine or full-node claim.
ProfileBinaryPatch plan_profile_binary_patch(
        const std::filesystem::path &root, std::string_view profile, std::string_view node,
        const std::map<std::string, std::filesystem::path> &native_builds = {},
        const std::filesystem::path &compiler = {}, const std::string &compiler_sha256 = {});
} // namespace voyemu
