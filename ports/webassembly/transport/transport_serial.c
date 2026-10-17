/** Serial SPSC byte rings: C <-> browser without device-specific logic */
#include "transport.h"
#include <string.h>
#include <emscripten.h>
#include <emscripten/emscripten.h>
typedef struct {
    uint32_t read;
    uint32_t write;
    uint32_t reserved0;
    uint32_t reserved1;
} shared_ring_header_t;

typedef struct {
    shared_ring_header_t header;
    uint8_t data[TP_RING_SERIAL_CAPACITY];
} serial_ring_t;

static serial_ring_t serial_rx;
static serial_ring_t serial_tx;

typedef char serial_region_size_matches_protocol[
    sizeof(serial_ring_t) == TP_REGION_SIZE_SERIAL_RX ? 1 : -1
];

/* No Atomics.wait(), no Emscripten JS import and no allocations. */
static uint32_t ring_read(void *ring, uint32_t capacity, uint8_t *dst, uint32_t length) {
    shared_ring_header_t *header = (shared_ring_header_t *)ring;
    uint8_t *data = (uint8_t *)ring + sizeof(shared_ring_header_t);
    uint32_t read = __atomic_load_n(&header->read, __ATOMIC_RELAXED);
    uint32_t write = __atomic_load_n(&header->write, __ATOMIC_ACQUIRE);
    uint32_t available = write >= read ? write - read : capacity - read + write;
    uint32_t count = length < available ? length : available;
    if (count == 0) return 0;
    uint32_t first = count < capacity - read ? count : capacity - read;
    memcpy(dst, data + read, first);
    if (first < count) memcpy(dst + first, data, count - first);
    __atomic_store_n(&header->read, (read + count) % capacity, __ATOMIC_RELEASE);
#if defined(__wasm__)
    // Wake browser-side asynchronous backpressure waits, never sleep in C
    __builtin_wasm_memory_atomic_notify((int *)&header->read, 1);
#endif
    return count;
}

static uint32_t ring_write(void *ring, uint32_t capacity, const uint8_t *src, uint32_t length) {
    shared_ring_header_t *header = (shared_ring_header_t *)ring;
    uint8_t *data = (uint8_t *)ring + sizeof(shared_ring_header_t);
    uint32_t write = __atomic_load_n(&header->write, __ATOMIC_RELAXED);
    uint32_t read = __atomic_load_n(&header->read, __ATOMIC_ACQUIRE);
    uint32_t used = write >= read ? write - read : capacity - read + write;
    uint32_t space = capacity - 1 - used;
    uint32_t count = length < space ? length : space;
    if (count == 0) return 0;
    uint32_t first = count < capacity - write ? count : capacity - write;
    memcpy(data + write, src, first);
    if (first < count) memcpy(data, src + first, count - first);
    __atomic_store_n(&header->write, (write + count) % capacity, __ATOMIC_RELEASE);
#if defined(__wasm__)
    // Immediate wake-up: eliminates the previous 8/20 ms polling floor
    __builtin_wasm_memory_atomic_notify((int *)&header->write, 1);
#endif
    return count;
}

int transport_serial_available(void) {
    uint32_t read = __atomic_load_n(&serial_rx.header.read, __ATOMIC_RELAXED);
    uint32_t write = __atomic_load_n(&serial_rx.header.write, __ATOMIC_ACQUIRE);
    return write >= read ? write - read : TP_RING_SERIAL_CAPACITY - read + write;
}

int transport_serial_read(uint8_t *dst, int size) {
    transport_check_cancel();
    return (int)ring_read(&serial_rx, TP_RING_SERIAL_CAPACITY, dst, (uint32_t)size);
}

int transport_serial_write(const uint8_t *src, int size) {
    transport_check_cancel();
    return (int)ring_write(&serial_tx, TP_RING_SERIAL_CAPACITY, src, (uint32_t)size);
}

int transport_serial_wait(int ms) {
    transport_check_cancel();
    if (transport_serial_available() > 0) return 1;
    if (ms == 0) return 0;
    double deadline = ms < 0 ? 0 : emscripten_get_now() + ms;
    uint32_t spin = 0;
    for (;;) {
        transport_check_cancel();
        if (transport_serial_available() > 0) return 1;
        if (ms >= 0 && (++spin & 0x7f) == 0 && emscripten_get_now() >= deadline) return 0;
    }
}


uintptr_t transport_serial_region(int id) {
    switch (id) {
        case TP_REGION_SERIAL_RX: return (uintptr_t)&serial_rx;
        case TP_REGION_SERIAL_TX: return (uintptr_t)&serial_tx;
        default: return 0;
    }
}
size_t transport_serial_region_size(int id) {
    return id == TP_REGION_SERIAL_RX || id == TP_REGION_SERIAL_TX ?
        sizeof(serial_ring_t) : 0;
}
