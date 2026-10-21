/** One stable ABI: dispatch exported regions to their owning channels */
#include "transport.h"
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE uintptr_t wasm_transport_region(int id) {
    uintptr_t address = transport_serial_region(id);
    if (!address) address = transport_mailbox_region(id);
    if (!address) address = transport_runtime_region(id);
    if (!address) address = transport_terminal_region(id);
    if (!address) address = transport_usb_pipe_region(id);
    return address;
}

EMSCRIPTEN_KEEPALIVE int wasm_transport_region_size(int id) {
    size_t size = transport_serial_region_size(id);
    if (!size) size = transport_mailbox_region_size(id);
    if (!size) size = transport_runtime_region_size(id);
    if (!size) size = transport_terminal_region_size(id);
    if (!size) size = transport_usb_pipe_region_size(id);
    return (int)size;
}

/** Number of descriptors exported by this transport version */
EMSCRIPTEN_KEEPALIVE int wasm_transport_region_count(void) {
    return TP_REGION_COUNT;
}

/** Short hash of the canonical protocol JSON schema */
EMSCRIPTEN_KEEPALIVE uint32_t wasm_transport_protocol_hash32(void) {
    return TP_PROTOCOL_HASH32;
}
