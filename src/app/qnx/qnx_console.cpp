#include "qnx_console.hpp"
#include "check.hpp"
#include "error.hpp"
#include "little_endian.hpp"
#include "model.hpp"
#include "process.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr CodeCheck need{"QNX console check failed"};

std::uint64_t field(std::string_view b, std::size_t at, unsigned n = 4) {
    return le_read(b, at, n, need, "QNX_CONSOLE_BOUNDS");
}

void put(std::string &b, std::size_t at, std::uint64_t v, unsigned n = 4) {
    le_write(b, at, v, n, need, "QNX_CONSOLE_BOUNDS");
}
} // namespace

void qnx_a11_console_layout_guard(std::string_view original) {
    need(original.size() >= 0x69108 && original.size() <= 16 * 1024 * 1024, "QNX_CONSOLE_LAYOUT");

    need(field(original, 0x3578) == 0x940005ba && field(original, 0x357c) == 0x940088f9,
         "QNX_CONSOLE_DIRECT_PROBE");
    need(field(original, 0x60ab0, 8) == 0x8572b7b8 &&
                 original.substr(0x2e5b8, 8) == std::string_view("ser8150\0", 8) &&
                 field(original, 0x60ac8, 8) == 0x85701ef0 &&
                 field(original, 0x60ad0, 8) == 0x85701dd0 &&
                 field(original, 0x60ad8, 8) == 0x857017b8,
         "QNX_CONSOLE_CALLBACKS");
    need(field(original, 0x60ae0, 8) == 0x85701728 && field(original, 0x60ae8, 8) == 0x85701750 &&
                 field(original, 0x60e10, 8) == 0x857655f8 &&
                 original.substr(0x683f8, 32) == std::string(32, 0),
         "QNX_CONSOLE_DEVICE_RECORDS");
    need(field(original, 0x45038, 8) == 0 && field(original, 0x45040, 8) == 0 &&
                 field(original, 0x45048, 8) == 0 &&
                 field(original, 0x453e8, 8) == 0x0060000017a00401ULL &&
                 field(original, 0x5cc78, 8) == 0xff000044eeaa0400ULL,
         "QNX_CONSOLE_DEVICE_MAPPING");
}

std::string adapt_qnx_pl011_console(QnxGuestAdapter adapter, std::string_view original,
                                    std::string_view pin, std::string_view prepared,
                                    std::string_view own) {
    const auto &spec = qnx_adapter(adapter);

    need(spec.platform != QnxGuestPlatform::qinggan09 && pin == spec.hyp_sha256 &&
                 original.size() == spec.hyp_bytes && sha256_bytes(original) == pin,
         "QNX_CONSOLE_ORIGINAL_SHA");
    qnx_a11_console_layout_guard(original);
    need(own.size() == 112, "QNX_CONSOLE_OWN_BLOB");
    need(prepared.size() >= 0x69108 && prepared.size() <= 16 * 1024 * 1024 &&
                 field(prepared, 0x3000) == 0x00ff7eeb && field(prepared, 0x3020) == 0x66108,
         "QNX_CONSOLE_PREPARED_LAYOUT");

    // Every preimage is checked before copying or mutating the derived image. Reapplication fails
    // here.
    for (const auto &[at, n] : std::array<std::array<std::size_t, 2>, 6>{{{0x45cc, 112},
                                                                          {0x60ac8, 16},
                                                                          {0x45040, 8},
                                                                          {0x4584, 4},
                                                                          {0x3578, 4},
                                                                          {0x4210, 0xf4}}}) {
        need(prepared.substr(at, n) == original.substr(at, n), "QNX_CONSOLE_PREPARED_PREIMAGE");
    }

    std::uint32_t sum = 0;

    for (std::size_t at = 0x3000; at < 0x69108; at += 4) {
        sum += static_cast<std::uint32_t>(field(prepared, at));
    }

    need(sum == 0, "QNX_CONSOLE_PREPARED_CHECKSUM");
    std::string output(prepared);
    output.replace(0x45cc, own.size(), own);
    put(output, 0x60ac8, 0x85701800, 8);
    put(output, 0x60ad0, 0x857017f4, 8);
    put(output, 0x45040, 0x0060000009000401ULL, 8);
    put(output, 0x4584, 0xd2820000);
    put(output, 0x3578, 0x94000429);
    put(output, 0x69104, 0);
    sum = 0;

    for (std::size_t at = 0x3000; at < 0x69104; at += 4) {
        sum += static_cast<std::uint32_t>(field(output, at));
    }

    put(output, 0x69104, 0 - sum);
    need(output.size() == prepared.size() && output.substr(0x69108) == prepared.substr(0x69108),
         "QNX_CONSOLE_PACKED_BINDING");

    return output;
}
} // namespace voyemu
