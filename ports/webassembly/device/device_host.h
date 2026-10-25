/** Generic browser-authorized device management API; independent of MicroPython. */
#ifndef WASM_DEVICE_HOST_H
#define WASM_DEVICE_HOST_H
#include <stddef.h>
/**
 * Issue one synchronous device operation through the owning I/O Worker.
 * kind: serial / hid / usb. operation: list_devices, open, close,
 * configure, and other raw lifecycle operations supported by the host.
 * arguments: JSON object (typically "{}"). response receives a JSON
 * envelope {ok:bool,value:...,error:...}. Never invokes a permission picker.
 * Returns payload bytes (excluding NUL), or negative for host/size errors.
 * The caller owns retry, discovery polling, and protocol recovery.
 */
int device_host_request(const char *kind, const char *operation,
    const char *arguments, char *response, size_t capacity);
#endif
