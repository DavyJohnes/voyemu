#pragma once
#include "check.hpp"
#include "graphics.hpp"
#include "little_endian.hpp"
#include <cstring>

namespace voyemu::graphics::detail {
inline constexpr std::size_t max_blob = 64 * 1024 * 1024;
inline constexpr Check need{"GRAPHICS"};

inline std::uint32_t u32(std::span<const std::byte> data, std::size_t at) {
    return std::uint32_t(le_read(data, at, 4, need, "Protocol u32 extent"));
}

inline std::uint64_t u64(std::span<const std::byte> data, std::size_t at) {
    return le_read(data, at, 8, need, "Protocol u64 extent");
}

struct CommandSpec {
    std::uint32_t op;
    const char *name;
    const char *category;
    unsigned argc;
    int pointer;
    bool returns_void;
};

// Metadata is generated once alongside the renderer's invoker, never copied into validators.
const CommandSpec &command_spec(std::uint32_t opcode);
bool clear_vector(std::string_view name);
void validate_inline_clear(std::string_view, const Arguments &, std::span<const std::byte>);
} // namespace voyemu::graphics::detail
