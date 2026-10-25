# Native WASM Serial / HID / USB device layer

This directory introduces three independent **C interfaces** which are usable
from any C module built into the webassembly port, not just from MicroPython.

| Header | Operations | Implementation |
| --- | --- | --- |
| `device_host.h` | authorized device list/open/close/configuration via generic C host-call interface | `device_host.c` |
| `device_serial.h` | RX/TX rings, available, wait | `device_serial.c` |
| `device_hid.h` | input/output reports, feature reports, batched and streamed data | `device_hid.c` |
| `device_usb.h` | per-device bulk/control, batch, generic typed stream records, typed accelerated pipe | `device_usb.c` |

Include the required header and call the C API directly:

```c
#include "device/device_usb.h"
uint8_t command[64] = {0};
uint8_t response[64];
int n = device_usb_out_in_for("/dev/web-usb1", 1, 1, command, sizeof(command),
    response, sizeof(response), 1000);
if (n < 0) {
    /* The caller decides whether to retry, reopen, or exit. */
}
```

## Layer boundaries

```
Python pySerial / hidapi / PyUSB / other native WASM clients
           |                     |
       wasm_device          device_*.h
           |                     |
           +----- device_*.c ----+
                        |
             transport/transport_*.c
                        |
           SharedArrayBuffer / native mailboxes
                        |
            Browser worker Web APIs (JS)
```

**All payload transmission and reception used by this API is performed in
C**, including mailbox packing, raw reports and shared ring access. The
JavaScript host remains mandatory for device authorization, opening/closing,
endpoint selection and browser-managed USB/HID/Serial calls: the Web APIs
cannot execute inside a C-only WASM module. A single generic device-host dispatcher makes browser operations available to
C, while Python wrappers no longer directly import the Serial/HID/USB JS
bridges. They do not replay DFU transactions,
automatically reconnect, or restart Python.

`wasm_device` is the **only** registered MicroPython device module. The
old implicit-device USB Bulk/Control calls are removed; use per-device APIs. There
is no `wasm_transport` alias or JSFFI device backend. `serial.list_devices()`,
`hid.list_devices()` / `hid.enumerate()`, and `usb.core.list_devices()` /
`usb.core.find()` all enumerate currently authorized devices through the C host.

Important limitations:

* C transfer functions do not claim USB/HID devices or prompt for permissions.
  These operations remain the responsibility of the browser host and Python
  device wrapper. Device list matching and reconnect timeout belong to Python.
* A successful USB write does not guarantee that the device processed a
  command. A reconnect does not implicitly retry a protocol transaction.
* The serial rings are one logical selected port per CLI Worker, preserving
  the existing transport ABI. They do not provide concurrent independent
  serial ports. The browser device registry maps active IDs to actual devices.
* A USB/HID mailbox transaction is single-flight per protocol. The USB pipe is
  separate and must be explicitly reset before reused after a protocol fault.
* Native functions return a negative number on failure, with details available
  from `transport_last_error()`; the MicroPython wrapper raises `OSError`.
* The package must be **recompiled** to add the native `wasm_device` module.

**Protocol v9 is required.** USB ordinary bulk/control mailbox requests now
carry an explicit device ID in shared region 15. Install the paired uploader
source and rebuild both WebAssembly assets and the browser application; do not
mix incompatible JS and WASM transport versions. The selected-device high-speed pipe, specialized DAP macro extensions and
isochronous browser transfers are distinct capabilities, not implicit USB
control/bulk compatibility paths. High-speed transfers still operate on one
selected device at a time. See ARCHITECTURE.md.
