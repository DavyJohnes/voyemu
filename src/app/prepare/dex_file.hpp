#pragma once
#include "check.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

// DEX file primitives shared by the DEX patchers: SHA-1 signature, Adler-32 checksum and a bounded
// little-endian reader whose failures carry the code of its patcher.
namespace voyemu::dex_file {
inline std::array<unsigned char, 20> sha1(std::string_view input) {
    std::string data(input);
    const auto bits = static_cast<std::uint64_t>(data.size()) * 8;

    data.push_back(static_cast<char>(128));

    while (data.size() % 64 != 56) {
        data.push_back(0);
    }

    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<char>(bits >> (i * 8)));
    }

    std::array<std::uint32_t, 5> h = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

    for (std::size_t off = 0; off < data.size(); off += 64) {
        std::array<std::uint32_t, 80> w{};

        for (unsigned i = 0; i < 16; ++i) {
            for (unsigned j = 0; j < 4; ++j) {
                w[i] = (w[i] << 8) |
                       static_cast<unsigned char>(data[off + static_cast<std::size_t>(i) * 4 + j]);
            }
        }

        for (unsigned i = 16; i < 80; ++i) {
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (unsigned i = 0; i < 80; ++i) {
            const auto f = i < 20   ? ((b & c) | (~b & d))
                           : i < 40 ? (b ^ c ^ d)
                           : i < 60 ? ((b & c) | (b & d) | (c & d))
                                    : (b ^ c ^ d);
            const std::uint32_t k = i < 20   ? 0x5a827999
                                    : i < 40 ? 0x6ed9eba1
                                    : i < 60 ? 0x8f1bbcdc
                                             : 0xca62c1d6;
            const auto next = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = next;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::array<unsigned char, 20> result{};

    for (unsigned i = 0; i < 20; ++i) {
        result[i] = static_cast<unsigned char>(h[i / 4] >> (24 - (i % 4) * 8));
    }

    return result;
}

inline std::uint32_t adler(std::string_view input) {
    std::uint32_t a = 1, b = 0;

    for (unsigned char c : input) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }

    return (b << 16) | a;
}

struct Reader {
    const std::string &b;
    Check need;

    void extent(std::size_t at, std::size_t count) const {
        need(at <= b.size() && count <= b.size() - at, "out of bounds");
    }

    unsigned byte(std::size_t at) const {
        extent(at, 1);

        return static_cast<unsigned char>(b[at]);
    }

    std::uint16_t half(std::size_t at) const {
        return static_cast<std::uint16_t>(byte(at) | (byte(at + 1) << 8));
    }

    std::uint32_t word(std::size_t at) const {
        return half(at) | (static_cast<std::uint32_t>(half(at + 2)) << 16);
    }

    std::uint32_t leb(std::size_t &at) const {
        std::uint32_t v = 0;

        for (unsigned i = 0; i < 5; ++i) {
            auto c = byte(at++);
            need(i < 4 || c <= 15, "ULEB overflow");
            v |= (c & 127) << (i * 7);

            if (!(c & 128)) {
                return v;
            }
        }

        need(false, "ULEB extent");

        return 0;
    }

    std::string text(std::uint32_t index) const {
        need(index < word(56), "string index");
        std::size_t at = word(word(60) + index * 4);
        auto units = leb(at);
        need(units <= 4096, "identity string extent");
        std::string result;

        while (byte(at)) {
            auto c = byte(at++);
            need(c >= 32 && c < 127 && result.size() < 4096, "non-ASCII identity");
            result += static_cast<char>(c);
        }

        need(result.size() == units, "identity string length");

        return result;
    }

    std::string type(std::uint32_t index) const {
        need(index < word(64), "type index");

        return text(word(word(68) + index * 4));
    }

    std::string proto(std::uint32_t index) const {
        need(index < word(72), "prototype index");
        auto at = word(76) + index * 12;
        auto params = word(at + 8);
        std::string result = "(";

        if (params) {
            need(params % 4 == 0, "type list alignment");
            auto count = word(params);
            need(count <= 65535, "type list count");
            extent(params + 4, static_cast<std::size_t>(count) * 2);

            for (std::uint32_t i = 0; i < count; ++i) {
                result += type(half(params + 4 + i * 2));
            }
        }

        result += ")";
        result += type(word(at + 4));

        return result;
    }
};

inline void put32(std::string &b, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        b[at + i] = static_cast<char>(value >> (8 * i));
    }
}
} // namespace voyemu::dex_file
