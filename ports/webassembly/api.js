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
// - heapsize: initial size (bytes) of the MicroPython GC heap; it may expand.
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
    proxy_js_init();
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
    Module.ccall("proxy_c_init", "null", [], []);
    // Serialize incoming terminal bytes. Asyncify must never be re-entered
    // while an earlier character is still executing Python code.
    let replPending = Promise.resolve();
    return {
        _module: Module,
        PyProxy: PyProxy,
        FS: Module.FS,
        // Fetch the CURRENT linear memory view.  Growth can invalidate any
        // previously cached Uint8Array pointing into WASM memory.
        getWasmMemoryBytes() {
            return Module.HEAPU8.byteLength;
        },
        // Request an interrupt using the same direct API as the newer port.
        // A VM hook can observe the request while Python is executing.
        requestKeyboardInterrupt() {
            Module.ccall("mp_sched_keyboard_interrupt", "null", [], []);
        },
        globals: {
            __dict__: pyimport("__main__").__dict__,
            get(key) {
                return this.__dict__[key];
            },
            set(key, value) {
                this.__dict__[key] = value;
            },
            delete(key) {
                delete this.__dict__[key];
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
        // With Asyncify, even ordinary execution can suspend during JS calls.
        // Always await it; pointers remain valid until the WASM call completes.
        async runPython(code) {
            const len = Module.lengthBytesUTF8(code);
            let buf = 0;
            let value = 0;
            try {
                buf = mallocOrThrow(len + 1);
                Module.stringToUTF8(code, buf, len + 1);
                value = mallocOrThrow(3 * 4);
                await Module.ccall(
                    "mp_js_do_exec",
                    "null",
                    ["pointer", "number", "pointer"],
                    [buf, len, value],
                    { async: true },
                );
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
                await Module.ccall(
                    "mp_js_do_exec_async",
                    "null",
                    ["pointer", "number", "pointer"],
                    [buf, len, value],
                    { async: true },
                );
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
            return Module.ccall(
                "mp_js_repl_process_char", "number", ["number"], [chr],
            );
        },
        // Preferred browser API; returns a Promise. Character handling is
        // sequenced so concurrent key events cannot re-enter Asyncify.
        replProcessCharWithAsyncify(chr) {
            const pending = replPending.then(() => Module.ccall(
                "mp_js_repl_process_char_async",
                "number", ["number"], [chr], { async: true },
            ));
            replPending = pending.catch(() => {});
            return pending;
        },
        interrupt() {
            Module.ccall("mp_sched_keyboard_interrupt", "null", [], []);
        },
    };
}

globalThis.loadMicroPython = loadMicroPython;
