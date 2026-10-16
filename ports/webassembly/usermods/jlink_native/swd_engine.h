/* SPDX-License-Identifier: MIT
 * Minimal ADIv5 SWD transaction framing for the browser libjaylink adapter.
 * Frame layout follows the ARM SWD protocol and the OpenOCD J-Link driver's
 * transaction convention. The returned raw sampling buffer has its ACK
 * at bit 8 and read data at bit 11 (as in OpenOCD jlink.c), even though
 * the transmitted SWD frame includes a physical turnaround clock.
 * No OpenOCD source code is copied into this file.
 */
#ifndef JLINK_WASM_SWD_ENGINE_H
#define JLINK_WASM_SWD_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

typedef int (*jlink_swd_exchange_fn)(void *ctx, const uint8_t *direction,
        const uint8_t *output, uint8_t *input, uint16_t bits);

enum jlink_swd_status {
    JLINK_SWD_OK = 0,
    JLINK_SWD_WAIT = -1,
    JLINK_SWD_FAULT = -2,
    JLINK_SWD_BAD_ACK = -3,
    JLINK_SWD_BAD_PARITY = -4,
    JLINK_SWD_TRANSPORT = -5,
    JLINK_SWD_INVALID_ARGUMENT = -6,
};

#define JLINK_SWD_IDLE_CLOCKS 8u
#define JLINK_SWD_MAX_AP_DELAY_CLOCKS 128u
#define JLINK_SWD_MAX_RETRIES 20u
#define JLINK_SWD_MAX_FRAME_BITS (54u + JLINK_SWD_MAX_AP_DELAY_CLOCKS)
#define JLINK_SWD_MAX_FRAME_BYTES ((JLINK_SWD_MAX_FRAME_BITS + 7u) / 8u)

struct jlink_swd_result {
    int status;
    int transport_status;
    uint8_t request;
    uint8_t ack;
    uint8_t attempts;
    uint16_t bits;
    uint32_t data;
    uint8_t input[JLINK_SWD_MAX_FRAME_BYTES];
    uint8_t direction[JLINK_SWD_MAX_FRAME_BYTES];
    uint8_t output[JLINK_SWD_MAX_FRAME_BYTES];
};

/* Register addresses are 0x0,0x4,0x8,0xc, not 0..3.
 * ap_delay applies only to AP accesses, is added after the 46-bit frame,
 * and is followed by 8 outgoing idle cycles. The read response and its
 * parity are valid only for an OK ACK.
 * RETRIES: maximum number of transfer attempts including the first.
 */
int jlink_swd_transfer(jlink_swd_exchange_fn exchange, void *ctx,
        bool ap, bool read, uint8_t address, uint32_t write_data,
        uint8_t retries, uint16_t ap_delay,
        struct jlink_swd_result *result);

/* A bounded queue of complete SWD frames. Each batch is sent in ONE
 * jaylink_swd_io() command. AP reads are posted: N AP reads and one DP
 * RDBUFF return N ordered words. The caller owns APSEL/APBANKSEL in SELECT.
 * Failures never retry a partially executed queue, especially AP.DRW writes.
 * This keeps the original single-transfer path available as a fallback.
 */
/* Up to 24 AP words fit within a 1,350-bit J-Link SWD command.
 * The engine reduces this per-call on a probe out-of-memory response.
 */
#define JLINK_SWD_BATCH_WORDS 24u
#define JLINK_SWD_EXCHANGE_NO_MEMORY (-1003)
struct jlink_swd_batch_result {
    int status;
    int transport_status;
    uint32_t failed_index;
    uint8_t ack;
};
int jlink_swd_ap_batch(jlink_swd_exchange_fn exchange, void *ctx,
        bool read, uint8_t address, const uint32_t *written,
        uint32_t *received, uint32_t count,
        struct jlink_swd_batch_result *result);

#endif
