/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George and 2017, 2018 Rami Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <emscripten.h>
#include <unistd.h>
#include "py/mphal.h"
#include "library.h"

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    write(2, str, len);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    return write(1, str, len);
}

extern bool mp_wasm_asyncify_can_yield(void);

// Matches the original customized v1.23 event-check entry point.
void mp_hal_process_events(void) {
    mp_js_hook();
}

void mp_wasm_vm_yield(void) {
    if (mp_wasm_asyncify_can_yield()) {
        emscripten_sleep(0);
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    // Original project used Asyncify for larger sleeps so serial/USB JS
    // promises and browser messages continue to be processed.
    if (ms > 10 && mp_wasm_asyncify_can_yield()) {
        emscripten_sleep((unsigned int)ms);
    } else {
        // A sync entry point cannot unwind; keep small delays synchronous.
        uint32_t start = mp_hal_ticks_ms();
        while (mp_hal_ticks_ms() - start < ms) {
        }
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    uint32_t start = mp_hal_ticks_us();
    while (mp_hal_ticks_us() - start < us) {
    }
}

mp_uint_t mp_hal_ticks_us(void) {
    return mp_js_ticks_ms() * 1000;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return mp_js_ticks_ms();
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return 0;
}

uint64_t mp_hal_time_ms(void) {
    double mm = mp_js_time_ms();
    return (uint64_t)mm;
}

uint64_t mp_hal_time_ns(void) {
    return mp_hal_time_ms() * 1000000ULL;
}

extern int mp_interrupt_char;

int mp_hal_get_interrupt_char(void) {
    return mp_interrupt_char;
}
