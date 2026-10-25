/**
 * MicroPython wasm_device Python bindings
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
#include "device/device_serial.h"
#include "device/device_host.h"
#include "device/device_hid.h"
#include "device/device_usb.h"

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
    int n = device_serial_available();
    if (n < 0) raise_native_error();
    return MP_OBJ_NEW_SMALL_INT(n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_available_obj, native_available_py);

static mp_obj_t native_rx_py(mp_obj_t length) {
    size_t n = checked_length(length);
    if (n == 0) return mp_obj_new_bytes((const byte *)"", 0);
    uint8_t *buffer = m_new(uint8_t, n);
    int count = device_serial_read(buffer, n);
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
    int count = device_serial_read(info.buf, (int)info.len);
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
    int n = device_serial_write(info.buf, info.len);
    if (n < 0) raise_native_error();
    return mp_obj_new_int(n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_tx_obj, native_tx_py);

static mp_obj_t native_wait_py(mp_obj_t timeout) {
    int result = device_serial_wait(mp_obj_get_int(timeout));
    if (result < 0) raise_native_error();
    return mp_obj_new_bool(result != 0);
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
    int count = -1;
    if (protocol == TP_KIND_HID) {
        switch (opcode) {
            case TP_OPCODE_HID_WRITE: count = device_hid_write(id, info.buf, info.len); break;
            case TP_OPCODE_HID_OUTPUT: count = device_hid_output(id, info.buf, info.len); break;
            case TP_OPCODE_HID_READ: count = device_hid_read(id, rx, rx_length, checked_timeout(timeout)); break;
            case TP_OPCODE_HID_FEATURE_WRITE: count = device_hid_feature_write(id, info.buf, info.len); break;
            case TP_OPCODE_HID_FEATURE_READ: count = device_hid_feature_read(id, paramA, rx, rx_length); break;
            default: mp_raise_ValueError(MP_ERROR_TEXT("Invalid HID opcode"));
        }
    }
    if (count < 0 || (return_bytes && (size_t)count > rx_length)) {
        m_del(uint8_t, rx, rx_length ? rx_length : 1);
        raise_native_error();
    }
    mp_obj_t result = return_bytes ? mp_obj_new_bytes(rx, count) : mp_obj_new_int(count);
    m_del(uint8_t, rx, rx_length ? rx_length : 1);
    return result;
}

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

/* v9 mailbox includes a USB device id for every normal control/bulk I/O. */
static mp_obj_t usb_device_io(size_t n, const mp_obj_t *args) {
    const char *id = mp_obj_str_get_str(args[0]);
    if (!id[0]) mp_raise_ValueError(MP_ERROR_TEXT("USB device id must not be empty"));
    mp_buffer_info_t info = {.buf = NULL, .len = 0};
    int op = mp_obj_get_int(args[1]);
    int ep_out = mp_obj_get_int(args[2]);
    int ep_in = mp_obj_get_int(args[3]);
    bool incoming = op == TP_OPCODE_USB_IN || op == TP_OPCODE_USB_OUT_IN;
    if (op == TP_OPCODE_USB_OUT || op == TP_OPCODE_USB_OUT_IN) {
        mp_get_buffer_raise(args[4], &info, MP_BUFFER_READ);
    }
    size_t rx_size = incoming ? checked_length(args[5]) : 0;
    if (info.len > TP_REGION_SIZE_USB_TX || rx_size > TP_REGION_SIZE_USB_RX)
        mp_raise_ValueError(MP_ERROR_TEXT("USB transfer too large"));
    uint8_t *rx = m_new(uint8_t, rx_size ? rx_size : 1);
    int timeout = mp_obj_get_int(args[6]);
    int count = -1;
    if (op == TP_OPCODE_USB_OUT) count = device_usb_out_for(id, ep_out, info.buf, info.len, timeout);
    if (op == TP_OPCODE_USB_IN) count = device_usb_in_for(id, ep_in, rx, rx_size, timeout);
    if (op == TP_OPCODE_USB_OUT_IN) count = device_usb_out_in_for(id, ep_out, ep_in,
        info.buf, info.len, rx, rx_size, timeout);
    if (count < 0 || (incoming && (size_t)count > rx_size)) {
        m_del(uint8_t, rx, rx_size ? rx_size : 1);
        raise_native_error();
    }
    mp_obj_t result = incoming ? mp_obj_new_bytes(rx, (size_t)count) : mp_obj_new_int(count);
    m_del(uint8_t, rx, rx_size ? rx_size : 1);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_device_io_obj, 7, 7, usb_device_io);

