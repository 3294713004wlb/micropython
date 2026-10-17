#ifndef MICROPY_WASM_MD5_CORE_H
#define MICROPY_WASM_MD5_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[4];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_len;
} wasm_md5_context_t;

void wasm_md5_init(wasm_md5_context_t *ctx);
void wasm_md5_update(wasm_md5_context_t *ctx, const uint8_t *data, size_t length);
void wasm_md5_digest(const wasm_md5_context_t *ctx, uint8_t output[16]);

#endif
