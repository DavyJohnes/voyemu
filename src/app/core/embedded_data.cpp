#include "embedded_data.hpp"
#include "error.hpp"
#include <miniz.h>
namespace voyemu {
std::string inflate_embedded(std::span<const unsigned char> deflated, std::size_t size) {
    std::string result(size, '\0');

    if (tinfl_decompress_mem_to_mem(result.data(), result.size(), deflated.data(), deflated.size(),
                                    0) != size) {
        throw VoyemuError("EMBEDDED_DATA", "embedded data does not inflate to its size");
    }

    return result;
}
} // namespace voyemu
