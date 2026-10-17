/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023-2024 Damien P. George
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

// Options:
// - pystack: size in words of the MicroPython Python stack.
// - heapsize: size (bytes) of the fixed MicroPython GC heap for the standard CLI variant.
//   The stack/pystack are not part of the GC heap.
// - url: location to load `micropython.mjs`.
// - stdin: optional callback for standard input (not a Node CLI).
// - stdout: function that takes one argument, and is passed lines of stdout
//   output as they are produced.  By default this is handled by Emscripten
//   and by default is forwarded to the browser/Worker console.
// - stderr: same behaviour as stdout but for error output.
// - linebuffer: whether to buffer line-by-line to stdout/stderr.
export async function loadMicroPython(options) {
    const { pystack, heapsize, url, stdin, stdout, stderr, linebuffer } =
        Object.assign(
            { pystack: 2 * 1024, heapsize: 16 * 1024 * 1024, linebuffer: true },
            options,
        );
    // mp_js_init takes 32-bit signed integers; reject accidental wraparound.
    if (!Number.isSafeInteger(heapsize) || heapsize < 64 * 1024 || heapsize > 1024 * 1024 * 1024) {
        throw new RangeError("heapsize must be an integer between 64 KiB and 1 GiB");
    }
    if (!Number.isSafeInteger(pystack) || pystack < 256 || pystack > 1024 * 1024) {
        throw new RangeError("pystack must be an integer between 256 and 1048576 words");
    }
    let Module = {};
    Module.locateFile = (path, scriptDirectory) =>
        url || scriptDirectory + path;
    Module._textDecoder = new TextDecoder();
    if (stdin !== undefined) {
        Module.stdin = stdin;
    }
    if (stdout !== undefined) {
        if (linebuffer) {
            Module._stdoutBuffer = [];
            Module.stdout = (c) => {
                if (c === 10) {
                    stdout(
                        Module._textDecoder.decode(
                            new Uint8Array(Module._stdoutBuffer),
                        ),
                    );
                    Module._stdoutBuffer = [];
                } else {
                    Module._stdoutBuffer.push(c);
                }
            };
        } else {
            Module.stdout = (c) => stdout(new Uint8Array([c]));
        }
    }
    if (stderr !== undefined) {
        if (linebuffer) {
            Module._stderrBuffer = [];
            Module.stderr = (c) => {
                if (c === 10) {
                    stderr(
                        Module._textDecoder.decode(
                            new Uint8Array(Module._stderrBuffer),
                        ),
                    );
                    Module._stderrBuffer = [];
                } else {
                    Module._stderrBuffer.push(c);
                }
            };
        } else {
            Module.stderr = (c) => stderr(new Uint8Array([c]));
        }
    }
    Module = await _createMicroPythonModule(Module);
    globalThis.Module = Module;

    /**
     * Detect the first startup phase that overwrites the low-memory cookie
     *
     * A damaged stack sentinel cannot be repaired by overwriting it again
     * Keep this check before the first MicroPython import or native buffer bind
     *
     * @param {string} phase Completed initialization phase
     * @returns {void}
     */
    const verifyStartupGuard = (phase) => {
        const buffer = Module.HEAPU8.buffer;
        if (buffer.byteLength < 12) {
            throw new Error(`WASM linear memory too small during ${phase}`);
        }
        const view = new DataView(buffer, 0, 12);
        const first = view.getUint32(4, true);
        const second = view.getUint32(8, true);
        // Some non-asserting Emscripten builds do not install a cookie
        const hasCookie = first === 0x02135467 || second === 0x89bacdfe;
        if (hasCookie &&
            (first !== 0x02135467 || second !== 0x89bacdfe)) {
            throw new Error(
                `WASM stack cookie first observed corrupted ${phase}: ` +
                `word[4]=0x${first.toString(16).padStart(8, '0')}, ` +
                `word[8]=0x${second.toString(16).padStart(8, '0')}. ` +
                'This was detected before the device-memory handshake if ' +
                'the phase is during startup.'
            );
        }
    };
    verifyStartupGuard('after Emscripten runtime creation');
    if (typeof Module._mp_js_get_main_dict !== 'function') {
        throw new Error(
            'MicroPython WASM and micropython.mjs are out of sync: ' +
            'missing native mp_js_get_main_dict export. Rebuild and sync both files.'
        );
    }
    // Native CLI transport and GC register scanning are separate capabilities.
    // Device I/O stays synchronous through SharedArrayBuffer, but Emscripten
    // requires an Asyncify-capable exported call to collect Wasm-local roots.
    if (typeof Module._mp_js_native_cli_profile !== 'function' ||
        typeof Module._mp_js_gc_register_scan_asyncify !== 'function') {
        throw new Error('WASM is missing the native CLI / GC ABI. Rebuild and sync .mjs/.wasm together.');
    }
    const nativeCliProfile = Module._mp_js_native_cli_profile() === 1;
    const gcRegisterScanAsyncify = Module._mp_js_gc_register_scan_asyncify() === 1;
    if (nativeCliProfile && !gcRegisterScanAsyncify) {
        throw new Error('Native CLI synchronous GC cannot scan Wasm locals without Asyncify. Rebuild with WASM_GC_REGISTER_SCAN_ASYNCIFY=1.');
    }
    proxy_js_init();
    verifyStartupGuard('after JS proxy initialization');
    const mallocOrThrow = (size) => {
        const ptr = Module._malloc(size);
        if (!ptr) {
            throw new RangeError(`WASM malloc failed for ${size} bytes`);
        }
        return ptr;
    };
    const pyimport = (name) => {
        const value = mallocOrThrow(3 * 4);
        try {
            Module.ccall(
                "mp_js_do_import",
                "null",
                ["string", "pointer"],
                [name, value],
            );
            return proxy_convert_mp_to_js_obj_jsside(value);
        } finally {
            Module._free(value);
        }
    };
    if (Module.ccall("mp_js_init", "number", ["number", "number"], [pystack, heapsize]) !== 0) {
        throw new RangeError(`Could not initialise MicroPython (pystack=${pystack}, heapsize=${heapsize})`);
    }
    verifyStartupGuard('after mp_js_init');
    Module.ccall("proxy_c_init", "null", [], []);
    verifyStartupGuard('after proxy_c_init');
    // A CLI run does not need a JS proxy for __main__ during startup.
    // Eagerly importing __main__ can execute native Python import/proxy code
    // before any device shared-memory handshake has completed.
    // Construct the same globals dictionary proxy lazily on first access.
    let mainModuleDictionary;
    const getMainModuleDictionary = () => {
        if (mainModuleDictionary === undefined) {
            verifyStartupGuard('before resolving __main__ globals');
            const value = mallocOrThrow(3 * 4);
            try {
                Module.ccall(
                    'mp_js_get_main_dict',
                    'null',
                    ['pointer'],
                    [value],
                );
                verifyStartupGuard('after retrieving __main__ globals');
                mainModuleDictionary = proxy_convert_mp_to_js_obj_jsside(value);
                verifyStartupGuard('after converting __main__ globals proxy');
            } finally {
                Module._free(value);
            }
        }
        return mainModuleDictionary;
    };
    // Serialize incoming terminal bytes. Asyncify must never be re-entered
    // while an earlier character is still executing Python code.
    let replPending = Promise.resolve();
    return {
        _module: Module,
        nativeCliProfile,
        gcRegisterScanAsyncify,
        PyProxy: PyProxy,
        FS: Module.FS,
        /**
         * Export native C transport ring addresses into the browser.
         * Views to these static regions remain valid across shared WASM growth.
         * No C/JS bridge is used by device read/write calls.
         *
         * @returns {{ memory: SharedArrayBuffer, regions: Array<object> } | null}
         */
        getNativeSharedTransport() {
            verifyStartupGuard('before native transport region lookup');
            const memory = Module.HEAPU8.buffer;
            if (!(memory instanceof SharedArrayBuffer) ||
                typeof Module._wasm_transport_region !== 'function') {
                return null;
            }
            const regions = [];
            const count = Module._wasm_transport_region_count?.();
            if (!Number.isInteger(count) || count <= 0 || count > 64) {
                throw new Error('Invalid native WASM transport region count');
            }
            for (let index = 0; index < count; index++) {
                const offset = Module._wasm_transport_region(index);
                const size = Module._wasm_transport_region_size(index);
                if (!Number.isInteger(offset) || !Number.isInteger(size) ||
                    offset <= 0 || size <= 0 || offset + size > memory.byteLength) {
                    throw new Error(`Invalid native shared region ${index}`);
                }
                regions.push({ offset, size });
                verifyStartupGuard(`after native region ${index} lookup`);
            }
            const hash32 = Module._wasm_transport_protocol_hash32?.();
            if (!Number.isInteger(hash32)) {
                throw new Error('WASM transport schema hash export is missing');
            }
            return { memory, regions, hash32: hash32 >>> 0 };
        },
        // Fetch the CURRENT linear memory view.  Growth can invalidate any
        // previously cached Uint8Array pointing into WASM memory.
        /**
         * Inspect the low-address Emscripten sentinel seen in builds whose
         * C stack end is zero. This is diagnostic only: other builds can
         * place stack cookies elsewhere, so an unknown layout is not an
         * indication of corruption.
         *
         * @returns {{supported:boolean,intact:boolean,first:number,second:number}}
         */
        getWasmLowMemoryGuard() {
            const buffer = Module.HEAPU8.buffer;
            const view = new DataView(buffer, 0, 12);
            const first = view.getUint32(4, true);
            const second = view.getUint32(8, true);
            const expectedFirst = 0x02135467;
            const expectedSecond = 0x89bacdfe;
            const supported = second === expectedSecond || first === expectedFirst;
            return {
                supported,
                intact: first === expectedFirst && second === expectedSecond,
                first,
                second,
                zero: view.getUint32(0, true),
            };
        },
        getWasmMemoryBytes() {
            return Module.HEAPU8.byteLength;
        },
        // Request an interrupt using the same direct API as the newer port.
        // A VM hook can observe the request while Python is executing.
        requestKeyboardInterrupt() {
            Module.ccall("mp_sched_keyboard_interrupt", "null", [], []);
        },
        globals: {
            get __dict__() {
                return getMainModuleDictionary();
            },
            get(key) {
                return getMainModuleDictionary()[key];
            },
            set(key, value) {
                getMainModuleDictionary()[key] = value;
            },
            delete(key) {
                delete getMainModuleDictionary()[key];
            },
        },
        registerJsModule(name, module) {
            const value = mallocOrThrow(3 * 4);
            try {
                proxy_convert_js_to_mp_obj_jsside(module, value);
                Module.ccall(
                    "mp_js_register_js_module",
                    "null",
                    ["string", "pointer"],
                    [name, value],
                );
            } finally {
                Module._free(value);
            }
        },
        pyimport: pyimport,
        // Keep a Promise-based public API for both profiles. Native CLI makes
        // the C call synchronously; the optional Asyncify profile may suspend.
        async runPython(code) {
            const len = Module.lengthBytesUTF8(code);
            let buf = 0;
            let value = 0;
            try {
                buf = mallocOrThrow(len + 1);
                Module.stringToUTF8(code, buf, len + 1);
                value = mallocOrThrow(3 * 4);
                if (nativeCliProfile && !gcRegisterScanAsyncify) {
                    Module.ccall(
                        "mp_js_do_exec",
                        "null",
                        ["pointer", "number", "pointer"],
                        [buf, len, value],
                    );
                } else {
                    await Module.ccall(
                        "mp_js_do_exec",
                        "null",
                        ["pointer", "number", "pointer"],
                        [buf, len, value],
                        { async: true },
                    );
                }
                return proxy_convert_mp_to_js_obj_jsside(value);
            } finally {
                if (value) Module._free(value);
                if (buf) Module._free(buf);
            }
        },
        async runPythonAsync(code) {
            const len = Module.lengthBytesUTF8(code);
            let buf = 0;
            let value = 0;
            let ret;
            try {
                buf = mallocOrThrow(len + 1);
                Module.stringToUTF8(code, buf, len + 1);
                value = mallocOrThrow(3 * 4);
                if (nativeCliProfile && !gcRegisterScanAsyncify) {
                    Module.ccall(
                        "mp_js_do_exec_async",
                        "null",
                        ["pointer", "number", "pointer"],
                        [buf, len, value],
                    );
                } else {
                    await Module.ccall(
                        "mp_js_do_exec_async",
                        "null",
                        ["pointer", "number", "pointer"],
                        [buf, len, value],
                        { async: true },
                    );
                }
                ret = proxy_convert_mp_to_js_obj_jsside(value);
            } finally {
                if (value) Module._free(value);
                if (buf) Module._free(buf);
            }
            // Promote a top-level Python awaitable into a JS promise.
            return ret instanceof PyProxyThenable ? Promise.resolve(ret) : ret;
        },
        // Browser terminal REPL. Initialise before processing keypresses.
        replInit() {
            Module.ccall("mp_js_repl_init", "null", [], []);
        },
        // Synchronous compatibility API: do NOT use for code that may
        // suspend, e.g. time.sleep() or JavaScript-bridged async operations.
        replProcessChar(chr) {
            if (gcRegisterScanAsyncify) {
                throw new Error('Use replProcessCharWithAsyncify() with synchronous WebAssembly GC');
            }
            return Module.ccall(
                "mp_js_repl_process_char", "number", ["number"], [chr],
            );
        },
        // Preferred browser API; returns a Promise. Character handling is
        // sequenced so concurrent key events cannot re-enter Asyncify.
        replProcessCharWithAsyncify(chr) {
            const pending = replPending.then(() => {
                if (nativeCliProfile && !gcRegisterScanAsyncify) {
                    return Module.ccall(
                        "mp_js_repl_process_char_async",
                        "number", ["number"], [chr],
                    );
                }
                return Module.ccall(
                    "mp_js_repl_process_char_async",
                    "number", ["number"], [chr], { async: true },
                );
            });
            replPending = pending.catch(() => {});
            return pending;
        },
        interrupt() {
            Module.ccall("mp_sched_keyboard_interrupt", "null", [], []);
        },
    };
}

globalThis.loadMicroPython = loadMicroPython;
