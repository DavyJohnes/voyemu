#pragma once
#include "error.hpp"
#include "process.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace voyemu::lp_fixture {
inline void field(std::string &s, std::size_t at, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        s.at(at + i) = static_cast<char>(v >> (i * 8));
    }
}

inline std::uint32_t crc(std::string_view s) {
    std::uint32_t v = 0xffffffff;

    for (unsigned char c : s) {
        v ^= c;

        for (unsigned i = 0; i < 8; i++) {
            v = (v >> 1) ^ (0xedb88320 & std::uint32_t(-std::int32_t(v & 1)));
        }
    }

    return v ^ 0xffffffff;
}

inline std::string digest(std::string_view s) {
    auto h = sha256_bytes(s);
    std::string out;

    for (std::size_t i = 0; i < h.size(); i += 2) {
        out += static_cast<char>(std::stoul(h.substr(i, 2), nullptr, 16));
    }

    return out;
}

inline void seal_header(std::string &m) {
    std::fill(m.begin() + 12, m.begin() + 44, 0);
    m.replace(48, 32, digest(std::string_view(m).substr(128, 264)));
    m.replace(12, 32, digest(std::string_view(m).substr(0, 128)));
}

inline void metadata(std::string &s, const std::function<void(std::string &)> &mutate) {
    auto m = s.substr(65536 + 12288, 4096);

    mutate(m);
    seal_header(m);

    for (unsigned i = 0; i < 4; i++) {
        s.replace(65536 + 12288 + i * 4096, 4096, m);
    }
}

inline void geometry(std::string &image, const std::function<void(std::string &)> &mutate) {
    auto g = image.substr(65536 + 4096, 52);

    mutate(g);
    std::fill(g.begin() + 8, g.begin() + 40, 0);
    g.replace(8, 32, digest(g));
    image.replace(65536 + 4096, 52, g);
    image.replace(65536 + 8192, 52, g);
}

inline std::string fixture(std::string_view payload = {}) {
    const std::uint64_t logical_bytes =
            std::max<std::uint64_t>(8192, (payload.size() + 4095) / 4096 * 4096);
    const std::uint64_t super_bytes = std::max<std::uint64_t>(131072, 32768 + logical_bytes + 4096);
    const std::uint64_t disk_bytes = 65536 + super_bytes + 65536;

    if (payload.size() > 4 * 1024 * 1024) {
        throw VoyemuError("FIXTURE", "LP test payload exceeds bound");
    }

    std::string s(disk_bytes, 0), table(256, 0);
    table[0] = 1;
    field(table, 32, 40, 8);
    field(table, 40, 47, 8);
    const std::string vb = "vbmeta";

    for (unsigned i = 0; i < vb.size(); i++) {
        table[56 + i * 2] = vb[i];
    }

    table[128] = 2;
    field(table, 160, 128, 8);
    field(table, 168, (65536 + super_bytes) / 512 - 1, 8);

    for (unsigned i = 0; i < 5; i++) {
        table[184 + i * 2] = "super"[i];
    }

    s.replace(1024, 256, table);
    std::string h(512, 0);
    h.replace(0, 8, "EFI PART");
    field(h, 8, 0x10000, 4);
    field(h, 12, 92, 4);
    field(h, 24, 1, 8);
    field(h, 32, disk_bytes / 512 - 1, 8);
    field(h, 40, 34, 8);
    field(h, 48, disk_bytes / 512 - 2, 8);
    field(h, 72, 2, 8);
    field(h, 80, 2, 4);
    field(h, 84, 128, 4);
    field(h, 88, crc(table), 4);
    field(h, 16, crc(h.substr(0, 92)), 4);
    s.replace(512, 512, h);
    std::string g(52, 0);
    field(g, 0, 0x616c4467, 4);
    field(g, 4, 52, 4);
    field(g, 40, 4096, 4);
    field(g, 44, 2, 4);
    field(g, 48, 4096, 4);
    g.replace(8, 32, digest(g));
    s.replace(65536 + 4096, 52, g);
    s.replace(65536 + 8192, 52, g);
    std::string m(4096, 0);
    field(m, 0, 0x414c5030, 4);
    field(m, 4, 10, 2);
    field(m, 8, 128, 4);
    field(m, 44, 264, 4);
    unsigned offsets[] = {0, 104, 152, 200}, counts[] = {2, 2, 1, 1}, widths[] = {52, 24, 48, 64};

    for (unsigned i = 0; i < 4; i++) {
        field(m, 80 + i * 12, offsets[i], 4);
        field(m, 84 + i * 12, counts[i], 4);
        field(m, 88 + i * 12, widths[i], 4);
    }

    m.replace(128, 6, "system");
    field(m, 128 + 36, 1, 4);
    field(m, 128 + 44, 1, 4);
    m.replace(180, 10, "system_ext");
    field(m, 180 + 36, 1, 4);
    field(m, 180 + 40, 1, 4);
    field(m, 180 + 44, 1, 4);
    field(m, 232, logical_bytes / 512, 8);
    field(m, 244, 64, 8);
    field(m, 256, 8, 8);
    field(m, 268, 64 + logical_bytes / 512, 8);
    m.replace(280, 7, "default");
    field(m, 320, logical_bytes + 8192, 8);
    field(m, 328, 64, 8);
    field(m, 344, super_bytes, 8);
    m.replace(352, 5, "super");
    seal_header(m);

    for (unsigned i = 0; i < 4; i++) {
        s.replace(65536 + 12288 + i * 4096, 4096, m);
    }

    if (!payload.empty()) {
        s.replace(98304, payload.size(), payload);
    }

    return s;
}
} // namespace voyemu::lp_fixture
