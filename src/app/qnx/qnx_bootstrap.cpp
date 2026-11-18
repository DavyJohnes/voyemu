#include "qnx_bootstrap.hpp"
#include "error.hpp"
#include <stdexcept>
namespace voyemu {
std::string qnx_smem_memory(std::uint64_t base, std::uint64_t bytes) {
    if (base % 4096 || bytes % 4096 || bytes < 64 * 1024 * 1024 || base > 0xffffffffULL ||
        bytes > 0x100000000ULL - base) {
        throw VoyemuError("QNX_SMEM_RAM_GEOMETRY", "unsupported shared memory RAM geometry");
    }

    std::string output(2 * 1024 * 1024, 0);
    auto put = [&](std::size_t at, std::uint64_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) {
            output[at + i] = static_cast<char>(value >> (i * 8));
        }
    };
    // Qualcomm SMEM partition table, one item 402 describing the emulated RAM bank.
    put(0x1ff000, 0x434f5424, 4);
    put(0x1ff008, 1, 4);
    put(0x1ff020, 0x1000, 4);
    put(0x1ff02c, 0xfffefffe, 4);
    put(0x1000, 0x54525024, 4);
    put(0x1004, 0xfffefffe, 4);
    put(0x100c, 0x90, 4);
    put(0x1020, 0xa5a5, 2);
    put(0x1022, 402, 2);
    put(0x1024, 0x60, 4);
    put(0x1030, 0xaf9ec4e29da5e0a8ULL, 8);
    put(0x1040, 1, 4);
    put(0x1058, base, 8);
    put(0x106c, 14, 4);
    put(0x1074, 1, 4);
    put(0x1088, bytes, 8);

    return output;
}
} // namespace voyemu
