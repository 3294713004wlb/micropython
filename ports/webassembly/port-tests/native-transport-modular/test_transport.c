/* Validate independent transport translation units and shared region exports */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transport/transport.h"

extern uintptr_t wasm_transport_region(int id);
extern int wasm_transport_region_size(int id);
extern int wasm_transport_region_count(void);
extern uint32_t wasm_transport_protocol_hash32(void);

/* The host test uses a stub MicroPython interrupt handler */
int mp_type_KeyboardInterrupt = 0;
void mp_raise_type(void *ignored) {
    (void)ignored;
    abort();
}

static void *usb_peer(void *unused) {
    (void)unused;
    int32_t *state = (int32_t *)wasm_transport_region(TP_REGION_USB_STATE);
    while (__atomic_load_n(&state[TP_FIELD_STATUS], __ATOMIC_ACQUIRE) !=
        TP_STATUS_PENDING) {
    }
    const uint8_t *tx = (const uint8_t *)wasm_transport_region(TP_REGION_USB_TX);
    uint8_t *rx = (uint8_t *)wasm_transport_region(TP_REGION_USB_RX);
    assert(state[TP_FIELD_OPCODE] == TP_OPCODE_USB_OUT_IN);
    assert(state[TP_FIELD_TX_LENGTH] == 4);
    assert(memcmp(tx, "PING", 4) == 0);
    int32_t expected = TP_STATUS_PENDING;
    assert(__atomic_compare_exchange_n(&state[TP_FIELD_STATUS], &expected,
        TP_STATUS_COMMITTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    memcpy(rx, "ACK", 3);
    state[TP_FIELD_RESULT_LENGTH] = 3;
    __atomic_store_n(&state[TP_FIELD_STATUS], TP_STATUS_DONE, __ATOMIC_RELEASE);
    return NULL;
}

int main(void) {
    assert(wasm_transport_region_count() == TP_REGION_COUNT);
    assert(wasm_transport_protocol_hash32() == TP_PROTOCOL_HASH32);
    for (int id = 0; id < TP_REGION_COUNT; ++id) {
        assert(wasm_transport_region(id) != 0);
        assert(wasm_transport_region_size(id) > 0);
    }
    assert(wasm_transport_region_size(TP_REGION_SERIAL_RX) ==
        TP_REGION_SIZE_SERIAL_RX);
    assert(wasm_transport_region_size(TP_REGION_TERMINAL_OUTPUT) ==
        TP_REGION_SIZE_TERMINAL_OUTPUT);
    uint8_t *rx = (uint8_t *)wasm_transport_region(TP_REGION_SERIAL_RX);
    uint32_t *header = (uint32_t *)rx;
    memcpy(rx + TP_RING_HEADER_BYTES, "TEST", 4);
    __atomic_store_n(&header[TP_RING_WRITE], 4, __ATOMIC_RELEASE);
    uint8_t output[8] = {0};
    assert(transport_serial_available() == 4);
    assert(transport_serial_read(output, 4) == 4);
    assert(memcmp(output, "TEST", 4) == 0);
    assert(wasm_transport_terminal_write(TP_TERMINAL_STDOUT, "Hi", 2) == 2);
    int32_t *term = (int32_t *)wasm_transport_region(TP_REGION_TERMINAL_OUTPUT);
    assert(term[TP_TERMINAL_HEADER_WORDS] ==
        ((TP_TERMINAL_STDOUT << TP_TERMINAL_STREAM_SHIFT) | 'H'));
    pthread_t thread;
    assert(pthread_create(&thread, NULL, usb_peer, NULL) == 0);
    uint8_t packet[4] = {0};
    assert(transport_transfer(TP_KIND_USB, TP_OPCODE_USB_OUT_IN,
        "", 2, 1, (const uint8_t *)"PING", 4, packet, 3, 1000) == 3);
    assert(pthread_join(thread, NULL) == 0);
    assert(memcmp(packet, "ACK", 3) == 0);
    puts("MODULAR_NATIVE_TRANSPORT_OK");
    return 0;
}