static mp_obj_t usb_device_control(size_t n, const mp_obj_t *args) {
    (void)n;
    const char *id = mp_obj_str_get_str(args[0]);
    int bm = mp_obj_get_int(args[1]) & 0xff;
    int req = mp_obj_get_int(args[2]) & 0xff;
    int value = mp_obj_get_int(args[3]) & 0xffff;
    int index = mp_obj_get_int(args[4]) & 0xffff;
    bool incoming = (bm & 0x80) != 0;
    mp_buffer_info_t input = {.buf = NULL, .len = 0};
    size_t rx_size = 0;
    if (incoming) rx_size = checked_length(args[5]);
    else if (args[5] != mp_const_none) mp_get_buffer_raise(args[5], &input, MP_BUFFER_READ);
    if (input.len > TP_REGION_SIZE_USB_TX || rx_size > TP_REGION_SIZE_USB_RX)
        mp_raise_ValueError(MP_ERROR_TEXT("USB control length too large"));
    uint8_t *rx = m_new(uint8_t, rx_size ? rx_size : 1);
    int count = device_usb_control_for(id, bm, req, value, index,
        input.buf, input.len, rx, rx_size, mp_obj_get_int(args[6]));
    if (count < 0 || (incoming && (size_t)count > rx_size)) {
        m_del(uint8_t, rx, rx_size ? rx_size : 1);
        raise_native_error();
    }
    mp_obj_t result = incoming ? mp_obj_new_bytes(rx, (size_t)count) : mp_obj_new_int(count);
    m_del(uint8_t, rx, rx_size ? rx_size : 1);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_device_control_obj, 7, 7, usb_device_control);

/*
 * An ordered batch is one native mailbox transaction. This avoids requiring
 * Python threads or concurrently publishing multiple mailbox descriptors.
 *
 * Wire format: uint16 count, then repeated (uint16 length, bytes). The high
 * bit of a USB length requests a ZLP after that frame without an extra reply.
 * The returned frames use the same format without flags.
 */
static mp_obj_t exchange_batch(const char *id,
    mp_obj_t packed, mp_obj_t max_length, mp_obj_t timeout) {
    mp_buffer_info_t input;
    mp_get_buffer_raise(packed, &input, MP_BUFFER_READ);
    size_t limit = TP_REGION_SIZE_HID_TX;
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
    int result = device_hid_exchange_batch(id, input.buf, input.len,
        rx, response_capacity, checked_timeout(timeout));
    if (result < 0 || (size_t)result > response_capacity) {
        m_del(uint8_t, rx, response_capacity);
        raise_native_error();
    }
    mp_obj_t output = mp_obj_new_bytes(rx, (size_t)result);
    m_del(uint8_t, rx, response_capacity);
    return output;
}

static mp_obj_t hid_batch_exchange_py(size_t n, const mp_obj_t *args) {
    (void)n;
    return exchange_batch(mp_obj_str_get_str(args[0]),
        args[1], args[2], args[3]);
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
    int result = -1;
    if (kind == TP_KIND_HID) {
        switch (opcode) {
            case TP_OPCODE_STREAM_SUBMIT: result = device_hid_stream_submit(id, input.buf, input.len, checked_timeout(timeout)); break;
            case TP_OPCODE_STREAM_TAKE: result = device_hid_stream_take(id, output, output_size, checked_timeout(timeout)); break;
            case TP_OPCODE_STREAM_RESET: result = device_hid_stream_reset(id); break;
        }
    } else if (kind == TP_KIND_USB) {
        switch (opcode) {
            case TP_OPCODE_STREAM_SUBMIT: result = device_usb_stream_submit(ep_out, ep_in, input.buf, input.len, checked_timeout(timeout)); break;
            case TP_OPCODE_STREAM_TAKE: result = device_usb_stream_take(ep_out, ep_in, output, output_size, checked_timeout(timeout)); break;
            case TP_OPCODE_STREAM_RESET: result = device_usb_stream_reset(ep_out, ep_in); break;
            case TP_OPCODE_USB_REPEAT_WRITE: result = device_usb_repeat(ep_out, ep_in, 1, input.buf, input.len, NULL, 0, checked_timeout(timeout)); break;
            case TP_OPCODE_USB_REPEAT_READ: result = device_usb_repeat(ep_out, ep_in, 0, input.buf, input.len, output, output_size, checked_timeout(timeout)); break;
        }
    }
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
    int expected = mp_obj_get_int(args[3]);
    if (expected < 1 || expected > 4096) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB pipeline response capacity must be 1..4096"));
    }
    int transferred = device_usb_pipe_submit(mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), info.buf, info.len,
        (size_t)expected, checked_timeout(args[4]),
        n == 6 && mp_obj_get_int(args[5]) != 0);
    if (transferred < 0) raise_native_error();
    return mp_obj_new_int(transferred);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_pipe_submit_obj, 5, 6, usb_pipe_submit_py);

/* Generic typed records: transport does not interpret the record content.
 * Record kinds are chosen by the Python client, not by the device subsystem.
 */
