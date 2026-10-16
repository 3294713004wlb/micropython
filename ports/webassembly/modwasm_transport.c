/*
 * Native SAB data plane for browser MicroPython CLI transports.
 *
 * The browser owns the Web Serial/HID/USB objects. The worker exposes their
 * job-scoped SAB rings as a synchronous bridge. These EM_JS imports copy
 * byte blocks directly between the native WASM heap and the SAB rings,
 * bypassing per-byte JS proxies and JSON/hex encodings.
 *
 * Ordinary (non-shared) WASM linear memory is not a SharedArrayBuffer, so the
 * HEAPU8 <-> SAB copy remains necessary. It is one bounded bulk copy.
 */
#include <stdint.h>
#include <string.h>

#include <emscripten.h>
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"

#define MAX_TRANSFER (1024 * 1024 - 1)

EM_JS(int, native_serial_available, (), {
    const bridge = globalThis.__mpWasmTransport;
    return bridge ? bridge.serialAvailable() : -1;
});

EM_JS(int, native_serial_read, (uint8_t *dst, int size), {
    const bridge = globalThis.__mpWasmTransport;
    return bridge ? bridge.serialRead(HEAPU8.subarray(dst, dst + size)) : -1;
});

EM_JS(int, native_serial_write, (const uint8_t *src, int size), {
    const bridge = globalThis.__mpWasmTransport;
    return bridge ? bridge.serialWrite(HEAPU8.subarray(src, src + size)) : -1;
});

EM_JS(int, native_serial_wait, (int ms), {
    const bridge = globalThis.__mpWasmTransport;
    return bridge && bridge.serialWait(ms) ? 1 : 0;
});

/* For reads the result is copied into the caller's native buffer. */
EM_JS(int, native_transfer, (int protocol, int opcode, const char *id,
    int paramA, int paramB, const uint8_t *tx, int txLen,
    uint8_t *rx, int rxLength, int timeout), {
    const bridge = globalThis.__mpWasmTransport;
    if (!bridge) return -1;
    const bytes = HEAPU8.subarray(tx, tx + txLen);
    const result = protocol === 1
        ? bridge.usbTransfer(opcode, paramA, paramB, bytes, rxLength, timeout)
        : bridge.hidTransfer(opcode, UTF8ToString(id), paramA, bytes, rxLength, timeout);
    if (result === null || result === undefined) return -1;
    if (typeof result === 'number') return result;
    if (result.byteLength > rxLength) return -1;
    HEAPU8.set(result, rx);
    return result.byteLength;
});

EM_JS(int, native_usb_control, (int opcode, int bm, int req, int value,
    int index, const uint8_t *tx, int txLen, uint8_t *rx, int rxLength,
    int timeout), {
    const bridge = globalThis.__mpWasmTransport;
    if (!bridge) return -1;
    const result = bridge.usbControl(opcode, bm, req, value, index,
        HEAPU8.subarray(tx, tx + txLen), rxLength, timeout);
    if (result === null || result === undefined) return -1;
    if (typeof result === 'number') return result;
    if (result.byteLength > rxLength) return -1;
    HEAPU8.set(result, rx);
    return result.byteLength;
});

EM_JS(void, native_error, (char *buf, int len), {
    const error = globalThis.__mpWasmTransport?.lastError() || 'transport unavailable';
    stringToUTF8(error, buf, len);
});

static void raise_native_error(void) {
    char message[256];
    native_error(message, sizeof(message));
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("Web transport: %s"), message);
}

static size_t checked_length(mp_obj_t arg) {
    mp_int_t value = mp_obj_get_int(arg);
    if (value < 0 || value > MAX_TRANSFER) {
        mp_raise_ValueError(MP_ERROR_TEXT("transfer length out of range"));
    }
    return (size_t)value;
}

static int checked_timeout(mp_obj_t arg) {
    return arg == mp_const_none ? 0 : mp_obj_get_int(arg);
}

