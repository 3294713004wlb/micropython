/** HID I/O is entirely native; the host handles only WebHID promises. */
#include "device_hid.h"
#include "transport/transport.h"
#include <limits.h>
#include <string.h>

#define HID_BATCH_OPCODE 6
#define STREAM_SUBMIT_OPCODE 7
#define STREAM_TAKE_OPCODE 8
#define STREAM_RESET_OPCODE 9

static int transfer(const char *id, int opcode, int a,
    const uint8_t *data, size_t size, uint8_t *dst, size_t capacity, int timeout) {
    if (!id || strlen(id) >= TP_REGION_SIZE_HID_ID ||
        size > TP_REGION_SIZE_HID_TX || capacity > TP_REGION_SIZE_HID_RX ||
        (size && !data) || (capacity && !dst) || size > INT_MAX || capacity > INT_MAX) {
        transport_set_error("Invalid HID id, report, or receive capacity");
        return -1;
    }
    return transport_transfer(TP_KIND_HID, opcode, id, a, 0,
        data, (int)size, dst, (int)capacity, timeout);
}

int device_hid_write(const char *id, const uint8_t *data, size_t size) {
    return transfer(id, TP_OPCODE_HID_WRITE, 0, data, size, NULL, 0, 0);
}
int device_hid_output(const char *id, const uint8_t *data, size_t size) {
    return transfer(id, TP_OPCODE_HID_OUTPUT, 0, data, size, NULL, 0, 0);
}
int device_hid_read(const char *id, uint8_t *dst, size_t capacity, int timeout_ms) {
    return transfer(id, TP_OPCODE_HID_READ, 0, NULL, 0, dst, capacity, timeout_ms);
}
int device_hid_feature_write(const char *id, const uint8_t *data, size_t size) {
    return transfer(id, TP_OPCODE_HID_FEATURE_WRITE, 0, data, size, NULL, 0, 0);
}
int device_hid_feature_read(const char *id, int report_id, uint8_t *dst, size_t capacity) {
    return transfer(id, TP_OPCODE_HID_FEATURE_READ, report_id, NULL, 0, dst, capacity, 0);
}
int device_hid_exchange_batch(const char *id, const uint8_t *data, size_t size,
    uint8_t *dst, size_t capacity, int timeout_ms) {
    return transfer(id, HID_BATCH_OPCODE, 0, data, size, dst, capacity, timeout_ms);
}
int device_hid_stream_submit(const char *id, const uint8_t *data, size_t size, int timeout_ms) {
    return transfer(id, STREAM_SUBMIT_OPCODE, 0, data, size, NULL, 0, timeout_ms);
}
int device_hid_stream_take(const char *id, uint8_t *dst, size_t capacity, int timeout_ms) {
    return transfer(id, STREAM_TAKE_OPCODE, 0, NULL, 0, dst, capacity, timeout_ms);
}
int device_hid_stream_reset(const char *id) {
    return transfer(id, STREAM_RESET_OPCODE, 0, NULL, 0, NULL, 0, 0);
}
