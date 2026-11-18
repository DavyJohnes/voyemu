#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace voyemu {
struct DexInventory {
    std::string version;
    std::uint32_t methods{};
    std::vector<std::string> classes;
};
// Bounded metadata reader for pinned DEX inputs, not a bytecode verifier or rewriter.
DexInventory inspect_dex_inventory(std::span<const std::byte> bytes);
struct DexInput {
    std::string entry;
    std::span<const std::byte> bytes;
};
struct DexTemplate {
    std::span<const std::byte> bytes;
    std::vector<std::string> expected_classes;
};
struct DexAppendSlot {
    std::string entry;
    DexInventory inventory;
};
// Inputs and the exact template class allowlists must be pinned by the caller.
// Plans new contiguous slots only; collisions require an explicit rewrite, never shadowing.
std::vector<DexAppendSlot> plan_dex_append(std::span<const DexInput> original,
                                           std::span<const DexTemplate> templates);
} // namespace voyemu
