#pragma once
#include "model.hpp"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
struct PinnedFileDependency {
    std::filesystem::path input;
    std::string sha256;
};
struct PinnedFilePatch {
    std::string id;
    unsigned api;
    std::filesystem::path input;
    std::string input_sha256, output_sha256;
    std::optional<PinnedFileDependency> donor;
    std::string patched_dependency, dependency_sha256;
    std::string profile;
    std::optional<PinnedFileDependency> certificate_apk;
    // OEM vendor policy of the bounded A9 su; donor is the OEM plat policy.
    std::optional<PinnedFileDependency> vendor_donor;
};
std::vector<std::string> native_file_patch_steps(std::string_view id);
// Entire graph is verified in memory; no partial batch is returned on failure.
std::vector<std::string> prepare_file_patches(std::span<const PinnedFilePatch> patches);
// The receipt entry that publish_file_patches writes for a patch and its output.
Json file_patch_receipt_entry(const PinnedFilePatch &patch);
// A new private draft and its provenance receipt become visible together.
void publish_file_patches(std::span<const PinnedFilePatch> patches,
                          const std::filesystem::path &destination);
} // namespace voyemu
