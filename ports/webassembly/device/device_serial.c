/** Zero-copy native boundary for the SPSC serial TX/RX rings. */
#include "device_serial.h"
#include "transport/transport.h"
#include <limits.h>

static int valid_length(size_t n) {
    if (n <= INT_MAX) return 1;
    transport_set_error("Serial buffer length exceeds INT_MAX");
    return 0;
}

int device_serial_available(void) {
    return transport_serial_available();
}

int device_serial_read(uint8_t *destination, size_t capacity) {
    if ((!destination && capacity) || !valid_length(capacity)) {
        transport_set_error("Invalid Serial receive buffer");
        return -1;
    }
    if (!capacity) return 0;
    return transport_serial_read(destination, (int)capacity);
}

int device_serial_write(const uint8_t *source, size_t length) {
    if ((!source && length) || !valid_length(length)) {
        transport_set_error("Invalid Serial transmit buffer");
        return -1;
    }
    if (!length) return 0;
    return transport_serial_write(source, (int)length);
}

int device_serial_wait(int timeout_ms) {
    return transport_serial_wait(timeout_ms);
}
