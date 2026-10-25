/** Raw WebHID report transport usable by any WASM C module. */
#ifndef WASM_DEVICE_HID_H
#define WASM_DEVICE_HID_H
#include <stddef.h>
#include <stdint.h>

/* Device id corresponds to the browser-authorized HID device key.
 * Report format (including report-id handling) stays at the caller boundary.
 */
int device_hid_write(const char *id, const uint8_t *data, size_t size);
int device_hid_output(const char *id, const uint8_t *data, size_t size);
int device_hid_read(const char *id, uint8_t *dst, size_t capacity, int timeout_ms);
int device_hid_feature_write(const char *id, const uint8_t *data, size_t size);
int device_hid_feature_read(const char *id, int report_id, uint8_t *dst, size_t capacity);
int device_hid_exchange_batch(const char *id, const uint8_t *data, size_t size,
    uint8_t *dst, size_t capacity, int timeout_ms);
int device_hid_stream_submit(const char *id, const uint8_t *data, size_t size, int timeout_ms);
int device_hid_stream_take(const char *id, uint8_t *dst, size_t capacity, int timeout_ms);
int device_hid_stream_reset(const char *id);
#endif
