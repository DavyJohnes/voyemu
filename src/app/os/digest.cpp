#include "digest.hpp"
#include "error.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#if defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>
#elif defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
#include <arm_neon.h>
#endif

namespace voyemu::os {
namespace {
constexpr std::uint32_t rotl(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

constexpr std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

constexpr std::array<std::uint32_t, 8> sha256_initial{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                                      0xa54ff53a, 0x510e527f, 0x9b05688c,
                                                      0x1f83d9ab, 0x5be0cd19};
constexpr std::array<std::uint32_t, 64> sha256_k{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

void sha256_portable_block(std::array<std::uint32_t, 8> &state, const std::uint8_t *block) {
    std::uint32_t w[64];

    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = std::uint32_t(block[i * 4]) << 24 | std::uint32_t(block[i * 4 + 1]) << 16 |
               std::uint32_t(block[i * 4 + 2]) << 8 | std::uint32_t(block[i * 4 + 3]);
    }

    for (int i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    auto a = state[0], b = state[1], c = state[2], d = state[3];
    auto e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        const auto t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                        sha256_k[i] + w[i];
        const auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256_portable(std::array<std::uint32_t, 8> &state, const std::uint8_t *data,
                     std::size_t count) {
    for (; count; --count, data += 64) {
        sha256_portable_block(state, data);
    }
}

#if defined(__x86_64__)
// Message words of four rounds per vector; the state is kept as ABEF and CDGH for sha256rnds2.
__attribute__((target("sha,sse4.1"))) void sha256_x86(std::array<std::uint32_t, 8> &state,
                                                      const std::uint8_t *data, std::size_t count) {
    const auto byte_swap = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
    const auto dcba =
            _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(&state[0])), 0xb1);
    const auto hgfe =
            _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(&state[4])), 0x1b);
    auto abef = _mm_alignr_epi8(dcba, hgfe, 8);
    auto cdgh = _mm_blend_epi16(hgfe, dcba, 0xf0);

    for (; count; --count, data += 64) {
        const auto abef_start = abef;
        const auto cdgh_start = cdgh;
        __m128i w[4];

        // Full unrolling keeps the four message vectors in registers.
#pragma unroll

        for (std::size_t group = 0; group < 16; ++group) {
            auto &words = w[group % 4];

            if (group < 4) {
                words = _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + group * 16)),
                        byte_swap);
            } else {
                const auto &previous = w[(group + 3) % 4];
                words = _mm_sha256msg2_epu32(
                        _mm_add_epi32(_mm_sha256msg1_epu32(words, w[(group + 1) % 4]),
                                      _mm_alignr_epi8(previous, w[(group + 2) % 4], 4)),
                        previous);
            }

            const auto message = _mm_add_epi32(
                    words,
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(&sha256_k[group * 4])));
            cdgh = _mm_sha256rnds2_epu32(cdgh, abef, message);
            abef = _mm_sha256rnds2_epu32(abef, cdgh, _mm_shuffle_epi32(message, 0x0e));
        }

        abef = _mm_add_epi32(abef, abef_start);
        cdgh = _mm_add_epi32(cdgh, cdgh_start);
    }

    const auto feba = _mm_shuffle_epi32(abef, 0x1b);
    const auto dchg = _mm_shuffle_epi32(cdgh, 0xb1);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&state[0]), _mm_blend_epi16(feba, dchg, 0xf0));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&state[4]), _mm_alignr_epi8(dchg, feba, 8));
}

bool x86_sha_supported() {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;

    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & bit_SSE4_1) || !(ecx & bit_SSSE3)) {
        return false;
    }

    return __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) && (ebx & bit_SHA);
}
#elif defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
void sha256_arm(std::array<std::uint32_t, 8> &state, const std::uint8_t *data, std::size_t count) {
    auto abcd = vld1q_u32(&state[0]);
    auto efgh = vld1q_u32(&state[4]);

    for (; count; --count, data += 64) {
        const auto abcd_start = abcd;
        const auto efgh_start = efgh;
        uint32x4_t w[4];

        // Full unrolling keeps the four message vectors in registers.
#pragma unroll

        for (std::size_t group = 0; group < 16; ++group) {
            auto &words = w[group % 4];

            if (group < 4) {
                words = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + group * 16)));
            } else {
                words = vsha256su1q_u32(vsha256su0q_u32(words, w[(group + 1) % 4]),
                                        w[(group + 2) % 4], w[(group + 3) % 4]);
            }

            const auto message = vaddq_u32(words, vld1q_u32(&sha256_k[group * 4]));
            const auto abcd_round = abcd;
            abcd = vsha256hq_u32(abcd, efgh, message);
            efgh = vsha256h2q_u32(efgh, abcd_round, message);
        }

        abcd = vaddq_u32(abcd, abcd_start);
        efgh = vaddq_u32(efgh, efgh_start);
    }

    vst1q_u32(&state[0], abcd);
    vst1q_u32(&state[4], efgh);
}
#endif
constexpr std::array<std::uint32_t, 64> md5_k{
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
        0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
        0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
        0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
        0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
        0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391};
