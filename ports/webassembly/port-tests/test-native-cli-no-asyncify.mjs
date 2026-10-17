/**
 * Check that the native CLI calls C synchronously and that the optional
 * Asyncify build retains asynchronous ccall semantics.
 * This is a wrapper test; it cannot replace a compiled Emscripten test.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../api.js', import.meta.url), 'utf8');

for (const profile of [0, 1]) {
    const heap = new SharedArrayBuffer(1024 * 1024);
    const view = new DataView(heap);
    view.setUint32(4, 0x02135467, true);
    view.setUint32(8, 0x89bacdfe, true);
    const calls = [];
    let next = 4096;
    const Module = {
        HEAPU8: new Uint8Array(heap),
        _mp_js_native_cli_profile() { return profile; },
        _mp_js_get_main_dict() {},
        _malloc(size) { const ptr = next; next += size + 8; return ptr; },
        _free() {},
        lengthBytesUTF8(value) { return value.length; },
        stringToUTF8() {},
        ccall(name, returnType, types, args, opts) {
            calls.push({ name, opts });
            if (name === 'mp_js_init') return 0;
            if (name === 'mp_js_do_exec' || name === 'mp_js_do_exec_async') {
                return opts?.async ? Promise.resolve(null) : null;
            }
            return null;
        },
    };
    const ctx = vm.createContext({
        SharedArrayBuffer, Uint8Array, DataView, TextDecoder,
        WeakRef, FinalizationRegistry, console,
        _createMicroPythonModule: async () => Module,
        proxy_js_init() {},
        PyProxy: class {},
        PyProxyThenable: class {},
        proxy_convert_mp_to_js_obj_jsside() { return null; },
    });
    vm.runInContext(
        source.replace('export async function loadMicroPython', 'async function loadMicroPython'),
        ctx,
    );
    const mp = await ctx.loadMicroPython({});
    await mp.runPython('print(1)');
    await mp.runPythonAsync('print(2)');
    await mp.replProcessCharWithAsyncify(65);
    const execCalls = calls.filter(item =>
        item.name === 'mp_js_do_exec' ||
        item.name === 'mp_js_do_exec_async' ||
        item.name === 'mp_js_repl_process_char_async'
    );
    assert.equal(execCalls.length, 3);
    for (const call of execCalls) {
        assert.equal(Boolean(call.opts?.async), profile === 0);
    }
}
console.log('Native CLI without Asyncify: PASS');
