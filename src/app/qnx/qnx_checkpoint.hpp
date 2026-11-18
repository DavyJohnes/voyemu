#pragma once
#include "model.hpp"
#include <string>
#include <vector>

namespace voyemu {
struct QnxCheckpoint {
    bool resume = false, external_ram = false;
    std::string reason = "disabled", manifest_sha256;
    std::filesystem::path directory;
    Json launch, verified_files, boot_roms;
    std::string verification = "not_checked";
    std::uint32_t state_sequence = 0, theme_sequence = 0;
    std::vector<std::uint64_t> producers;
    std::size_t omitted_loaders = 0;
    std::uint64_t omitted_loader_bytes = 0;
};
Json qnx_checkpoint_key(const Json &cold, const std::string &profile);
QnxCheckpoint inspect_qnx_checkpoint(const Json &cold, const std::string &profile,
                                     bool reuse_verified_files = false);
QnxCheckpoint
runtime_qnx_checkpoint(const Json &cold,
                       const std::string &profile); // Bounded configuration/ownership reads, no
                                                    // payload SHA or directory walk.
// Explicit start-time mutation; inspection itself never creates a receipt. False means the verified
// epoch changed.
bool remember_qnx_checkpoint_verification(const QnxCheckpoint &verified);
// Runtime failures after acknowledged restore are not evidence of a corrupt cache.
void reject_qnx_checkpoint(const QnxCheckpoint &, std::string_view reason,
                           bool restore_ready = false);
bool qnx_checkpoint_migration_ready(const Json &migration, const Json &status);
// Shared CPR snapshots use a private APFS clone. No guest may run before ROM repair and incoming
// complete.
Json qnx_checkpoint_boot_roms(std::string_view response, const Json &cold = Json{});
Json clone_qnx_checkpoint_ram(const QnxCheckpoint &, const std::filesystem::path &target);
void restore_qnx_checkpoint_boot_roms(const QnxCheckpoint &, const std::filesystem::path &target,
                                      std::string_view actual_roms);
} // namespace voyemu
