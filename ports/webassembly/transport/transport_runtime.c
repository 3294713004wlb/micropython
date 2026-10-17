/** CLI cancellation primitive, checked without JS callbacks */
#include "transport.h"
#include "py/runtime.h"
static int32_t cancel_flag __attribute__((aligned(4)));

bool wasm_transport_cancelled(void) {
    return __atomic_load_n(&cancel_flag, __ATOMIC_ACQUIRE) != 0;
}

void transport_check_cancel(void) {
    if (wasm_transport_cancelled()) {
        mp_raise_type(&mp_type_KeyboardInterrupt);
    }
}

uintptr_t transport_runtime_region(int id) {
    return id == TP_REGION_CANCEL ? (uintptr_t)&cancel_flag : 0;
}
size_t transport_runtime_region_size(int id) {
    return id == TP_REGION_CANCEL ? sizeof(cancel_flag) : 0;
}
