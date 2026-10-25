/** WebUSB native mailbox and CMSIS-DAP pipelined transport. */
#include "device_usb.h"
#include "transport/transport.h"
#include <limits.h>

#define USB_BATCH_OPCODE 6
#define STREAM_SUBMIT_OPCODE 7
#define STREAM_TAKE_OPCODE 8
#define STREAM_RESET_OPCODE 9

static int valid(const uint8_t *data, size_t length,
    const uint8_t *out, size_t capacity) {
    if (length > (size_t)INT_MAX || capacity > (size_t)INT_MAX ||
        length > TP_REGION_SIZE_USB_TX || capacity > TP_REGION_SIZE_USB_RX ||
        (length && !data) || (capacity && !out)) {
        transport_set_error("Invalid USB transfer buffers");
        return 0;
    }
    return 1;
}
static int transfer(int opcode, int ep_out, int ep_in,
    const uint8_t *data, size_t length,
    uint8_t *out, size_t capacity, int timeout_ms) {
    if (!valid(data, length, out, capacity)) return -1;
    return transport_transfer(TP_KIND_USB, opcode, "", ep_out, ep_in,
        data, (int)length, out, (int)capacity, timeout_ms);
}
static int transfer_for(const char *id, int opcode, int ep_out, int ep_in,
    const uint8_t *data, size_t length,
    uint8_t *out, size_t capacity, int timeout_ms) {
    if (!id || !valid(data, length, out, capacity)) return -1;
    return transport_transfer(TP_KIND_USB, opcode, id, ep_out, ep_in,
        data, (int)length, out, (int)capacity, timeout_ms);
}
int device_usb_out_for(const char *id, int ep_out, const uint8_t *data,
    size_t length, int timeout_ms) {
    return transfer_for(id, TP_OPCODE_USB_OUT, ep_out, 0, data, length, NULL, 0, timeout_ms);
}
int device_usb_in_for(const char *id, int ep_in, uint8_t *out,
    size_t capacity, int timeout_ms) {
    return transfer_for(id, TP_OPCODE_USB_IN, 0, ep_in, NULL, 0, out, capacity, timeout_ms);
}
int device_usb_out_in_for(const char *id, int ep_out, int ep_in,
    const uint8_t *data, size_t length, uint8_t *out, size_t capacity, int timeout_ms) {
    return transfer_for(id, TP_OPCODE_USB_OUT_IN, ep_out, ep_in, data, length,
        out, capacity, timeout_ms);
}
int device_usb_control_for(const char *id, int request_type, int request,
    int value, int index, const uint8_t *data, size_t length,
    uint8_t *out, size_t capacity, int timeout_ms) {
    if (!id || !valid(data, length, out, capacity)) return -1;
    return transport_usb_control_for(id,
        (request_type & 128) ? TP_OPCODE_USB_CONTROL_IN : TP_OPCODE_USB_CONTROL_OUT,
        request_type, request, value, index, data, (int)length,
        out, (int)capacity, timeout_ms);
}
int device_usb_stream_submit(int ep_out, int ep_in,
    const uint8_t *data, size_t length, int timeout_ms) {
    return transfer(STREAM_SUBMIT_OPCODE, ep_out, ep_in, data, length, NULL, 0, timeout_ms);
}
int device_usb_stream_take(int ep_out, int ep_in,
    uint8_t *out, size_t capacity, int timeout_ms) {
    return transfer(STREAM_TAKE_OPCODE, ep_out, ep_in, NULL, 0, out, capacity, timeout_ms);
}
int device_usb_stream_reset(int ep_out, int ep_in) {
    return transfer(STREAM_RESET_OPCODE, ep_out, ep_in, NULL, 0, NULL, 0, 0);
}
int device_usb_repeat(int ep_out, int ep_in, int writing,
    const uint8_t *descriptor, size_t descriptor_len, uint8_t *out,
    size_t capacity, int timeout_ms) {
    return transfer(writing ? TP_OPCODE_USB_REPEAT_WRITE : TP_OPCODE_USB_REPEAT_READ,
        ep_out, ep_in, descriptor, descriptor_len, out, capacity, timeout_ms);
}
int device_usb_pipe_submit(int ep_out, int ep_in,
    const uint8_t *data, size_t length, size_t response_length, int timeout_ms, bool send_zlp) {
    return transport_usb_pipe_submit(ep_out, ep_in, data, length, response_length, timeout_ms, send_zlp);
}
int device_usb_pipe_submit_typed(int ep_out, int ep_in,
    const uint8_t *data, size_t length, int timeout_ms, uint8_t type,
    size_t expected_response_length, bool send_zlp) {
    return transport_usb_pipe_submit_typed(ep_out, ep_in, data, length, timeout_ms,
        type, expected_response_length, send_zlp);
}
int device_usb_pipe_take(int ep_out, int ep_in, uint8_t *out, size_t capacity, int timeout_ms) {
    return transport_usb_pipe_take(ep_out, ep_in, out, capacity, timeout_ms);
}
int device_usb_pipe_reset(void) { return transport_usb_pipe_reset(); }
int device_usb_pipe_available(void) { return transport_usb_pipe_available(); }
uint64_t device_usb_pipe_last_arrival(void) { return transport_usb_pipe_last_arrival(); }
