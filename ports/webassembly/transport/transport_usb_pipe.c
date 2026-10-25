/**
 * Generic USB dual SPSC report rings.
 *
 * CLI Worker is the sole TX producer / RX consumer. Transport Worker is
 * the sole TX consumer / RX producer. Fixed 4112-byte cells avoid wrap-frame
 * copies, torn variable-length headers and allocations in the C fast path.
 * This is separate from the USB mailbox used by device control and
 * native DAP-repeat compatibility operations.
 */
#include "transport.h"
#include <string.h>
#include <emscripten.h>
#include <emscripten/emscripten.h>

#define PIPE_DEPTH TP_USB_PIPE_DEPTH
#define PIPE_MASK (PIPE_DEPTH - 1u)
#define PIPE_SLOT_BYTES TP_USB_PIPE_CELL_BYTES
#define PIPE_PACKET_BYTES TP_USB_PIPE_PAYLOAD_BYTES

typedef struct {
    uint32_t read;
    uint32_t write;
    uint32_t error;
    uint32_t generation;
    uint8_t cells[PIPE_DEPTH][PIPE_SLOT_BYTES];
} pipe_region_t;

static pipe_region_t tx_pipe __attribute__((aligned(16)));
static pipe_region_t rx_pipe __attribute__((aligned(16)));
static uint64_t last_rx_arrival_ms;

typedef char pipe_tx_size_check[
    sizeof(pipe_region_t) == TP_REGION_SIZE_USB_PIPE_TX ? 1 : -1
];
typedef char pipe_rx_size_check[
    sizeof(pipe_region_t) == TP_REGION_SIZE_USB_PIPE_RX ? 1 : -1
];

static uint32_t load32(const uint32_t *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void publish32(uint32_t *p, uint32_t value) {
    __atomic_store_n(p, value, __ATOMIC_RELEASE);
#if defined(__wasm__)
    __builtin_wasm_memory_atomic_notify((int *)p, 1);
#endif
}

static void wait32(uint32_t *p, uint32_t expected) {
#if defined(__wasm__)
    __builtin_wasm_memory_atomic_wait32((int *)p, (int)expected, 4000000LL);
#else
    (void)p;
    (void)expected;
#endif
}

static int timed_out(double deadline) {
    return deadline > 0 && emscripten_get_now() >= deadline;
}

int transport_usb_pipe_reset(void) {
    /* Caller must first execute the existing STREAM_RESET mailbox barrier. */
    for (unsigned i = 0; i < 2; ++i) {
        pipe_region_t *pipe = i ? &rx_pipe : &tx_pipe;
        publish32(&pipe->read, 0);
        publish32(&pipe->write, 0);
        publish32(&pipe->error, 0);
        publish32(&pipe->generation, load32(&pipe->generation) + 1u);
    }
    return 0;
}

int transport_usb_pipe_submit_typed(int ep_out, int ep_in,
    const uint8_t *frame, size_t length, int timeout_ms, uint8_t type,
    size_t expected_response_length, bool send_zlp) {
    if ((type > 3) || !frame || length < 1 || length > PIPE_PACKET_BYTES ||
        expected_response_length < 1 || expected_response_length > PIPE_PACKET_BYTES ||
        ep_out < 1 || ep_out > 15 || ep_in < 1 || ep_in > 15) {
        transport_set_error("Invalid USB TX report or endpoint");
        return -1;
    }
    double deadline = emscripten_get_now() + (timeout_ms > 0 ? timeout_ms : 10000);
    for (;;) {
        transport_check_cancel();
        if (load32(&tx_pipe.error) || load32(&rx_pipe.error)) {
            transport_set_error("USB stream failed; reconnect probe");
            return -1;
        }
        uint32_t read = load32(&tx_pipe.read);
        uint32_t write = load32(&tx_pipe.write);
        if ((uint32_t)(write - read) < PIPE_DEPTH) {
            uint8_t *cell = tx_pipe.cells[write & PIPE_MASK];
            cell[0] = (uint8_t)length;
            cell[1] = (uint8_t)(length >> 8);
            cell[2] = (uint8_t)ep_out;
            cell[3] = (uint8_t)ep_in;
            cell[4] = type;
            cell[5] = send_zlp ? 1 : 0;
            cell[6] = (uint8_t)expected_response_length;
            cell[7] = (uint8_t)(expected_response_length >> 8);
            memcpy(cell + 16, frame, length);
            /* The Worker observes the cursor only after bytes are published. */
            publish32(&tx_pipe.write, write + 1u);
            return (int)length;
        }
        if (timed_out(deadline)) {
            transport_set_error("USB pipe stall/timeout");
            return -1;
        }
        wait32(&tx_pipe.read, read);
    }
}

int transport_usb_pipe_submit(int ep_out, int ep_in,
    const uint8_t *frame, size_t length, size_t response_length, int timeout_ms,
    bool send_zlp) {
    return transport_usb_pipe_submit_typed(ep_out, ep_in, frame, length, timeout_ms,
        0, response_length, send_zlp);
}

int transport_usb_pipe_take(int ep_out, int ep_in,
    uint8_t *output, size_t capacity, int timeout_ms) {
    if (!output || capacity < 1 || ep_out < 1 || ep_out > 15 ||
        ep_in < 1 || ep_in > 15) {
        transport_set_error("Invalid USB RX report or endpoint");
        return -1;
    }
    double deadline = emscripten_get_now() + (timeout_ms > 0 ? timeout_ms : 10000);
    for (;;) {
        transport_check_cancel();
        if (load32(&tx_pipe.error) || load32(&rx_pipe.error)) {
            transport_set_error("USB stream failed; reconnect probe");
            return -1;
        }
        uint32_t read = load32(&rx_pipe.read);
        uint32_t write = load32(&rx_pipe.write);
        if (read != write) {
            uint8_t *cell = rx_pipe.cells[read & PIPE_MASK];
            size_t length = (size_t)cell[0] | ((size_t)cell[1] << 8);
            if (length < 1 || length > PIPE_PACKET_BYTES || length > capacity ||
                cell[2] != (uint8_t)ep_out || cell[3] != (uint8_t)ep_in ||
                cell[4] != 0) {
                transport_set_error("USB pipe response corrupt or endpoint mismatch");
                return -1;
            }
            memcpy(output, cell + 16, length);
            last_rx_arrival_ms = 0;
            for (unsigned i = 0; i < 8; ++i) {
                last_rx_arrival_ms |= (uint64_t)cell[8 + i] << (i * 8);
            }
            publish32(&rx_pipe.read, read + 1u);
            return (int)length;
        }
        if (timed_out(deadline)) {
            transport_set_error("USB pipe stall/timeout");
            return -1;
        }
        wait32(&rx_pipe.write, write);
    }
}

int transport_usb_pipe_available(void) {
    if (load32(&tx_pipe.error) || load32(&rx_pipe.error)) return -1;
    return (int)(load32(&rx_pipe.write) - load32(&rx_pipe.read));
}

uintptr_t transport_usb_pipe_region(int id) {
    if (id == TP_REGION_USB_PIPE_TX) return (uintptr_t)&tx_pipe;
    if (id == TP_REGION_USB_PIPE_RX) return (uintptr_t)&rx_pipe;
    return 0;
}

size_t transport_usb_pipe_region_size(int id) {
    if (id == TP_REGION_USB_PIPE_TX) return sizeof(tx_pipe);
    if (id == TP_REGION_USB_PIPE_RX) return sizeof(rx_pipe);
    return 0;
}

uint64_t transport_usb_pipe_last_arrival(void) {
    return last_rx_arrival_ms;
}
