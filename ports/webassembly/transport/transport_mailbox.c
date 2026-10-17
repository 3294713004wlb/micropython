/** USB/HID single-flight packet mailboxes and atomic response ownership */
#include "transport.h"
#include <string.h>
#include <emscripten.h>
#include <emscripten/emscripten.h>
static uint8_t usb_tx[TP_REGION_SIZE_USB_TX] __attribute__((aligned(16)));
static uint8_t usb_rx[TP_REGION_SIZE_USB_RX] __attribute__((aligned(16)));
static uint8_t hid_tx[TP_REGION_SIZE_HID_TX] __attribute__((aligned(16)));
static uint8_t hid_rx[TP_REGION_SIZE_HID_RX] __attribute__((aligned(16)));
static int32_t usb_state[TP_MAILBOX_WORDS] __attribute__((aligned(16))) = {
    [TP_FIELD_MAGIC] = TP_MAILBOX_MAGIC,
    [TP_FIELD_VERSION] = TP_PROTOCOL_VERSION,
};
static int32_t hid_state[TP_MAILBOX_WORDS] __attribute__((aligned(16))) = {
    [TP_FIELD_MAGIC] = TP_MAILBOX_MAGIC,
    [TP_FIELD_VERSION] = TP_PROTOCOL_VERSION,
};
static uint8_t usb_error[TP_REGION_SIZE_USB_ERROR];
static uint8_t hid_error[TP_REGION_SIZE_HID_ERROR];
static uint8_t hid_id[TP_REGION_SIZE_HID_ID];
static char last_error[256];

