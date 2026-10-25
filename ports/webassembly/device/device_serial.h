/** Browser-backed serial byte stream.  C API independent of MicroPython. */
#ifndef WASM_DEVICE_SERIAL_H
#define WASM_DEVICE_SERIAL_H
#include <stddef.h>
#include <stdint.h>

/* Negative result: transport failure, consult device_last_error().
 * Zero: no bytes available / no room, never implicit EOF.
 * These functions do not open/close the physical WebSerialPort; a host
 * adapter manages the browser's permission and device lifecycle.
 */
int device_serial_available(void);
int device_serial_read(uint8_t *destination, size_t capacity);
int device_serial_write(const uint8_t *source, size_t length);
int device_serial_wait(int timeout_ms);

#endif