static mp_obj_t usb_pipe_submit_record_py(size_t n, const mp_obj_t *args) {
    (void)n;
    mp_buffer_info_t info;
    mp_get_buffer_raise(args[2], &info, MP_BUFFER_READ);
    int kind = mp_obj_get_int(args[3]);
    if (kind < 0 || kind > 255) mp_raise_ValueError(MP_ERROR_TEXT("record type out of range"));
    int transferred = device_usb_pipe_submit_typed(mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), info.buf, info.len, checked_timeout(args[4]), (uint8_t)kind, 64, false);
    if (transferred < 0) raise_native_error();
    return mp_obj_new_int(transferred);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    usb_pipe_submit_record_obj, 5, 5, usb_pipe_submit_record_py);

static mp_obj_t usb_pipe_take_py(size_t n, const mp_obj_t *args) {
    (void)n;
    int capacity = mp_obj_get_int(args[2]);
    if (capacity < 1 || capacity > 4096) {
        mp_raise_ValueError(MP_ERROR_TEXT("USB pipe response capacity must be 1..4096"));
    }
    uint8_t *bytes = m_new(uint8_t, capacity);
    int transferred = device_usb_pipe_take(mp_obj_get_int(args[0]),
        mp_obj_get_int(args[1]), bytes, (size_t)capacity, checked_timeout(args[3]));
    if (transferred < 0) {
        m_del(uint8_t, bytes, capacity);
        raise_native_error();
    }
    mp_obj_t result = mp_obj_new_bytes(bytes, transferred);
    m_del(uint8_t, bytes, capacity);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usb_pipe_take_obj, 4, 4, usb_pipe_take_py);

static mp_obj_t usb_pipe_reset_py(void) {
    device_usb_pipe_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(usb_pipe_reset_obj, usb_pipe_reset_py);

static mp_obj_t usb_pipe_available_py(void) {
    return mp_obj_new_int(device_usb_pipe_available());
}
static MP_DEFINE_CONST_FUN_OBJ_0(usb_pipe_available_obj, usb_pipe_available_py);

/** Most recent native RX slot publication time, for --timings only. */
static mp_obj_t usb_pipe_last_arrival_py(void) {
    return mp_obj_new_int_from_ull(device_usb_pipe_last_arrival());
}
static MP_DEFINE_CONST_FUN_OBJ_0(usb_pipe_last_arrival_obj, usb_pipe_last_arrival_py);

/* Generic device operations are implemented by C rather than Python JSFFI.
 * Responses are JSON envelopes so Python adapters may preserve their own
 * public exception classes without embedding application logic in the host.
 */
static mp_obj_t native_device_request_py(size_t n, const mp_obj_t *args) {
    (void)n;
    const char *kind = mp_obj_str_get_str(args[0]);
    const char *operation = mp_obj_str_get_str(args[1]);
    const char *arguments = args[2] == mp_const_none ? "{}" : mp_obj_str_get_str(args[2]);
    /* Dynamic result on MicroPython heap: no static shared output race. */
    size_t capacity = 65536;
    char *result = m_new(char, capacity);
    int count = device_host_request(kind, operation, arguments, result, capacity);
    if (count < 0) {
        m_del(char, result, capacity);
        raise_native_error();
    }
    mp_obj_t value = mp_obj_new_str(result, (size_t)count);
    m_del(char, result, capacity);
    return value;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_device_request_obj, 3, 3, native_device_request_py);

static const mp_rom_map_elem_t wasm_device_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_wasm_device) },
    { MP_ROM_QSTR(MP_QSTR_host_call), MP_ROM_PTR(&native_device_request_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_device_io), MP_ROM_PTR(&usb_device_io_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_device_control), MP_ROM_PTR(&usb_device_control_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_available), MP_ROM_PTR(&native_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_read), MP_ROM_PTR(&native_rx_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_readinto), MP_ROM_PTR(&native_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_write), MP_ROM_PTR(&native_tx_obj) },
    { MP_ROM_QSTR(MP_QSTR_serial_wait), MP_ROM_PTR(&native_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_stream_submit), MP_ROM_PTR(&usb_stream_submit_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_stream_take), MP_ROM_PTR(&usb_stream_take_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_stream_reset), MP_ROM_PTR(&usb_stream_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_submit), MP_ROM_PTR(&usb_pipe_submit_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_submit_record), MP_ROM_PTR(&usb_pipe_submit_record_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_take), MP_ROM_PTR(&usb_pipe_take_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_reset), MP_ROM_PTR(&usb_pipe_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_available), MP_ROM_PTR(&usb_pipe_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_pipe_last_arrival), MP_ROM_PTR(&usb_pipe_last_arrival_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_dap_repeat), MP_ROM_PTR(&usb_dap_repeat_obj) },
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
static MP_DEFINE_CONST_DICT(wasm_device_globals, wasm_device_globals_table);
const mp_obj_module_t wasm_device_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wasm_device_globals,
};
MP_REGISTER_MODULE(MP_QSTR_wasm_device, wasm_device_module);
