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
uintptr_t transport_usb_pipe_region(int id);
size_t transport_usb_pipe_region_size(int id);
int transport_usb_pipe_reset(void);
int transport_usb_pipe_submit_typed(int ep_out, int ep_in, const uint8_t *frame, size_t length, int timeout_ms, uint8_t type, size_t expected_response_length, bool send_zlp);
int transport_usb_pipe_submit(int ep_out, int ep_in, const uint8_t *frame, size_t length, size_t response_length, int timeout_ms, bool send_zlp);
int transport_usb_pipe_take(int ep_out, int ep_in, uint8_t *output, size_t capacity, int timeout_ms);
int transport_usb_pipe_available(void);
uint64_t transport_usb_pipe_last_arrival(void);
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
int transport_usb_control_for(const char *id, int opcode, int bm, int req,
    int value, int index, const uint8_t *tx, int tx_len, uint8_t *rx,
    int rx_length, int timeout);
void transport_set_error(const char *message);
const char *transport_last_error(void);
#endif
