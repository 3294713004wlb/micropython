/**
 * MicroPython wasm_transport Python bindings
 *
 * This file only translates Python objects and errors. The protocol,
 * shared-memory storage, packet state machine and serial streams live
 * in transport .c and are independently testable.
 */
#include <stdint.h>
#include <stdbool.h>
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
#include "transport/transport.h"

/* Additive per-request opcodes; the shared region ABI remains v5. */
#define TP_OPCODE_USB_BATCH_EXCHANGE 6
#define TP_OPCODE_HID_BATCH_EXCHANGE 6

static void raise_native_error(void) {
    const char *msg = transport_last_error()[0] ? transport_last_error() :
        "Device timeout, cancellation or transfer error";
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("Web transport: %s"), msg);
}

static size_t checked_length(mp_obj_t arg) {
    mp_int_t value = mp_obj_get_int(arg);
    if (value < 0 || value > (TP_REGION_SIZE_USB_TX - 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("transfer length out of range"));
    }
    return (size_t)value;
}

static int checked_timeout(mp_obj_t arg) {
    return arg == mp_const_none ? 0 : mp_obj_get_int(arg);
}

static mp_obj_t native_available_py(void) {
    int n = transport_serial_available();
    if (n < 0) raise_native_error();
    return MP_OBJ_NEW_SMALL_INT(n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_available_obj, native_available_py);

static mp_obj_t native_rx_py(mp_obj_t length) {
    size_t n = checked_length(length);
    if (n == 0) return mp_obj_new_bytes((const byte *)"", 0);
    uint8_t *buffer = m_new(uint8_t, n);
    int count = transport_serial_read(buffer, n);
    if (count < 0) {
        m_del(uint8_t, buffer, n);
        raise_native_error();
    }
    mp_obj_t result = mp_obj_new_bytes(buffer, count);
    m_del(uint8_t, buffer, n);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_rx_obj, native_rx_py);

/* Fill a Python-owned writable buffer directly; no temporary bytes object. */
static mp_obj_t native_readinto_py(mp_obj_t target) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(target, &info, MP_BUFFER_WRITE);
    if (info.len > (TP_REGION_SIZE_USB_TX - 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("serial readinto too large"));
    }
    int count = transport_serial_read(info.buf, (int)info.len);
    if (count < 0) raise_native_error();
    return mp_obj_new_int(count);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_readinto_obj, native_readinto_py);

static mp_obj_t native_tx_py(mp_obj_t data) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(data, &info, MP_BUFFER_READ);
    if (info.len > (TP_REGION_SIZE_USB_TX - 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("serial write too large"));
    }
    int n = transport_serial_write(info.buf, info.len);
    if (n < 0) raise_native_error();
    return mp_obj_new_int(n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_tx_obj, native_tx_py);

