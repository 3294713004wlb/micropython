/* SPDX-License-Identifier: MIT
 * SWD framing used by the MicroPython WASM J-Link adapter.
 * See OpenOCD src/jtag/drivers/jlink.c for an independently implemented
 * SWD queue engine. This module is a compact synchronous adapter.
 */
#include "swd_engine.h"
#include <string.h>

static void put_bit(uint8_t *dst, unsigned pos, unsigned val)
{
    if (val & 1u)
        dst[pos >> 3] |= (uint8_t)(1u << (pos & 7u));
}

static unsigned get_bit(const uint8_t *src, unsigned pos)
{
    return (src[pos >> 3] >> (pos & 7u)) & 1u;
}

static unsigned parity32(uint32_t value)
{
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return value & 1u;
}

static uint32_t extract_u32(const uint8_t *src, unsigned offset)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 32; ++i)
        value |= (uint32_t)get_bit(src, offset + i) << i;
    return value;
}

int jlink_swd_transfer(jlink_swd_exchange_fn exchange, void *ctx,
        bool ap, bool read, uint8_t address, uint32_t write_data,
        uint8_t retries, uint16_t ap_delay,
        struct jlink_swd_result *result)
{
    if (!exchange || !result || (address & 3u) || address > 12u ||
            retries < 1u || retries > JLINK_SWD_MAX_RETRIES ||
            ap_delay > JLINK_SWD_MAX_AP_DELAY_CLOCKS)
        return JLINK_SWD_INVALID_ARGUMENT;

    uint16_t delay = ap ? ap_delay : 0u;
    /* 8 request + 1 turn + 3 ACK + 34 data/turn + 8 idle */
    const uint16_t total_bits = (uint16_t)(54u + delay);
    const unsigned nbytes = (total_bits + 7u) / 8u;
    uint8_t direction[JLINK_SWD_MAX_FRAME_BYTES];
    uint8_t output[JLINK_SWD_MAX_FRAME_BYTES];
    memset(direction, 0, sizeof(direction));
    memset(output, 0, sizeof(output));
    memset(result, 0, sizeof(*result));

    const uint8_t a = (address >> 2) & 3u;
    const uint8_t request = (uint8_t)(0x81u | (ap ? 2u : 0u) |
            (read ? 4u : 0u) | (a << 3) |
            (((ap ? 1u : 0u) ^ (read ? 1u : 0u) ^
              (a & 1u) ^ ((a >> 1) & 1u)) << 5));
    result->request = request;
    result->bits = total_bits;

    /* Host drives the eight request bits, then target owns turnaround/ACK. */
    for (unsigned i = 0; i < 8u; ++i) {
        put_bit(direction, i, 1u);
        put_bit(output, i, (request >> i) & 1u);
    }
    if (!read) {
        /* After ACK: target releases line for one turnaround (bit 12),
         * host drives data (bits 13..44) and parity (bit 45). */
        for (unsigned i = 0; i < 32u; ++i) {
            put_bit(direction, 13u + i, 1u);
            put_bit(output, 13u + i, (write_data >> i) & 1u);
        }
        put_bit(direction, 45u, 1u);
        put_bit(output, 45u, parity32(write_data));
    }
    /* For reads, bit 45 is the target->host turnaround and remains input.
     * For both read/write, drive AP delay and final idle cycles low. */
    for (unsigned i = 46u; i < total_bits; ++i)
        put_bit(direction, i, 1u);

    memcpy(result->direction, direction, nbytes);
    memcpy(result->output, output, nbytes);
    for (unsigned attempt = 1; attempt <= retries; ++attempt) {
        /* The return buffer is retained even for invalid ACK diagnostics. */
        memset(result->input, 0, nbytes);
        result->attempts = (uint8_t)attempt;
        const int rv = exchange(ctx, direction, output, result->input, total_bits);
        if (rv != 0) {
            result->transport_status = rv;
            result->status = JLINK_SWD_TRANSPORT;
            return result->status;
        }

        /* libjaylink's sampled input is aligned as OpenOCD jlink.c uses it:
         * ACK begins at the first returned bit following the 8-bit request,
         * and read data starts 3 bits later. The physical turnaround is
         * still clocked and must remain in the direction/output frame; it
         * is NOT an additional bit in the response indexing convention.
         *
         * OpenOCD jlink_swd_run_queue(): ack at pending.first (8),
         * data at pending.first + 3 (11), parity at first + 35 (43).
         */
        result->ack = (uint8_t)(get_bit(result->input, 8u) |
                (get_bit(result->input, 9u) << 1) |
                (get_bit(result->input, 10u) << 2));
        if (result->ack == 1u) {
            if (read) {
                result->data = extract_u32(result->input, 11u);
                if (get_bit(result->input, 43u) != parity32(result->data)) {
                    result->status = JLINK_SWD_BAD_PARITY;
                    return result->status;
                }
            }
            result->status = JLINK_SWD_OK;
            return result->status;
        }
        /* ADIv5 DPv2 TARGETSEL write is broadcast and has no ACK. */
        if (!ap && !read && address == 12u) {
            result->status = JLINK_SWD_OK;
            return result->status;
        }
        if (result->ack == 2u) {
            result->status = JLINK_SWD_WAIT;
            continue;
        }
        result->status = (result->ack == 4u) ? JLINK_SWD_FAULT : JLINK_SWD_BAD_ACK;
        return result->status;
    }
    return result->status;
}


