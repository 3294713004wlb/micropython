/** WebUSB native transfer API for MicroPython and non-Python WASM code. */
#ifndef WASM_DEVICE_USB_H
#define WASM_DEVICE_USB_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* The *_for APIs are truly per-device and thread-safe at the mailbox level:
 * each binary request snapshots its own browser-authorized virtual device id.
 * No ordinary USB control/bulk operation has an implicit default target.
 */
int device_usb_out_for(const char *id, int ep_out, const uint8_t *data,
    size_t length, int timeout_ms);
int device_usb_in_for(const char *id, int ep_in, uint8_t *out,
    size_t capacity, int timeout_ms);
int device_usb_out_in_for(const char *id, int ep_out, int ep_in,
    const uint8_t *data, size_t length, uint8_t *out, size_t capacity, int timeout_ms);
int device_usb_control_for(const char *id, int request_type, int request,
    int value, int index, const uint8_t *data, size_t length,
    uint8_t *out, size_t capacity, int timeout_ms);

/* Stream and pipe functions are separate asynchronous/batched operations.
 * The selected active USB device is explicit in the Python CLI worker.
 */
int device_usb_stream_submit(int ep_out, int ep_in,
    const uint8_t *data, size_t length, int timeout_ms);
int device_usb_stream_take(int ep_out, int ep_in,
    uint8_t *out, size_t capacity, int timeout_ms);
int device_usb_stream_reset(int ep_out, int ep_in);
int device_usb_repeat(int ep_out, int ep_in, int writing,
    const uint8_t *descriptor, size_t descriptor_len,
    uint8_t *out, size_t capacity, int timeout_ms);
/* Generic USB ordered, asynchronous submit/take pipeline. */
int device_usb_pipe_submit(int ep_out, int ep_in,
    const uint8_t *data, size_t length, size_t response_length, int timeout_ms, bool send_zlp);
int device_usb_pipe_submit_typed(int ep_out, int ep_in,
    const uint8_t *data, size_t length, int timeout_ms, uint8_t type, size_t expected_response_length, bool send_zlp);
int device_usb_pipe_take(int ep_out, int ep_in, uint8_t *out, size_t capacity, int timeout_ms);
int device_usb_pipe_reset(void);
int device_usb_pipe_available(void);
uint64_t device_usb_pipe_last_arrival(void);
#endif
