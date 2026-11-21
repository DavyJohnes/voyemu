#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace voyemu {
// Whether width bytes at offset at lie inside a buffer of size bytes, without offset overflow; a
// field is at most eight bytes wide.
constexpr bool le_extent(std::size_t size, std::uint64_t at, unsigned width) {
    return width <= 8 && at <= size && width <= size - at;
}

// Reads the little-endian field of width bytes at at; require(false, extent_error) rejects a field
// outside bytes, so each reader keeps its own error code and message.
template <typename Require>
std::uint64_t le_read(std::string_view bytes, std::uint64_t at, unsigned width, Require require,
                      const char *extent_error) {
    require(le_extent(bytes.size(), at, width), extent_error);
    std::uint64_t value = 0;

    for (unsigned i = 0; i < width; ++i) {
        value |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    }

    return value;
}

template <typename Require>
std::uint64_t le_read(std::span<const std::byte> bytes, std::uint64_t at, unsigned width,
                      Require require, const char *extent_error) {
    return le_read(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()), at,
                   width, require, extent_error);
}

// Writes the low width bytes of value at at in little-endian order, with the extent check of
// le_read.
template <typename Require>
void le_write(std::string &bytes, std::uint64_t at, std::uint64_t value, unsigned width,
              Require require, const char *extent_error) {
    require(le_extent(bytes.size(), at, width), extent_error);

    for (unsigned i = 0; i < width; ++i) {
        bytes[at + i] = static_cast<char>(value >> (i * 8));
    }
}
} // namespace voyemu
