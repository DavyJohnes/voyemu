#include "graphics_ram.hpp"
#include "graphics.hpp"
#include <cstring>

namespace voyemu::graphics::detail {
std::span<std::byte> Ram::extent(std::uint64_t address, std::uint64_t length) {
    const auto memory = mapped();

    validate_ram_extent(address, length, memory.size());

    return memory.subspan(std::size_t(address - ram_base), std::size_t(length));
}

void Ram::write(std::uint64_t address, std::span<const std::byte> bytes) {
    const auto destination = extent(address, bytes.size());

    if (destination.data() != bytes.data()) {
        std::memcpy(destination.data(), bytes.data(), bytes.size());
    }
}
} // namespace voyemu::graphics::detail
