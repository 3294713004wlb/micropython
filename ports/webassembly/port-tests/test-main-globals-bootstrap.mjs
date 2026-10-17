/**
 * Verify that loading the browser MicroPython wrapper does not import
 * __main__ or create a Python/JS globals proxy before first use.
 * This test runs against a stubbed Emscripten module; it is not a WASM
 * execution test and cannot establish that native memory corruption is fixed.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../api.js', import.meta.url), 'utf8');
const calls = [];
const memory = new SharedArrayBuffer(64 * 1024);
const view = new DataView(memory);
view.setUint32(4, 0x02135467, true);
view.setUint32(8, 0x89bacdfe, true);
let allocation = 4096;
const dictionary = { value: 42 };
const Module = {
    HEAPU8: new Uint8Array(memory),
    _mp_js_get_main_dict() {},
    _mp_js_native_cli_profile() { return 1; },
    _malloc(length) {
        const address = allocation;
        allocation += (length + 7) & ~7;
        return address;
    },
    _free() {},
    ccall(name, _returnType, _types, params) {
        calls.push(name);
        if (name === 'mp_js_init') {
            return 0;
        }
        if (name === 'mp_js_get_main_dict') {
            view.setUint32(params[0], 8, true);
            view.setUint32(params[0] + 4, 1, true);
        }
        return null;
    },
};
const context = vm.createContext({
    SharedArrayBuffer,
    Uint8Array,
    DataView,
    TextDecoder,
    WeakRef,
    FinalizationRegistry,
    console,
    _createMicroPythonModule: async () => Module,
    proxy_js_init() {},
    PyProxy: class PyProxy {},
    proxy_convert_mp_to_js_obj_jsside() {
        return dictionary;
    },
});
vm.runInContext(source.replace('export async function loadMicroPython', 'async function loadMicroPython'), context);
const mp = await context.loadMicroPython({});
assert.equal(calls.filter((name) => name === 'mp_js_do_import').length, 0);
assert.equal(calls.filter((name) => name === 'mp_js_get_main_dict').length, 0);
assert.equal(mp.globals.get('value'), 42);
assert.equal(calls.filter((name) => name === 'mp_js_get_main_dict').length, 1);
mp.globals.set('value', 99);
assert.equal(mp.globals.__dict__.value, 99);
assert.equal(calls.filter((name) => name === 'mp_js_get_main_dict').length, 1);
assert.equal(view.getUint32(4, true), 0x02135467);
assert.equal(view.getUint32(8, true), 0x89bacdfe);
console.log('Lazy __main__ globals bootstrap: PASS');
