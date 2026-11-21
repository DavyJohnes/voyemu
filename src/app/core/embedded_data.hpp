#pragma once
#include <cstddef>
#include <span>
#include <string>
namespace voyemu {
// The size bytes that src/app/embedded_deflate.cmake deflated into the raw deflate stream deflated.
std::string inflate_embedded(std::span<const unsigned char> deflated, std::size_t size);
} // namespace voyemu
