/*
 * Host-only native mailbox regression test
 *
 * Build with the stub headers and pthread; no Emscripten required
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../transport/transport_runtime.c"
#include "../../transport/transport_terminal.c"
#include "../../transport/transport_serial.c"
#include "../../transport/transport_mailbox.c"
#include "../../transport/transport_regions.c"
#include "../../modwasm_device.c"

int mp_type_KeyboardInterrupt = 0;
void mp_raise_type(void *exception) {
    (void)exception;
    abort();
}

static void *complete_usb(void *unused) {
    (void)unused;
    while (__atomic_load_n(&usb_state[0], __ATOMIC_ACQUIRE) != TP_STATUS_PENDING) {
        /* Simulate the asynchronous browser device operation */
    }
    assert(usb_state[2] == 3);
    assert(usb_state[3] == 2);
    assert(usb_state[4] == 1);
    assert(usb_state[5] == 4);
    assert(usb_state[6] == 3);
    assert(memcmp(usb_tx, "PING", 4) == 0);

    int32_t expected = TP_STATUS_PENDING;
    assert(__atomic_compare_exchange_n(&usb_state[0], &expected,
        TP_STATUS_COMMITTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    memcpy(usb_rx, "ACK", 3);
    __atomic_store_n(&usb_state[8], 3, __ATOMIC_RELAXED);
    __atomic_store_n(&usb_state[0], TP_STATUS_DONE, __ATOMIC_RELEASE);
    return NULL;
}

int main(void) {
    /* C writes terminal output directly into shared static memory. */
    assert(wasm_transport_region_size(12) == sizeof(native_terminal_queue_t));
    assert(wasm_transport_terminal_write(1, "Hello", 5) == 5);
    assert(terminal_output.write == 5);
    for (int i = 0; i < 5; i++) {
        assert(terminal_output.data[i] == ((1 << 8) | (int)"Hello"[i]));
    }
    terminal_output.read = 5;
    assert(wasm_transport_terminal_write(2, "!", 1) == 1);
    assert(terminal_output.data[5] == ((2 << 8) | (int)'!'));
    terminal_output.read = 6;
    assert(usb_state[14] == TP_MAILBOX_MAGIC);
    assert(usb_state[15] == TP_PROTOCOL_VERSION);
    assert(hid_state[14] == TP_MAILBOX_MAGIC);
    assert(wasm_transport_region_size(4) == TP_REGION_SIZE_HID_TX);
    assert(wasm_transport_region_size(5) == TP_REGION_SIZE_HID_TX);

    pthread_t thread;
    assert(pthread_create(&thread, NULL, complete_usb, NULL) == 0);
    uint8_t rx[4] = {0};
    int count = transport_transfer(1, 3, "", 2, 1,
        (const uint8_t *)"PING", 4, rx, 3, 1000);
    assert(pthread_join(thread, NULL) == 0);
    assert(count == 3);
    assert(memcmp(rx, "ACK", 3) == 0);
    assert(usb_state[0] == TP_STATUS_IDLE);
    assert(usb_state[1] == 1);
    /* USB/HID are raw packet slots, with no ring headers or cursors. */
    assert(wasm_transport_region_size(2) == TP_REGION_SIZE_USB_TX);
    assert(wasm_transport_region_size(3) == TP_REGION_SIZE_USB_TX);
    assert(transport_transfer(2, 1, "hid1", 0, 0,
        (const uint8_t *)"x", TP_REGION_SIZE_HID_TX + 1,
        NULL, 0, 1) == -1);
    puts("NATIVE_MAILBOX_C_OK");
    return 0;
}
