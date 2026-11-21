#pragma once
#include <cstddef>
namespace voyemu::os {
// Fills size bytes at data from the system random number generator; throws RANDOM on failure.
void random_bytes(unsigned char *data, std::size_t size);
} // namespace voyemu::os