constexpr int md5_shift[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void md5_block(std::array<std::uint32_t, 4> &state, const std::uint8_t *block) {
    std::uint32_t m[16];

    for (std::size_t i = 0; i < 16; ++i) {
        m[i] = std::uint32_t(block[i * 4]) | std::uint32_t(block[i * 4 + 1]) << 8 |
               std::uint32_t(block[i * 4 + 2]) << 16 | std::uint32_t(block[i * 4 + 3]) << 24;
    }

    auto a = state[0], b = state[1], c = state[2], d = state[3];

    for (int i = 0; i < 64; ++i) {
        std::uint32_t f = 0;
        int g = 0;

        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }

        const auto next = d;
        d = c;
        c = b;
        b = b + rotl(a + f + md5_k[i] + m[g], md5_shift[i]);
        a = next;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_blocks(std::array<std::uint32_t, 4> &state, const std::uint8_t *data, std::size_t count) {
    for (; count; --count, data += 64) {
        md5_block(state, data);
    }
}

template <typename Block, typename State>
void absorb(State &state, std::array<std::uint8_t, 64> &buffer, std::uint64_t &length,
            const void *data, std::size_t size, Block block) {
    auto input = static_cast<const std::uint8_t *>(data);
    auto used = std::size_t(length % 64);

    length += size;

    if (used) {
        const auto take = std::min(size, 64 - used);
        std::memcpy(buffer.data() + used, input, take);
        input += take;
        size -= take;
        used += take;

        if (used < 64) {
            return;
        }

        block(state, buffer.data(), 1);
    }

    block(state, input, size / 64);
    input += size / 64 * 64;
    std::memcpy(buffer.data(), input, size % 64);
}

template <typename Block, typename State>
void pad(State &state, std::array<std::uint8_t, 64> &buffer, std::uint64_t length, bool big_endian,
         Block block) {
    auto used = std::size_t(length % 64);

    buffer[used++] = 0x80;

    if (used > 56) {
        std::memset(buffer.data() + used, 0, 64 - used);
        block(state, buffer.data(), 1);
        used = 0;
    }

    std::memset(buffer.data() + used, 0, 56 - used);
    const auto bits = length * 8;

    for (int i = 0; i < 8; ++i) {
        buffer[56 + i] = std::uint8_t(bits >> (big_endian ? 56 - 8 * i : 8 * i));
    }

    block(state, buffer.data(), 1);
}
} // namespace

std::vector<Sha256Engine> sha256_engines() {
    std::vector<Sha256Engine> engines{Sha256Engine::portable};

#if defined(__x86_64__)
    if (x86_sha_supported()) {
        engines.push_back(Sha256Engine::x86_sha);
    }
#elif defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
    engines.push_back(Sha256Engine::arm_sha2);
#endif

    return engines;
}

Sha256::Sha256()
    : Sha256([] {
          static const auto fastest = sha256_engines().back();

          return fastest;
      }()) {}

Sha256::Sha256(Sha256Engine engine) : blocks_(sha256_portable), state_(sha256_initial) {
    const auto engines = sha256_engines();

    if (std::find(engines.begin(), engines.end(), engine) == engines.end()) {
        throw VoyemuError("SHA256_ENGINE", "engine not supported by this CPU");
    }

#if defined(__x86_64__)
    if (engine == Sha256Engine::x86_sha) {
        blocks_ = sha256_x86;
    }
#elif defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
    if (engine == Sha256Engine::arm_sha2) {
        blocks_ = sha256_arm;
    }
#endif
}

void Sha256::update(const void *data, std::size_t size) {
    absorb(state_, block_, length_, data, size, blocks_);
}

std::array<std::uint8_t, 32> Sha256::finish() {
    pad(state_, block_, length_, true, blocks_);
    std::array<std::uint8_t, 32> digest{};

    for (int i = 0; i < 32; ++i) {
        digest[i] = std::uint8_t(state_[i / 4] >> (24 - 8 * (i % 4)));
    }

    state_ = sha256_initial;
    length_ = 0;

    return digest;
}

Md5::Md5() : state_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::update(const void *data, std::size_t size) {
    absorb(state_, block_, length_, data, size, md5_blocks);
}

std::array<std::uint8_t, 16> Md5::finish() {
    pad(state_, block_, length_, false, md5_blocks);
    std::array<std::uint8_t, 16> digest{};

    for (int i = 0; i < 16; ++i) {
        digest[i] = std::uint8_t(state_[i / 4] >> (8 * (i % 4)));
    }

    *this = Md5();

    return digest;
}

std::string hex_digest(const std::uint8_t *data, std::size_t size) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;

    result.reserve(size * 2);

    for (std::size_t i = 0; i < size; ++i) {
        result += hex[data[i] >> 4];
        result += hex[data[i] & 15];
    }

    return result;
}
} // namespace voyemu::os