/* Batch AP.DRW and other repeated AP register transfers to amortize
 * browser WebUSB latency. Each frame is 54 clocks including 8 idle clocks.
 * The maximum frame batch is 25 frames/1350 clocks/169 bytes.
 * Crucially, a batch never crosses an AP SELECT bank change or MEM-AP TAR
 * auto-increment boundary; pyOCD already splits TAR pages at the caller.
 */
int jlink_swd_ap_batch(jlink_swd_exchange_fn exchange, void *ctx,
        bool read, uint8_t address, const uint32_t *written,
        uint32_t *received, uint32_t count,
        struct jlink_swd_batch_result *result)
{
    if (!exchange || !result || address > 12u || (address & 3u) ||
            (read && count && !received) || (!read && count && !written))
        return JLINK_SWD_INVALID_ARGUMENT;

    memset(result, 0, sizeof(*result));
    result->status = JLINK_SWD_OK;
    uint32_t processed = 0;
    uint32_t max_words = JLINK_SWD_BATCH_WORDS;
    while (processed < count) {
        uint32_t nwords = count - processed;
        if (nwords > max_words)
            nwords = max_words;
        const unsigned nframes = (unsigned)nwords + (read ? 1u : 0u);
        const uint16_t bit_count = (uint16_t)(54u * nframes);
        const unsigned nbytes = (bit_count + 7u) / 8u;
        uint8_t dir[(54u * (JLINK_SWD_BATCH_WORDS + 1u) + 7u) / 8u];
        uint8_t out[sizeof(dir)];
        uint8_t in[sizeof(dir)];
        memset(dir, 0, nbytes);
        memset(out, 0, nbytes);
        memset(in, 0, nbytes);

        for (unsigned f = 0; f < nframes; ++f) {
            const unsigned base = f * 54u;
            const bool is_last_rdbuff = read && f == nframes - 1u;
            const bool frame_ap = !is_last_rdbuff;
            const uint8_t frame_addr = is_last_rdbuff ? 12u : address;
            const bool frame_read = read || is_last_rdbuff;
            const uint8_t a = (frame_addr >> 2) & 3u;
            const uint8_t req = (uint8_t)(0x81u | (frame_ap ? 2u : 0u) |
                    (frame_read ? 4u : 0u) | (a << 3) |
                    (((frame_ap ? 1u : 0u) ^ (frame_read ? 1u : 0u) ^
                      (a & 1u) ^ ((a >> 1) & 1u)) << 5));
            for (unsigned i = 0; i < 8u; ++i) {
                put_bit(dir, base + i, 1u);
                put_bit(out, base + i, (req >> i) & 1u);
            }
            if (!frame_read) {
                const uint32_t value = written[processed + f];
                for (unsigned i = 0; i < 32u; ++i) {
                    put_bit(dir, base + 13u + i, 1u);
                    put_bit(out, base + 13u + i, (value >> i) & 1u);
                }
                put_bit(dir, base + 45u, 1u);
                put_bit(out, base + 45u, parity32(value));
            }
            for (unsigned i = 46u; i < 54u; ++i)
                put_bit(dir, base + i, 1u);
        }
        const int transport = exchange(ctx, dir, out, in, bit_count);
        if (transport == JLINK_SWD_EXCHANGE_NO_MEMORY && nwords > 1u) {
            /* Device rejected the entire SWD command before execution.
             * Shrink this task's batch and retry with no cross-task cache.
             * Do not retry other errors: partial flash writes are possible.
             */
            max_words = (nwords + 1u) / 2u;
            continue;
        }
        if (transport != 0) {
            result->status = JLINK_SWD_TRANSPORT;
            result->transport_status = transport;
            result->failed_index = processed;
            return result->status;
        }
        /* All responses must be validated before exposing the returned data.
         * No implicit retry of a partially executed transaction sequence. */
        for (unsigned f = 0; f < nframes; ++f) {
            const unsigned base = 54u * f;
            const uint8_t ack = (uint8_t)(get_bit(in, base + 8u) |
                    (get_bit(in, base + 9u) << 1) |
                    (get_bit(in, base + 10u) << 2));
            if (ack != 1u) {
                result->ack = ack;
                result->failed_index = processed + (f < nwords ? f : nwords - 1u);
                result->status = ack == 2u ? JLINK_SWD_WAIT :
                    (ack == 4u ? JLINK_SWD_FAULT : JLINK_SWD_BAD_ACK);
                return result->status;
            }
            if (read) {
                const uint32_t data = extract_u32(in, base + 11u);
                if (get_bit(in, base + 43u) != parity32(data)) {
                    result->status = JLINK_SWD_BAD_PARITY;
                    result->failed_index = processed + (f ? f - 1u : 0u);
                    return result->status;
                }
                if (f > 0)
                    received[processed + f - 1u] = data;
            }
        }
        processed += nwords;
    }
    return result->status;
}
