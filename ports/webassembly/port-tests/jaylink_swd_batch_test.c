/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../usermods/jlink_native/swd_engine.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned calls, nwords, ack_failure, parity_failure, max_probe_words;
static bool write_mode;
static uint32_t actual_writes[256];
static unsigned bget(const uint8_t *data, unsigned bit) { return (data[bit / 8] >> (bit % 8)) & 1u; }
static void bset(uint8_t *data, unsigned bit, unsigned val) {
    if (val) data[bit / 8] |= (uint8_t)(1u << (bit % 8));
}
static uint32_t get_u32(const uint8_t *data, unsigned offset) {
    uint32_t word = 0;
    for (unsigned i = 0; i < 32; i++) word |= (uint32_t)bget(data, offset + i) << i;
    return word;
}
static unsigned parity(uint32_t v) {
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return v & 1;
}
static int simulated_jlink(void *ctx, const uint8_t *direction,
        const uint8_t *output, uint8_t *input, uint16_t bits) {
    (void)ctx;
    calls++;
    CHECK(bits % 54u == 0);
    CHECK(bits <= (JLINK_SWD_BATCH_WORDS + 1u) * 54u);
    unsigned frames = bits / 54u;
    if (max_probe_words && frames > max_probe_words) {
        return JLINK_SWD_EXCHANGE_NO_MEMORY;
    }
    for (unsigned f = 0; f < frames; f++) {
        unsigned pos = f * 54u;
        for (unsigned i = 0; i < 8; i++) CHECK(bget(direction, pos + i));
        for (unsigned i = 46; i < 54; i++) CHECK(bget(direction, pos + i));
        unsigned is_last = !write_mode && f == frames - 1;
        unsigned request = is_last ? 0xbd : (write_mode ? 0xbb : 0x9f);
        CHECK((get_u32(output, pos) & 0xff) == request);
        if (write_mode) {
            CHECK(bget(direction, pos + 13));
            unsigned v = get_u32(output, pos + 13);
            CHECK(bget(output, pos + 45) == parity(v));
            actual_writes[nwords++] = v;
        }
        unsigned ack = ack_failure && calls == 1 && f == 2 ? 4 : 1;
        for (unsigned i = 0; i < 3; i++) bset(input, pos + 8 + i, (ack >> i) & 1);
        if (!write_mode) {
            /* Posted AP read: first response is a stale value. Subsequent
             * AP read and final RDBUFF contain the expected ordered values. */
            uint32_t value = nwords ? 0xB000 + nwords - 1 : 0xDEADBEEF;
            for (unsigned i = 0; i < 32; i++) bset(input, pos + 11 + i, (value >> i) & 1);
            bset(input, pos + 43, parity(value) ^ (parity_failure && f == 1));
            if (!is_last) nwords++;
        }
    }
    return 0;
}
int main(void) {
    struct jlink_swd_batch_result r;
    uint32_t words[256], written[256];
    calls = nwords = ack_failure = parity_failure = 0; write_mode = false;
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, true, 12, NULL, words, 256, &r) == JLINK_SWD_OK);
    CHECK(calls == 11 && nwords == 256);
    for (unsigned i = 0; i < 256; i++) CHECK(words[i] == 0xB000u + i);
    calls = nwords = 0; write_mode = true;
    for (unsigned i = 0; i < 256; i++) written[i] = 0xC000 + i;
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, false, 12, written, NULL, 256, &r) == JLINK_SWD_OK);
    CHECK(calls == 11 && nwords == 256);
    for (unsigned i = 0; i < 256; i++) CHECK(actual_writes[i] == written[i]);
    calls = nwords = 0; write_mode = false; ack_failure = 1;
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, true, 12, NULL, words, 256, &r) == JLINK_SWD_FAULT);
    CHECK(r.ack == 4 && r.failed_index == 2 && calls == 1);
    calls = nwords = 0; ack_failure = 0; parity_failure = 1;
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, true, 12, NULL, words, 256, &r) == JLINK_SWD_BAD_PARITY);
    CHECK(calls == 1);
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, true, 3, NULL, words, 256, &r) == JLINK_SWD_INVALID_ARGUMENT);

    /* A smaller J-Link buffer rejects the first command and causes the
     * within-operation batch limit to shrink to 12 AP words. */
    calls = nwords = 0; max_probe_words = 13; parity_failure = 0;
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, true, 12, NULL, words, 256, &r) == JLINK_SWD_OK);
    CHECK(calls == 23 && nwords == 256);
    for (unsigned i = 0; i < 256; i++) CHECK(words[i] == 0xB000u + i);
    calls = nwords = 0; write_mode = true;
    CHECK(jlink_swd_ap_batch(simulated_jlink, NULL, false, 12, written, NULL, 256, &r) == JLINK_SWD_OK);
    CHECK(calls == 23 && nwords == 256);
    for (unsigned i = 0; i < 256; i++) CHECK(actual_writes[i] == written[i]);
    puts("SWD_AP_BATCH_TEST_PASSED 256 words in 11 USB commands; read/write fallback verified");
    return 0;
}
