/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017, 2018 Rami Ali
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

mergeInto(LibraryManager.library, {
    // This string will be emitted directly into the output file by Emscripten.
    mp_js_ticks_ms__postset: "var MP_JS_EPOCH = Date.now()",

    mp_js_ticks_ms: () => Date.now() - MP_JS_EPOCH,

    // Legacy v1.23 browser/Worker interrupt request flag.  For direct JS
    // callers, the public interrupt() method still schedules KeyboardInterrupt.
    mp_js_hook__postset: "var MP_INTERRUPT = false;",

    mp_js_hook: () => {
        if (MP_INTERRUPT) {
            MP_INTERRUPT = false;
            Module.ccall("mp_sched_keyboard_interrupt", "null", [], []);
        }
    },

    mp_js_time_ms: () => Date.now(),

    // crypto.getRandomValues is available in secure browser contexts, including Workers.
    mp_js_random_u32: () =>
        globalThis.crypto.getRandomValues(new Uint32Array(1))[0],
});
