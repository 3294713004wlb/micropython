/** Native transport public C API; no Python object dependency in data channels */
#ifndef MICROPY_WASM_NATIVE_TRANSPORT_H
#define MICROPY_WASM_NATIVE_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

bool wasm_transport_cancelled(void);
void transport_check_cancel(void);
uintptr_t transport_runtime_region(int id);
size_t transport_runtime_region_size(int id);
uintptr_t transport_serial_region(int id);
size_t transport_serial_region_size(int id);
uintptr_t transport_mailbox_region(int id);
size_t transport_mailbox_region_size(int id);
uintptr_t transport_terminal_region(int id);
size_t transport_terminal_region_size(int id);

size_t wasm_transport_terminal_write(int stream, const char *data, size_t length);
int transport_serial_available(void);
int transport_serial_read(uint8_t *dst, int size);
int transport_serial_write(const uint8_t *src, int size);
int transport_serial_wait(int ms);
int transport_transfer(int protocol, int opcode, const char *id,
    int param_a, int param_b, const uint8_t *tx, int tx_len,
    uint8_t *rx, int rx_length, int timeout);
int transport_usb_control(int opcode, int bm, int req, int value, int index,
    const uint8_t *tx, int tx_len, uint8_t *rx, int rx_length, int timeout);
const char *transport_last_error(void);
#endif