static mp_obj_t native_wait_py(mp_obj_t timeout) {
    return mp_obj_new_bool(transport_serial_wait(mp_obj_get_int(timeout)) != 0);
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
    if (info.len > (TP_REGION_SIZE_USB_TX - 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("transfer exceeds shared slot capacity"));
    }
    if (protocol == TP_KIND_HID && (info.len > TP_REGION_SIZE_HID_TX || rx_length > TP_REGION_SIZE_HID_TX)) {
        mp_raise_ValueError(MP_ERROR_TEXT("HID report exceeds native slot capacity"));
    }
    // At least one byte is allocated to avoid zero-length pointer pitfalls.
    uint8_t *rx = m_new(uint8_t, rx_length ? rx_length : 1);
    int count = transport_transfer(protocol, opcode, id, paramA, paramB,
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
    return transfer_binary(TP_KIND_USB, TP_OPCODE_USB_OUT, "", mp_obj_get_int(ep), 0,
        data, MP_OBJ_NEW_SMALL_INT(0), timeout, false);
}
static MP_DEFINE_CONST_FUN_OBJ_3(usb_out_obj, usb_out_py);

static mp_obj_t usb_in_py(mp_obj_t ep, mp_obj_t length, mp_obj_t timeout) {
    return transfer_binary(TP_KIND_USB, TP_OPCODE_USB_IN, "", 0, mp_obj_get_int(ep),
        mp_const_none, length, timeout, true);
}
static MP_DEFINE_CONST_FUN_OBJ_3(usb_in_obj, usb_in_py);

static mp_obj_t usb_out_in_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return transfer_binary(TP_KIND_USB, TP_OPCODE_USB_OUT_IN, "", mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
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
    if (info.len > (TP_REGION_SIZE_USB_TX - 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB control transfer too large"));
    }
    uint8_t *rx = m_new(uint8_t, length ? length : 1);
    int count = transport_usb_control(incoming ? TP_OPCODE_USB_CONTROL_IN : TP_OPCODE_USB_CONTROL_OUT, bm, req, value, index,
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
    return transfer_binary(TP_KIND_HID, TP_OPCODE_HID_WRITE, mp_obj_str_get_str(id), 0, 0,
        data, MP_OBJ_NEW_SMALL_INT(0), mp_const_none, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hid_write_obj, hid_write_py);

static mp_obj_t hid_read_py(mp_obj_t id, mp_obj_t length, mp_obj_t timeout) {
    return transfer_binary(TP_KIND_HID, TP_OPCODE_HID_READ, mp_obj_str_get_str(id), 0, 0,
        mp_const_none, length, timeout, true);
}
static MP_DEFINE_CONST_FUN_OBJ_3(hid_read_obj, hid_read_py);

static mp_obj_t hid_output_py(mp_obj_t id, mp_obj_t data) {
    return transfer_binary(TP_KIND_HID, TP_OPCODE_HID_OUTPUT, mp_obj_str_get_str(id), 0, 0,
        data, MP_OBJ_NEW_SMALL_INT(0), mp_const_none, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hid_output_obj, hid_output_py);

static mp_obj_t hid_feature_write_py(mp_obj_t id, mp_obj_t data) {
    return transfer_binary(TP_KIND_HID, TP_OPCODE_HID_FEATURE_WRITE, mp_obj_str_get_str(id), 0, 0,
        data, MP_OBJ_NEW_SMALL_INT(0), mp_const_none, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(hid_feature_write_obj, hid_feature_write_py);

static mp_obj_t hid_feature_read_py(mp_obj_t id, mp_obj_t report_id, mp_obj_t length) {
    return transfer_binary(TP_KIND_HID, TP_OPCODE_HID_FEATURE_READ, mp_obj_str_get_str(id), mp_obj_get_int(report_id), 0,
        mp_const_none, length, mp_const_none, true);
}
static MP_DEFINE_CONST_FUN_OBJ_3(hid_feature_read_obj, hid_feature_read_py);

/*
 * An ordered batch is one native mailbox transaction. This avoids requiring
 * Python threads or concurrently publishing multiple mailbox descriptors.
 *
 * Wire format: uint16 count, then repeated (uint16 length, bytes). The high
 * bit of a USB length requests a ZLP after that frame without an extra reply.
 * The returned frames use the same format without flags.
 */
static mp_obj_t exchange_batch(int kind, const char *id, int ep_out,
    int ep_in, mp_obj_t packed, mp_obj_t max_length, mp_obj_t timeout) {
    mp_buffer_info_t input;
    mp_get_buffer_raise(packed, &input, MP_BUFFER_READ);
    size_t limit = kind == TP_KIND_USB ? TP_REGION_SIZE_USB_TX : TP_REGION_SIZE_HID_TX;
    size_t response_capacity = checked_length(max_length);
    if (input.len < 2 || input.len > limit || response_capacity < 2 ||
        response_capacity > limit) {
        mp_raise_ValueError(MP_ERROR_TEXT("batch frame length exceeds native mailbox"));
    }
    /* Validate framing in C as well as JS; prevents accidental buffer
     * over-read from malformed Python input. */
    const uint8_t *cursor = input.buf;
    size_t count = (size_t)cursor[0] | ((size_t)cursor[1] << 8);
    if (count == 0 || count > 64) {
        mp_raise_ValueError(MP_ERROR_TEXT("batch count must be 1..64"));
    }
    size_t offset = 2;
    for (size_t i = 0; i < count; ++i) {
        if (offset + 2 > input.len) {
            mp_raise_ValueError(MP_ERROR_TEXT("truncated batch length"));
        }
        size_t size = (size_t)(cursor[offset] | (cursor[offset + 1] << 8)) & 0x7fff;
        offset += 2 + size;
        if (offset > input.len) {
            mp_raise_ValueError(MP_ERROR_TEXT("truncated batch frame"));
        }
    }
    if (offset != input.len) {
        mp_raise_ValueError(MP_ERROR_TEXT("extra bytes after batch frames"));
    }
    uint8_t *rx = m_new(uint8_t, response_capacity);
    int result = transport_transfer(kind,
        kind == TP_KIND_USB ? TP_OPCODE_USB_BATCH_EXCHANGE : TP_OPCODE_HID_BATCH_EXCHANGE,
        id, ep_out, ep_in, input.buf, (int)input.len,
        rx, (int)response_capacity, checked_timeout(timeout));
    if (result < 0 || (size_t)result > response_capacity) {
        m_del(uint8_t, rx, response_capacity);
        raise_native_error();
    }
    mp_obj_t output = mp_obj_new_bytes(rx, (size_t)result);
    m_del(uint8_t, rx, response_capacity);
    return output;
}

static mp_obj_t usb_batch_exchange_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return exchange_batch(TP_KIND_USB, "", mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), args[2], args[3], args[4]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_batch_exchange_obj, 5, 5, usb_batch_exchange_py);

static mp_obj_t hid_batch_exchange_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return exchange_batch(TP_KIND_HID, mp_obj_str_get_str(args[0]),
        0, 0, args[1], args[2], args[3]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hid_batch_exchange_obj, 4, 4, hid_batch_exchange_py);


/*
 * Persistent ordered CMSIS-DAP stream.
 * SUBMIT takes ownership of one packet, TAKE retrieves one response;
 * the JS transport Worker advances TX and RX independently between calls.
 * Only the browser backend uses these exports. The existing Python
 * Interface.write/read signatures and command encoding stay unchanged.
 */
#define TP_OPCODE_STREAM_SUBMIT 7
#define TP_OPCODE_STREAM_TAKE 8
#define TP_OPCODE_STREAM_RESET 9

static mp_obj_t stream_transfer(int kind, int opcode, const char *id,
    int ep_out, int ep_in, mp_obj_t payload, mp_obj_t capacity, mp_obj_t timeout) {
    mp_buffer_info_t input = { .buf = NULL, .len = 0 };
    if (payload != mp_const_none) {
        mp_get_buffer_raise(payload, &input, MP_BUFFER_READ);
    }
    size_t limit = kind == TP_KIND_HID ? TP_REGION_SIZE_HID_TX : TP_REGION_SIZE_USB_TX;
    size_t output_size = (opcode == TP_OPCODE_STREAM_TAKE ||
        opcode == TP_OPCODE_USB_REPEAT_READ) ? checked_length(capacity) : 0;
    if (input.len > limit || output_size > limit) {
        mp_raise_ValueError(MP_ERROR_TEXT("stream frame exceeds mailbox capacity"));
    }
    uint8_t *output = m_new(uint8_t, output_size ? output_size : 1);
    int result = transport_transfer(kind, opcode, id, ep_out, ep_in,
        input.buf, (int)input.len, output, (int)output_size, checked_timeout(timeout));
    if (result < 0 || (output_size && (size_t)result > output_size)) {
        m_del(uint8_t, output, output_size ? output_size : 1);
        raise_native_error();
    }
    mp_obj_t value = output_size ? mp_obj_new_bytes(output, result) : mp_obj_new_int(result);
    m_del(uint8_t, output, output_size ? output_size : 1);
    return value;
}

static mp_obj_t hid_stream_submit_py(mp_obj_t id, mp_obj_t packet, mp_obj_t timeout) {
    return stream_transfer(TP_KIND_HID, TP_OPCODE_STREAM_SUBMIT,
        mp_obj_str_get_str(id), 0, 0, packet, mp_const_none, timeout);
}
static MP_DEFINE_CONST_FUN_OBJ_3(hid_stream_submit_obj, hid_stream_submit_py);

static mp_obj_t hid_stream_take_py(mp_obj_t id, mp_obj_t capacity, mp_obj_t timeout) {
    return stream_transfer(TP_KIND_HID, TP_OPCODE_STREAM_TAKE,
        mp_obj_str_get_str(id), 0, 0, mp_const_none, capacity, timeout);
}
static MP_DEFINE_CONST_FUN_OBJ_3(hid_stream_take_obj, hid_stream_take_py);

static mp_obj_t hid_stream_reset_py(mp_obj_t id) {
    return stream_transfer(TP_KIND_HID, TP_OPCODE_STREAM_RESET,
        mp_obj_str_get_str(id), 0, 0, mp_const_none, mp_const_none, mp_const_none);
}
static MP_DEFINE_CONST_FUN_OBJ_1(hid_stream_reset_obj, hid_stream_reset_py);

static mp_obj_t usb_stream_submit_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return stream_transfer(TP_KIND_USB, TP_OPCODE_STREAM_SUBMIT, "",
        mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
        args[2], mp_const_none, args[3]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_stream_submit_obj, 4, 4, usb_stream_submit_py);

static mp_obj_t usb_stream_take_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return stream_transfer(TP_KIND_USB, TP_OPCODE_STREAM_TAKE, "",
        mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
        mp_const_none, args[2], args[3]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_stream_take_obj, 4, 4, usb_stream_take_py);

/*
 * Execute an entire DAP_TransferBlock repeat in the USB I/O Worker.
 * The one-slot mailbox can carry up to 1 MiB; pyOCD uses <=1 KiB pages.
 * Read returns packed little-endian words, write returns 0 on success.
 */
static mp_obj_t usb_dap_repeat_py(size_t n, const mp_obj_t *args) {
    (void)n;
    int is_write = mp_obj_get_int(args[4]) != 0;
    return stream_transfer(TP_KIND_USB,
        is_write ? TP_OPCODE_USB_REPEAT_WRITE : TP_OPCODE_USB_REPEAT_READ,
        "", mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
        args[2], is_write ? mp_const_none : args[3], args[5]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    usb_dap_repeat_obj, 6, 6, usb_dap_repeat_py);

static mp_obj_t usb_stream_reset_py(mp_obj_t ep_out, mp_obj_t ep_in) {
    return stream_transfer(TP_KIND_USB, TP_OPCODE_STREAM_RESET, "",
        mp_obj_get_int(ep_out), mp_obj_get_int(ep_in),
        mp_const_none, mp_const_none, mp_const_none);
}
static MP_DEFINE_CONST_FUN_OBJ_2(usb_stream_reset_obj, usb_stream_reset_py);

/** Non-blocking report submission to the shared TX ring. */
static mp_obj_t usb_pipe_submit_py(size_t n, const mp_obj_t *args) {
    (void)n;
    mp_buffer_info_t info;
    mp_get_buffer_raise(args[2], &info, MP_BUFFER_READ);
    int transferred = transport_usb_pipe_submit(mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), info.buf, info.len, checked_timeout(args[3]));
    if (transferred < 0) raise_native_error();
    return mp_obj_new_int(transferred);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_pipe_submit_obj, 4, 4, usb_pipe_submit_py);

/** Block only when the next response has not yet reached the RX ring. */
/** Start one contiguous read job without reserving the single-shot mailbox. */
static mp_obj_t usb_pipe_span_start_py(size_t n, const mp_obj_t *args) {
    (void)n;
    mp_buffer_info_t info;
    mp_get_buffer_raise(args[2], &info, MP_BUFFER_READ);
    if (info.len != 10) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB pipe span descriptor must be 10 bytes"));
    }
    int transferred = transport_usb_pipe_submit_typed(mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), info.buf, info.len, checked_timeout(args[3]), 1);
    if (transferred < 0) raise_native_error();
    return mp_obj_new_int(transferred);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    usb_pipe_span_start_obj, 4, 4, usb_pipe_span_start_py);

/**
 * Publish a Flash state-machine descriptor or page-data fragment.
 * These records never contain a CMSIS-DAP packet. The USB Worker performs
 * all dependent SWD operations and publishes per-page results to RX.
 */
static mp_obj_t usb_pipe_flash_submit_py(size_t n, const mp_obj_t *args) {
    (void)n;
    mp_buffer_info_t info;
    mp_get_buffer_raise(args[2], &info, MP_BUFFER_READ);
    int type = mp_obj_get_int(args[3]);
    if (type != 2 && type != 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("Flash pipe record must be control(2) or data(3)"));
    }
    int transferred = transport_usb_pipe_submit_typed(
        mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
        info.buf, info.len, checked_timeout(args[4]), (uint8_t)type);
    if (transferred < 0) raise_native_error();
    return mp_obj_new_int(transferred);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    usb_pipe_flash_submit_obj, 5, 5, usb_pipe_flash_submit_py);

static mp_obj_t usb_pipe_take_py(size_t n, const mp_obj_t *args) {
    (void)n;
    int capacity = mp_obj_get_int(args[2]);
    if (capacity < 1 || capacity > 64) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB pipe response capacity must be 1..64"));
    }
    uint8_t bytes[64];
    int transferred = transport_usb_pipe_take(mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), bytes, (size_t)capacity, checked_timeout(args[3]));
    if (transferred < 0) raise_native_error();
    return mp_obj_new_bytes(bytes, transferred);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_pipe_take_obj, 4, 4, usb_pipe_take_py);

static mp_obj_t usb_pipe_reset_py(void) {
    transport_usb_pipe_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(usb_pipe_reset_obj, usb_pipe_reset_py);

static mp_obj_t usb_pipe_available_py(void) {
    return mp_obj_new_int(transport_usb_pipe_available());
}
static MP_DEFINE_CONST_FUN_OBJ_0(usb_pipe_available_obj, usb_pipe_available_py);

/** Most recent native RX slot publication time, for --timings only. */
static mp_obj_t usb_pipe_last_arrival_py(void) {
    return mp_obj_new_int_from_ull(transport_usb_pipe_last_arrival());
}
static MP_DEFINE_CONST_FUN_OBJ_0(usb_pipe_last_arrival_obj, usb_pipe_last_arrival_py);

static const mp_rom_map_elem_t wasm_transport_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_wasm_transport) },
    { MP_ROM_QSTR(MP_QSTR_serial_available), MP_ROM_PTR(&native_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_read), MP_ROM_PTR(&native_rx_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_readinto), MP_ROM_PTR(&native_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_write), MP_ROM_PTR(&native_tx_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_wait), MP_ROM_PTR(&native_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_out), MP_ROM_PTR(&usb_out_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_in), MP_ROM_PTR(&usb_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_out_in), MP_ROM_PTR(&usb_out_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_batch_exchange), MP_ROM_PTR(&usb_batch_exchange_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_stream_submit), MP_ROM_PTR(&usb_stream_submit_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_stream_take), MP_ROM_PTR(&usb_stream_take_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_stream_reset), MP_ROM_PTR(&usb_stream_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_submit), MP_ROM_PTR(&usb_pipe_submit_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_take), MP_ROM_PTR(&usb_pipe_take_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_span_start), MP_ROM_PTR(&usb_pipe_span_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_flash_submit), MP_ROM_PTR(&usb_pipe_flash_submit_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_reset), MP_ROM_PTR(&usb_pipe_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_available), MP_ROM_PTR(&usb_pipe_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_last_arrival), MP_ROM_PTR(&usb_pipe_last_arrival_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_dap_repeat), MP_ROM_PTR(&usb_dap_repeat_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_control_transfer), MP_ROM_PTR(&usb_control_transfer_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_write), MP_ROM_PTR(&hid_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_read), MP_ROM_PTR(&hid_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_batch_exchange), MP_ROM_PTR(&hid_batch_exchange_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_stream_submit), MP_ROM_PTR(&hid_stream_submit_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_stream_take), MP_ROM_PTR(&hid_stream_take_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_stream_reset), MP_ROM_PTR(&hid_stream_reset_obj) },
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
