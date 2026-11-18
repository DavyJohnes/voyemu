#pragma once
#include "model.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
namespace voyemu {
// Diagnostic only: candidate kernel mapping is accepted after OEM code and PID guards.
using QnxPhysicalReader = std::function<bool(std::uint64_t, std::span<std::byte>)>;
// Candidate historical TTBR1/TCR only; a translation alone does not validate live mapping.
std::optional<std::uint64_t> qnx_boot_candidate_physical(const QnxPhysicalReader &read,
                                                         std::uint64_t address);
// Prior observations are only hints: live PID, slot, name and epoch are rechecked.
struct QnxBootProcessTarget {
    std::uint32_t pid;
    std::string name;
};
Json read_qnx_boot_progress(const QnxPhysicalReader &read,
                            std::span<const QnxBootProcessTarget> targets = {});
} // namespace voyemu