static mp_obj_t native_available_py(void) {
    int n = native_serial_available();
    if (n < 0) raise_native_error();
    return MP_OBJ_NEW_SMALL_INT(n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_available_obj, native_available_py);

static mp_obj_t native_rx_py(mp_obj_t length) {
    size_t n = checked_length(length);
    if (n == 0) return mp_obj_new_bytes((const byte *)"", 0);
    uint8_t *buffer = m_new(uint8_t, n);
    int count = native_serial_read(buffer, n);
    if (count < 0) {
        m_del(uint8_t, buffer, n);
        raise_native_error();
    }
    mp_obj_t result = mp_obj_new_bytes(buffer, count);
    m_del(uint8_t, buffer, n);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_rx_obj, native_rx_py);

static mp_obj_t native_tx_py(mp_obj_t data) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(data, &info, MP_BUFFER_READ);
    if (info.len > MAX_TRANSFER) {
        mp_raise_ValueError(MP_ERROR_TEXT("serial write too large"));
    }
    int n = native_serial_write(info.buf, info.len);
    if (n < 0) raise_native_error();
    return mp_obj_new_int(n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_tx_obj, native_tx_py);

static mp_obj_t native_wait_py(mp_obj_t timeout) {
    return mp_obj_new_bool(native_serial_wait(mp_obj_get_int(timeout)) != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_wait_obj, native_wait_py);

/* Transfers accept bytes / memoryview directly; no Python byte-list proxy. */
static mp_obj_t transfer_binary(int protocol, int opcode, const char *id,
    int paramA, int paramB, mp_obj_t input, mp_obj_t length, mp_obj_t timeout,
    bool return_bytes) {
    mp_buffer_info_t info = { .buf = NULL, .len = 0 };
    if (input != mp_const_none) {
        mp_get_buffer_raise(input, &info, MP_BUFFER_READ);
    }
    size_t rx_length = return_bytes ? checked_length(length) : 0;
    if (info.len > MAX_TRANSFER) {
        mp_raise_ValueError(MP_ERROR_TEXT("transfer exceeds shared ring capacity"));
    }
    // At least one byte is allocated to avoid zero-length pointer pitfalls.
    uint8_t *rx = m_new(uint8_t, rx_length ? rx_length : 1);
    int count = native_transfer(protocol, opcode, id, paramA, paramB,
        info.buf, info.len, rx, rx_length, checked_timeout(timeout));
    if (count < 0 || (return_bytes && (size_t)count > rx_length)) {
        m_del(uint8_t, rx, rx_length ? rx_length : 1);
        raise_native_error();
    }
    mp_obj_t result = return_bytes ? mp_obj_new_bytes(rx, count) : mp_obj_new_int(count);
    m_del(uint8_t, rx, rx_length ? rx_length : 1);
    return result;
}

static mp_obj_t usb_out_py(mp_obj_t ep, mp_obj_t data, mp_obj_t timeout) {
    return transfer_binary(1, 1, "", mp_obj_get_int(ep), 0,
        data, MP_OBJ_NEW_SMALL_INT(0), timeout, false);
}
static MP_DEFINE_CONST_FUN_OBJ_3(usb_out_obj, usb_out_py);

static mp_obj_t usb_in_py(mp_obj_t ep, mp_obj_t length, mp_obj_t timeout) {
    return transfer_binary(1, 2, "", 0, mp_obj_get_int(ep),
        mp_const_none, length, timeout, true);
}
static MP_DEFINE_CONST_FUN_OBJ_3(usb_in_obj, usb_in_py);

static mp_obj_t usb_out_in_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return transfer_binary(1, 3, "", mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
        args[2], args[3], args[4], true);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_out_in_obj, 5, 5, usb_out_in_py);

static mp_obj_t usb_control_transfer_py(size_t n, const mp_obj_t *args) {
    (void)n;
    int bm = mp_obj_get_int(args[0]) & 0xff;
    int req = mp_obj_get_int(args[1]) & 0xff;
    int value = mp_obj_get_int(args[2]) & 0xffff;
    int index = mp_obj_get_int(args[3]) & 0xffff;
    bool incoming = (bm & 0x80) != 0;
    mp_buffer_info_t info = { .buf = NULL, .len = 0 };
    size_t length = 0;
    if (incoming) {
        length = checked_length(args[4]);
    } else if (args[4] != mp_const_none) {
        mp_get_buffer_raise(args[4], &info, MP_BUFFER_READ);
    }
    if (info.len > MAX_TRANSFER) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB control transfer too large"));
    }
    uint8_t *rx = m_new(uint8_t, length ? length : 1);
    int count = native_usb_control(incoming ? 5 : 4, bm, req, value, index,
        info.buf, info.len, rx, length, checked_timeout(args[5]));
    if (count < 0 || (incoming && (size_t)count > length)) {
        m_del(uint8_t, rx, length ? length : 1);
        raise_native_error();
    }
    mp_obj_t result = incoming ? mp_obj_new_bytes(rx, count) : mp_obj_new_int(count);
    m_del(uint8_t, rx, length ? length : 1);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_control_transfer_obj, 6, 6, usb_control_transfer_py);

