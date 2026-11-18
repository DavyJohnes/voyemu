/* Streaming SHA-256 (FIPS 180-4) shared by the e2fsprogs image helpers on every host. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} Sha256;
static const uint32_t sha256_round[64] = {
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

static uint32_t sha256_rotate(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void sha256_compress(Sha256 *ctx, const unsigned char *block) {
    uint32_t w[64], v[8];

    for (size_t i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 |
               (uint32_t)block[4 * i + 2] << 8 | block[4 * i + 3];
    }

    for (unsigned i = 16; i < 64; i++) {
        const uint32_t s0 = sha256_rotate(w[i - 15], 7) ^ sha256_rotate(w[i - 15], 18) ^
                            (w[i - 15] >> 3),
                       s1 = sha256_rotate(w[i - 2], 17) ^ sha256_rotate(w[i - 2], 19) ^
                            (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    memcpy(v, ctx->state, sizeof v);

    for (unsigned i = 0; i < 64; i++) {
        const uint32_t s1 = sha256_rotate(v[4], 6) ^ sha256_rotate(v[4], 11) ^
                            sha256_rotate(v[4], 25),
                       t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha256_round[i] + w[i],
                       s0 = sha256_rotate(v[0], 2) ^ sha256_rotate(v[0], 13) ^
                            sha256_rotate(v[0], 22),
                       t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }

    for (unsigned i = 0; i < 8; i++) {
        ctx->state[i] += v[i];
    }
}

static void sha256_init(Sha256 *ctx) {
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    memcpy(ctx->state, initial, sizeof initial);
    ctx->bytes = 0;
    ctx->used = 0;
}

static void sha256_update(Sha256 *ctx, const void *data, size_t len) {
    const unsigned char *p = data;

    ctx->bytes += len;

    while (len) {
        size_t n = sizeof ctx->block - ctx->used < len ? sizeof ctx->block - ctx->used : len;
        memcpy(ctx->block + ctx->used, p, n);
        ctx->used += n;
        p += n;
        len -= n;

        if (ctx->used == sizeof ctx->block) {
            sha256_compress(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sha256_final(Sha256 *ctx, unsigned char out[32]) {
    const uint64_t bits = ctx->bytes * 8;
    unsigned char tail[8];

    sha256_update(ctx, "\x80", 1);

    while (ctx->used != 56) {
        sha256_update(ctx, "", 1);
    }

    for (unsigned i = 0; i < 8; i++) {
        tail[i] = (unsigned char)(bits >> (56 - 8 * i));
    }

    sha256_update(ctx, tail, sizeof tail);

    for (size_t i = 0; i < 8; i++) {
        out[4 * i] = (unsigned char)(ctx->state[i] >> 24);
        out[4 * i + 1] = (unsigned char)(ctx->state[i] >> 16);
        out[4 * i + 2] = (unsigned char)(ctx->state[i] >> 8);
        out[4 * i + 3] = (unsigned char)ctx->state[i];
    }
}
