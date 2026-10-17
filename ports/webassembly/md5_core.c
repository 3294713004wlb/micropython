/* Small portable incremental MD5 implementation for MicroPython WASM.
 *
 * The context keeps only a 64-byte partial block; update() never accumulates
 * input. This removes the memory amplification caused by the Python fallback.
 * MD5 is used here only for the ESP ROM/stub's data-integrity check, not for
 * password hashing or cryptographic authentication.
 */
#include <string.h>

#include "md5_core.h"

static const uint32_t k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
  0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const uint8_t shifts[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static uint32_t rol(uint32_t value, unsigned amount) {
    return (value << amount) | (value >> (32 - amount));
}

static void md5_transform(wasm_md5_context_t *ctx, const uint8_t data[64]) {
    uint32_t words[16];
    for (size_t i = 0; i < 16; ++i) {
        size_t pos = i * 4;
        words[i] = (uint32_t)data[pos]
            | ((uint32_t)data[pos + 1] << 8)
            | ((uint32_t)data[pos + 2] << 16)
            | ((uint32_t)data[pos + 3] << 24);
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];

    for (unsigned i = 0; i < 64; ++i) {
        uint32_t f;
        unsigned g;
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
        uint32_t rotated = rol(a + f + k[i] + words[g], shifts[i]);
        uint32_t next_d = d;
        d = c;
        c = b;
        b = b + rotated;
        a = next_d;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

void wasm_md5_init(wasm_md5_context_t *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->bit_count = 0;
    ctx->buffer_len = 0;
}

void wasm_md5_update(wasm_md5_context_t *ctx, const uint8_t *data, size_t length) {
    ctx->bit_count += (uint64_t)length * 8;
    while (length) {
        size_t take = 64 - ctx->buffer_len;
        if (take > length) {
            take = length;
        }
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        length -= take;
        if (ctx->buffer_len == 64) {
            md5_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

void wasm_md5_digest(const wasm_md5_context_t *ctx, uint8_t output[16]) {
    wasm_md5_context_t copy = *ctx;
    uint64_t bits = copy.bit_count;
    static const uint8_t marker = 0x80;
    static const uint8_t padding[64] = { 0 };

    wasm_md5_update(&copy, &marker, 1);
    size_t zeros = (copy.buffer_len <= 56)
        ? (56 - copy.buffer_len)
        : (120 - copy.buffer_len);
    wasm_md5_update(&copy, padding, zeros);
    uint8_t length_bytes[8];
    for (unsigned i = 0; i < 8; ++i) {
        length_bytes[i] = (uint8_t)(bits >> (i * 8));
    }
    wasm_md5_update(&copy, length_bytes, 8);
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t value = copy.state[i];
        for (unsigned j = 0; j < 4; ++j) {
            output[4 * i + j] = (uint8_t)(value >> (j * 8));
        }
    }
}