static mp_obj_t hid_write_py(mp_obj_t id, mp_obj_t data) {
    return transfer_binary(2, 1, mp_obj_str_get_str(id), 0, 0,
        data, MP_OBJ_NEW_SMALL_INT(0), mp_const_none, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hid_write_obj, hid_write_py);

static mp_obj_t hid_read_py(mp_obj_t id, mp_obj_t length, mp_obj_t timeout) {
    return transfer_binary(2, 2, mp_obj_str_get_str(id), 0, 0,
        mp_const_none, length, timeout, true);
}
static MP_DEFINE_CONST_FUN_OBJ_3(hid_read_obj, hid_read_py);

static mp_obj_t hid_output_py(mp_obj_t id, mp_obj_t data) {
    return transfer_binary(2, 3, mp_obj_str_get_str(id), 0, 0,
        data, MP_OBJ_NEW_SMALL_INT(0), mp_const_none, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hid_output_obj, hid_output_py);

static mp_obj_t hid_feature_write_py(mp_obj_t id, mp_obj_t data) {
    return transfer_binary(2, 4, mp_obj_str_get_str(id), 0, 0,
        data, MP_OBJ_NEW_SMALL_INT(0), mp_const_none, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hid_feature_write_obj, hid_feature_write_py);

static mp_obj_t hid_feature_read_py(mp_obj_t id, mp_obj_t report_id, mp_obj_t length) {
    return transfer_binary(2, 5, mp_obj_str_get_str(id), mp_obj_get_int(report_id), 0,
        mp_const_none, length, mp_const_none, true);
}
static MP_DEFINE_CONST_FUN_OBJ_3(hid_feature_read_obj, hid_feature_read_py);

static const mp_rom_map_elem_t wasm_transport_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_wasm_transport) },
    { MP_ROM_QSTR(MP_QSTR_serial_available), MP_ROM_PTR(&native_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_read), MP_ROM_PTR(&native_rx_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_write), MP_ROM_PTR(&native_tx_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_wait), MP_ROM_PTR(&native_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_out), MP_ROM_PTR(&usb_out_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_in), MP_ROM_PTR(&usb_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_out_in), MP_ROM_PTR(&usb_out_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_control_transfer), MP_ROM_PTR(&usb_control_transfer_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_write), MP_ROM_PTR(&hid_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_read), MP_ROM_PTR(&hid_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_output), MP_ROM_PTR(&hid_output_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_feature_write), MP_ROM_PTR(&hid_feature_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_feature_read), MP_ROM_PTR(&hid_feature_read_obj) },
};
static MP_DEFINE_CONST_DICT(wasm_transport_globals, wasm_transport_globals_table);

const mp_obj_module_t wasm_transport_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wasm_transport_globals,
};
MP_REGISTER_MODULE(MP_QSTR_wasm_transport, wasm_transport_module);
