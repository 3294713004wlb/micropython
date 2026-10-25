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
#include <stdio.h>
#include "py/mphal.h"
#include "py/runtime.h"
#include "library.h"

extern size_t wasm_transport_terminal_write(int stream, const char *data, size_t length);

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    wasm_transport_terminal_write(2, str, len);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

/* One real stdin implementation shared by input(), readline and stream I/O.
 * Emscripten fd 0 delegates to Module.stdin in the isolated Worker.
 * Non-interactive jobs receive EOF; the application decides what to do.
 */
int mp_hal_stdin_rx_chr(void) {
    unsigned char ch;
    if (read(STDIN_FILENO, &ch, 1) == 1) {
        return ch;
    }
    return 4; // Ctrl-D: MicroPython readline's end-of-input marker.
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    return wasm_transport_terminal_write(1, str, len);
}

extern bool mp_wasm_asyncify_can_yield(void);

// VM-level cancellation must be read directly from shared WASM memory.
// A synchronously running Python Worker cannot process a postMessage event.
extern bool wasm_transport_cancelled(void);

void mp_hal_process_events(void) {
    if (wasm_transport_cancelled()) {
        mp_raise_type(&mp_type_KeyboardInterrupt);
    }
#if !MICROPY_WASM_CLI_BUSY_DELAY
    // Interactive/async profile also honors the legacy JS interrupt path
    mp_js_hook();
#endif
}

void mp_wasm_vm_yield(void) {
#if !MICROPY_WASM_CLI_BUSY_DELAY
    if (mp_wasm_asyncify_can_yield()) {
        emscripten_sleep(0);
    }
#endif
}

void mp_hal_delay_ms(mp_uint_t ms) {
#if MICROPY_WASM_CLI_BUSY_DELAY
    // The CLI Worker must not rely on its JS event loop during synchronous
    // Python execution. Device callbacks are served by a different context.
    // This also keeps short pyOCD delays out of Asyncify unwind/rewind.
    uint32_t start = mp_hal_ticks_ms();
    while ((uint32_t)(mp_hal_ticks_ms() - start) < ms) {
        if (wasm_transport_cancelled()) {
            mp_raise_type(&mp_type_KeyboardInterrupt);
        }
    }
#else
    if (ms > 10 && mp_wasm_asyncify_can_yield()) {
        emscripten_sleep((unsigned int)ms);
    } else {
        uint32_t start = mp_hal_ticks_ms();
        while ((uint32_t)(mp_hal_ticks_ms() - start) < ms) {
        }
    }
#endif
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
