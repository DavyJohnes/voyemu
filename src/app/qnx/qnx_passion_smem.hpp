#pragma once
#include "qnx_bootstrap.hpp"
#include <cstdint>
namespace voyemu {
// Passion uses the already-backed low RAM identity block. Keep the first 4MiB
// outside sysram: QEMU DTB, IPL at40200000 and tables at40300000/40301000.
inline constexpr bool qnx_passion_smem_bank_valid(std::uint64_t base, std::uint64_t bytes) {
    constexpr std::uint64_t boot_end = 0x40400000, old_base = 0x50000000, bank_end = 0xb0000000;

    return base >= boot_end && base <= old_base && base % 4096 == 0 && bytes % 4096 == 0 &&
           bytes == bank_end - base;
}

inline std::string qnx_passion_smem_memory() {
    constexpr std::uint64_t base = 0x40400000, bytes = 0x6fc00000;

    static_assert(qnx_passion_smem_bank_valid(base, bytes));

    return qnx_smem_memory(base, bytes);
}
} // namespace voyemu
