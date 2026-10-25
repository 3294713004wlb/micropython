/** Synchronous, allocation-free C -> browser device metadata/control bridge.
 * This is deliberately a thin OS-like device-operation boundary. No tool or
 * protocol name is interpreted here. Only the already-authorized host can
 * access devices; opening the permission picker requires user activation.
 */
#include "device_host.h"
#include "transport/transport.h"
#include <limits.h>
#include <string.h>
#include <emscripten.h>

EM_JS(int, device_host_invoke_js,
    (const char *kind, const char *operation, const char *args_ptr,
     char *response, int capacity), {
    const k = UTF8ToString(kind);
    const op = UTF8ToString(operation);
    const a = UTF8ToString(args_ptr);
    let output;
    try {
        const call = globalThis.__wasmDeviceHostCall;
        if (typeof call !== 'function') {
            throw new Error('WASM device host is not installed');
        }
        output = String(call(k, op, a));
    } catch (error) {
        output = JSON.stringify({ok:false,error:{
            name:String(error?.name || 'Error'),
            message:String(error?.message || error),
            errno:null,status:null
        }});
    }
    const count = lengthBytesUTF8(output);
    if (count + 1 > capacity) return -2;
    stringToUTF8(output, response, capacity);
    return count;
});

int device_host_request(const char *kind, const char *operation,
    const char *arguments, char *response, size_t capacity) {
    if (!kind || !operation || !response || capacity == 0 ||
        capacity > INT_MAX || !*operation ||
        (strcmp(kind, "serial") && strcmp(kind, "hid") && strcmp(kind, "usb"))) {
        transport_set_error("Invalid device management operation");
        return -1;
    }
    response[0] = '\0';
    int result = device_host_invoke_js(kind, operation,
        arguments ? arguments : "{}", response, (int)capacity);
    if (result == -2) transport_set_error("Device operation response too large");
    else if (result < 0) transport_set_error("Device operation failed");
    return result;
}
