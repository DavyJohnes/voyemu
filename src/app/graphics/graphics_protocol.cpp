#include "graphics_protocol.hpp"
#include <map>

namespace voyemu::graphics {
using namespace detail;
using U = std::uint32_t;

void validate_mcd_batch(std::uint64_t count, std::uint64_t dropped,
                        std::span<const std::byte> data) {
    need(count >= 1 && count <= 32 && dropped < 0x100000000ULL && data.size() == count * 32,
         "Mcd trace geometry");

    for (std::size_t at = 0; at < data.size(); at += 32) {
        const auto message = u32(data, at), source = u32(data, at + 4),
                   destination = u32(data, at + 8);
        const bool legacy = message >= 1 && message <= 23 &&
                            (source == 6 || source == 7 || source == 17 || destination == 6 ||
                             destination == 7 || destination == 17);
        const bool language = message == 24 && source >= 1 && source <= 32 && destination == 8 &&
                              u32(data, at + 12) == 8 && u32(data, at + 16) == 3 &&
                              u32(data, at + 20) <= 1;
        need(legacy || language, "Mcd trace scope");
    }
}

void validate_ram_extent(std::uint64_t physical, std::uint64_t size, std::uint64_t extent) {
    need(extent == ram_bytes || extent == (11713ULL << 20), "RAM configured geometry");
    need(size > 0 && size <= max_blob && physical >= ram_base && physical - ram_base <= extent &&
                 size <= extent - (physical - ram_base),
         "RAM transfer geometry");
}

void validate_inline_client(const Arguments &a, std::span<const std::byte> data) {
    std::size_t unit = 0;

    switch (a[2]) {
    case 0x1400:
    case 0x1401:
        unit = 1;
        break;
    case 0x1402:
    case 0x1403:
    case 0x140b:
        unit = 2;
        break;
    case 0x1404:
    case 0x1405:
    case 0x1406:
    case 0x140c:
        unit = 4;
        break;
    default:
        break;
    }

    need(a[0] < 64 && a[1] >= 1 && a[1] <= 4 && unit && a[3] <= 255 && a[4] <= max_blob && !a[5] &&
                 a[6] > 0 && a[6] <= rpc_payload_bytes,
         "Inline client attribute metadata");
    const auto element = a[1] * unit, step = a[4] ? a[4] : element;
    need(!data.empty() && data.size() <= rpc_payload_bytes &&
                 (a[6] - 1) * step + element == data.size(),
         "Inline client attribute extent");
}

void validate_inline_uniform(std::string_view name, const Arguments &a,
                             std::span<const std::byte> data) {
    std::size_t unit = 0;

    if (name == "glUniform1fv") {
        unit = 4;
    } else if (name == "glUniform3fv") {
        unit = 12;
    } else if (name == "glUniform4fv") {
        unit = 16;
    } else if (name == "glUniformMatrix3fv") {
        unit = 36;
    } else if (name == "glUniformMatrix4fv") {
        unit = 64;
    }

    need(unit && a[1] <= rpc_payload_bytes / unit && data.size() == a[1] * unit,
         "Inline uniform extent");
}

bool detail::clear_vector(std::string_view name) {
    return name == "glClearBufferfv" || name == "glClearBufferiv" || name == "glClearBufferuiv";
}

void detail::validate_inline_clear(std::string_view name, const Arguments &a,
                                   std::span<const std::byte> data) {
    need(clear_vector(name) && !a[2] && data.size() == (a[0] == 0x1800 ? 16u : 4u),
         "Inline clear extent");
}

std::vector<BatchRecord> validate_batch(std::uint64_t count, std::span<const std::byte> data) {
    need(count >= 1 && count <= 64 && !data.empty() && data.size() <= rpc_payload_bytes,
         "GL batch extent");
    std::size_t at = 0;
    std::vector<BatchRecord> records;

    for (std::uint64_t i = 0; i < count; i++) {
        need(at + 16 <= data.size(), "GL batch header");
        U op = u32(data, at), argc = u32(data, at + 4), size = u32(data, at + 8),
          reserved = u32(data, at + 12);
        at += 16;
        need(!reserved && argc <= 8 && static_cast<std::size_t>(argc) * 8 <= data.size() - at &&
                     size <= data.size() - at - static_cast<std::size_t>(argc) * 8,
             "GL batch record");
        BatchRecord r{op, {}, {}};

        for (U j = 0; j < argc; j++) {
            r.arguments[j] = u64(data, at);
            at += 8;
        }

        r.payload.assign(data.begin() + at, data.begin() + at + size);
        at += size;
        const auto padded = (at + 7) & ~std::size_t(7);
        need(padded <= data.size(), "GL batch padding");

        for (; at < padded; at++) {
            need(data[at] == std::byte{0}, "GL batch padding");
        }

        static const std::map<U, U> core{{22, 4}, {23, 1}, {27, 1}, {30, 2}, {31, 1},
                                         {33, 1}, {34, 4}, {35, 3}, {36, 1}, {37, 1}};

        if (auto found = core.find(op); found != core.end()) {
            need(argc == found->second && r.payload.empty(), "GL scalar batch arity");
        } else if (op == 350) {
            need(argc == 7, "GL attribute batch arity");
            validate_inline_client(r.arguments, r.payload);
        } else {
            const auto &s = detail::command_spec(op);
            need(s.returns_void && argc == s.argc, "GL batch scope/arity");

            if (std::string_view(s.category) == "scalar" && s.pointer < 0) {
                need(r.payload.empty(), "GL scalar batch payload");
            } else if (std::string_view(s.category) == "vector" &&
                       (std::string_view(s.name).starts_with("glUniform") ||
                        clear_vector(s.name))) {
                need(!r.arguments[s.pointer], "GL batch vector source");

                if (clear_vector(s.name)) {
                    validate_inline_clear(s.name, r.arguments, r.payload);
                } else {
                    validate_inline_uniform(s.name, r.arguments, r.payload);
                }
            } else {
                throw VoyemuError("GRAPHICS", "GL batch unsupported semantics");
            }
        }

        records.push_back(std::move(r));
    }

    need(at == data.size(), "GL batch trailing data");

    return records;
}
} // namespace voyemu::graphics
