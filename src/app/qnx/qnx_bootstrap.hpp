#pragma once
#include <cstdint>
#include <string>
namespace voyemu {
std::string qnx_smem_memory(std::uint64_t base = 0x50000000, std::uint64_t bytes = 0x60000000);
}
