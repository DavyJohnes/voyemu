#pragma once
#include "file_patch.hpp"
#include "model.hpp"
#include <functional>
#include <set>
namespace voyemu {
struct ProfileFileNodes {
    std::vector<PinnedFilePatch> patches;
    Json provenance;
    std::function<void()> revalidate;
};
// Binds registered API28/API30 finite profile file families to native ext4 receipts
// and exact profile/source/input/output/dependency pins.
// Returns an in-memory, fully checked patch batch; does not install or publish a machine.
// API28 hands off only the exact codec output; existing eleven Java file recipes remain unchanged.
ProfileFileNodes plan_profile_codec_file_node(const std::filesystem::path &root,
                                              std::string_view profile,
                                              const std::filesystem::path &system_extraction);
// The file patch steps that the file node planner registers.
const std::set<std::string> &file_steps();
ProfileFileNodes plan_profile_file_nodes(const std::filesystem::path &root,
                                         std::string_view profile,
                                         const std::filesystem::path &system_extraction,
                                         const std::filesystem::path &vendor_extraction);
} // namespace voyemu
