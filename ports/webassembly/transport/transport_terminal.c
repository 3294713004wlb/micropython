/** VM stdout/stderr stream-tagged byte queue */
#include "transport.h"
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
/* Single-producer terminal queue (encoded stream/byte words).
 * Header: read, write, reserved, closed. Data: (stream << 8) | byte.
 * The main thread is the only reader; the Python VM is the only writer.
 */
#define TERMINAL_SLOTS TP_TERMINAL_CAPACITY_WORDS
typedef struct {
    int32_t read;
    int32_t write;
    int32_t reserved;
    int32_t closed;
    int32_t data[TERMINAL_SLOTS];
} native_terminal_queue_t;
static native_terminal_queue_t terminal_output __attribute__((aligned(16)));

typedef char terminal_region_size_matches_protocol[
    sizeof(native_terminal_queue_t) == TP_REGION_SIZE_TERMINAL_OUTPUT ? 1 : -1
];

/**
 * Write MicroPython standard output without entering Emscripten JavaScript.
 * Check cancellation while waiting for main-thread backpressure.
 *
 * @param stream 1 for stdout, 2 for stderr
 * @param data Byte buffer to publish
 * @param length Byte count
 * @return Number of bytes written
 */
size_t wasm_transport_terminal_write(int stream, const char *data, size_t length) {
    size_t written = 0;
    while (written < length) {
        if (wasm_transport_cancelled() ||
            __atomic_load_n(&terminal_output.closed, __ATOMIC_ACQUIRE)) {
            break;
        }
        int32_t read = __atomic_load_n(&terminal_output.read, __ATOMIC_ACQUIRE);
        int32_t write = __atomic_load_n(&terminal_output.write, __ATOMIC_RELAXED);
        int32_t next = write + 1 == TERMINAL_SLOTS ? 0 : write + 1;
        if (next == read) {
            continue;
        }
        terminal_output.data[write] = ((stream & TP_TERMINAL_BYTE_MASK) << TP_TERMINAL_STREAM_SHIFT) |
            (uint8_t)data[written];
        __atomic_store_n(&terminal_output.write, next, __ATOMIC_RELEASE);
        written++;
    }
#if defined(__wasm__)
    __builtin_wasm_memory_atomic_notify((int *)&terminal_output.write, 1);
#endif
    return written;
}



uintptr_t transport_terminal_region(int id) {
    return id == TP_REGION_TERMINAL_OUTPUT ? (uintptr_t)&terminal_output : 0;
}
size_t transport_terminal_region_size(int id) {
    return id == TP_REGION_TERMINAL_OUTPUT ? sizeof(terminal_output) : 0;
}
