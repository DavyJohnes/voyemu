#pragma once
#include "model.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>

namespace voyemu {
inline void qnx_producer_origin(std::map<std::uint64_t, Json> &sources, std::uint64_t address,
                                const char *source) {
    auto &origins = sources[address];

    if (origins.is_null()) {
        origins = Json::array();
    }

    if (std::find(origins.begin(), origins.end(), source) == origins.end()) {
        origins.push_back(source);
    }
}

inline void qnx_producer_identity(std::uint64_t address, const std::array<std::uint32_t, 2> &page,
                                  std::size_t read_count, Json context,
                                  const std::function<void(const Json &)> &report) {
    if (read_count == 8 && page[0] == 0x58464751 && page[1] == 3) {
        return;
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string raw;
    const auto bytes = std::as_bytes(std::span(page));

    for (std::size_t i = 0; i < std::min(read_count, bytes.size()); ++i) {
        const auto byte = std::to_integer<unsigned>(bytes[i]);
        raw += hex[byte >> 4];
        raw += hex[byte & 15];
    }

    context.update({{"schema", 1},
                    {"producer_address", address},
                    {"read_count", read_count},
                    {"raw_hex", raw},
                    {"magic", read_count >= 4 ? Json(page[0]) : Json()},
                    {"version", read_count >= 8 ? Json(page[1]) : Json()}});
    try {
        report(context);
    } catch (...) { /* Ignored: diagnostics cannot replace the identity failure. */
    }

    throw VoyemuError("QNX_PRODUCER", "page identity " + context.dump());
}
} // namespace voyemu
