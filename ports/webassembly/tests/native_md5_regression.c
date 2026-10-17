/* Regression vectors for the standalone incremental MD5 implementation. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../md5_core.h"

static void check(const char *input, const char *expected) {
    wasm_md5_context_t ctx;
    uint8_t bytes[16];
    char hex[33];
    static const char alphabet[] = "0123456789abcdef";
    wasm_md5_init(&ctx);
    for (size_t i = 0; input[i]; ++i) {
        wasm_md5_update(&ctx, (const uint8_t *)&input[i], 1);
    }
    wasm_md5_digest(&ctx, bytes);
    for (size_t i = 0; i < 16; ++i) {
        hex[i * 2] = alphabet[bytes[i] >> 4];
        hex[i * 2 + 1] = alphabet[bytes[i] & 15];
    }
    hex[32] = '\0';
    assert(strcmp(hex, expected) == 0);
    uint8_t second[16];
    wasm_md5_digest(&ctx, second);
    assert(memcmp(bytes, second, 16) == 0);
}

int main(void) {
    check("", "d41d8cd98f00b204e9800998ecf8427e");
    check("a", "0cc175b9c0f1b6a831c399e269772661");
    check("abc", "900150983cd24fb0d6963f7d28e17f72");
    check("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
    check("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
    check("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f");
    check("12345678901234567890123456789012345678901234567890123456789012345678901234567890",
        "57edf4a22be3c955ac49da2e2107b67a");

    wasm_md5_context_t ctx;
    wasm_md5_init(&ctx);
    static uint8_t data[4096];
    memset(data, 'x', sizeof(data));
    for (unsigned i = 0; i < 1024; ++i) {
        wasm_md5_update(&ctx, data, sizeof(data));
    }
    uint8_t digest[16];
    wasm_md5_digest(&ctx, digest);
    static const uint8_t expected[16] = {
        0x44, 0x98, 0x13, 0x62, 0xd3, 0xba, 0x9b, 0x5b,
        0xac, 0xaf, 0x01, 0x7c, 0x2f, 0x29, 0xd3, 0x55,
    };
    assert(memcmp(digest, expected, sizeof(expected)) == 0);
    puts("PASS: RFC MD5 vectors and 4-MiB incremental MD5");
    return 0;
}