typedef char mailbox_descriptor_size_matches_protocol[
    sizeof(usb_state) == TP_REGION_SIZE_USB_STATE &&
    sizeof(hid_state) == TP_REGION_SIZE_HID_STATE ? 1 : -1
];
static int32_t status_load(int32_t *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void status_store(int32_t *p, int32_t value) {
    __atomic_store_n(p, value, __ATOMIC_RELEASE);
#if defined(__wasm__)
    __builtin_wasm_memory_atomic_notify((int *)p, 1);
#endif
}

/*
 * Execute one packet transaction using the static WASM memory as a mailbox.
 * The browser may process WebUSB/HID asynchronously, but only one VM caller
 * publishes a command per protocol at a time.  This removes four ring cursor
 * updates and all shared-ring copies from the USB/HID fast path.
 *
 * The browser must CAS PENDING->COMMITTING before writing the result slot.
 * This excludes C timeouts from recycling memory during a browser-side copy.
 */
static int transport_transfer_inner(int protocol, int opcode, const char *id,
    int param_a, int param_b, const uint8_t *tx, int tx_len,
    uint8_t *rx, int rx_length, int timeout,
    int bm, int req, int value, int index) {
    transport_check_cancel();
    last_error[0] = '\0';
    int32_t *state = protocol == TP_KIND_USB ? usb_state : hid_state;
    uint8_t *out = protocol == TP_KIND_USB ? usb_tx : hid_tx;
    uint8_t *in = protocol == TP_KIND_USB ? usb_rx : hid_rx;
    int capacity = protocol == TP_KIND_USB ? TP_REGION_SIZE_USB_TX : TP_REGION_SIZE_HID_TX;
    uint32_t id_len = protocol == TP_KIND_USB ? 0 : (uint32_t)strlen(id);

    if (tx_len < 0 || tx_len > capacity ||
        rx_length < 0 || rx_length > capacity ||
        id_len >= TP_REGION_SIZE_HID_ID) {
        return -1;
    }

    /* A zero USB timeout gets a finite safety deadline.  HID's negative
     * timeout is a conventional indefinitely-blocking input-report read. */
    int wait_ms = timeout > 0 ?
        (timeout > 2147482647 ? 2147483647 : timeout + 1000) :
        (protocol == TP_KIND_HID && opcode == 2 && timeout < 0 ? -1 : 30000);
    double deadline = wait_ms < 0 ? 0 : emscripten_get_now() + wait_ms;
    uint32_t spin = 0;

    while (status_load(&state[TP_FIELD_STATUS]) != TP_STATUS_IDLE) {
        transport_check_cancel();
        if (deadline > 0 && (++spin & 0xff) == 0 &&
            emscripten_get_now() >= deadline) {
            return -1;
        }
    }

    /* All arguments and bytes are written before the release-store below.
     * The slot belongs to C until the pending state is published. */
    if (tx_len) {
        memcpy(out, tx, (size_t)tx_len);
    }
    int32_t seq = __atomic_add_fetch(&state[TP_FIELD_SEQUENCE], 1, __ATOMIC_ACQ_REL);
    state[TP_FIELD_OPCODE] = opcode;
    if (protocol == TP_KIND_USB) {
        state[TP_FIELD_PARAM_A] = param_a & 15;
        state[TP_FIELD_PARAM_B] = param_b & 15;
        state[TP_FIELD_BM_REQUEST_TYPE] = bm & 255;
        state[TP_FIELD_B_REQUEST] = req & 255;
        state[TP_FIELD_W_VALUE] = value & 65535;
        state[TP_FIELD_W_INDEX] = index & 65535;
    } else {
        memcpy(hid_id, id, id_len);
        hid_id[id_len] = '\0';
        state[TP_FIELD_PARAM_A] = param_a & 255;
        state[TP_FIELD_PARAM_B] = (int32_t)id_len;
    }
    state[TP_FIELD_TX_LENGTH] = tx_len;
    state[TP_FIELD_RX_LENGTH] = rx_length;
    state[TP_FIELD_TIMEOUT_MS] = timeout;
    state[TP_FIELD_RESULT_LENGTH] = 0;
    state[TP_FIELD_ERROR_LENGTH] = 0;
    status_store(&state[TP_FIELD_STATUS], TP_STATUS_PENDING);

    spin = 0;
    for (;;) {
        int32_t status = status_load(&state[TP_FIELD_STATUS]);
        if (status != TP_STATUS_PENDING && status != TP_STATUS_COMMITTING) {
            break;
        }

        /* Only recycle the mailbox if the device side has not claimed it.
         * Once COMMITTING, the JS copy runs without yielding and is short. */
        bool cancelled = wasm_transport_cancelled();
        bool expired = deadline > 0 && (++spin & 0xff) == 0 &&
            emscripten_get_now() >= deadline;
        if ((cancelled || expired) && status == TP_STATUS_PENDING) {
            int32_t pending = TP_STATUS_PENDING;
            if (__atomic_compare_exchange_n(&state[TP_FIELD_STATUS], &pending,
                TP_STATUS_IDLE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                if (cancelled) {
                    transport_check_cancel();
                }
                return -1;
            }
        }
    }

    int status = status_load(&state[TP_FIELD_STATUS]);
    if (wasm_transport_cancelled()) {
        status_store(&state[TP_FIELD_STATUS], TP_STATUS_IDLE);
        transport_check_cancel();
    }
    if (status != TP_STATUS_DONE || status_load(&state[TP_FIELD_SEQUENCE]) != seq) {
        if (status == TP_STATUS_ERROR) {
            const char *details = (const char *)(protocol == TP_KIND_USB ? usb_error : hid_error);
            size_t n = (size_t)status_load(&state[TP_FIELD_ERROR_LENGTH]);
            if (n > sizeof(last_error) - 1) n = sizeof(last_error) - 1;
            memcpy(last_error, details, n);
            last_error[n] = '\0';
        }
        status_store(&state[TP_FIELD_STATUS], TP_STATUS_IDLE);
        return -1;
    }

    int count = status_load(&state[TP_FIELD_RESULT_LENGTH]);
    if (count < 0 || (rx_length > 0 && count > rx_length)) {
        status_store(&state[TP_FIELD_STATUS], TP_STATUS_IDLE);
        return -1;
    }
    if (rx_length > 0 && count > 0) {
        memcpy(rx, in, (size_t)count);
    }
    status_store(&state[TP_FIELD_STATUS], TP_STATUS_IDLE);
    return count;
}

int transport_transfer(int protocol, int opcode, const char *id,
    int param_a, int param_b, const uint8_t *tx, int tx_len,
    uint8_t *rx, int rx_length, int timeout) {
    return transport_transfer_inner(protocol, opcode, id, param_a, param_b, tx,
        tx_len, rx, rx_length, timeout, 0, 0, 0, 0);
}

int transport_usb_control(int opcode, int bm, int req, int value, int index,
    const uint8_t *tx, int tx_len, uint8_t *rx, int rx_length, int timeout) {
    return transport_transfer_inner(TP_KIND_USB, opcode, "", 0, 0, tx, tx_len, rx,
        rx_length, timeout, bm, req, value, index);
}


const char *transport_last_error(void) {
    return last_error;
}
uintptr_t transport_mailbox_region(int id) {
    switch (id) {
        case TP_REGION_USB_TX: return (uintptr_t)usb_tx;
        case TP_REGION_USB_RX: return (uintptr_t)usb_rx;
        case TP_REGION_HID_TX: return (uintptr_t)hid_tx;
        case TP_REGION_HID_RX: return (uintptr_t)hid_rx;
        case TP_REGION_USB_STATE: return (uintptr_t)usb_state;
        case TP_REGION_HID_STATE: return (uintptr_t)hid_state;
        case TP_REGION_USB_ERROR: return (uintptr_t)usb_error;
        case TP_REGION_HID_ERROR: return (uintptr_t)hid_error;
        case TP_REGION_HID_ID: return (uintptr_t)hid_id;
        default: return 0;
    }
}
size_t transport_mailbox_region_size(int id) {
    switch (id) {
        case TP_REGION_USB_TX: return sizeof(usb_tx);
        case TP_REGION_USB_RX: return sizeof(usb_rx);
        case TP_REGION_HID_TX: return sizeof(hid_tx);
        case TP_REGION_HID_RX: return sizeof(hid_rx);
        case TP_REGION_USB_STATE: return sizeof(usb_state);
        case TP_REGION_HID_STATE: return sizeof(hid_state);
        case TP_REGION_USB_ERROR: return sizeof(usb_error);
        case TP_REGION_HID_ERROR: return sizeof(hid_error);
        case TP_REGION_HID_ID: return sizeof(hid_id);
        default: return 0;
    }
}
