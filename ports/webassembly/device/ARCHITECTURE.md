# Generic browser device layer — v2

```
Python applications (pySerial / hidapi / PyUSB / pyOCD / nrfutil / other)
            | Python wrappers: protocol state and error/retry policy
            v
    wasm_device native module
            | C device_host.c: authorized enumerate/open/close/configure
            | C device_serial.c: RX/TX ring bytes
            | C device_hid.c: HID report/feature/batch/stream
            | C device_usb.c: control/bulk/batch/stream/pipe
            v
    C transport (shared buffers + binary mailbox)
            v
    Worker JS (only browser WebSerial/WebHID/WebUSB operations)
```

The WASM C functions are independent of Python objects so native C clients can
link against `device/*.h`. The device-host API uses a generic JSON envelope
for metadata only; binary packets are not represented as JSON or byte lists.

## Ownership

- Browser host owns the browser object, user-granted permission and physical I/O.
- Native C owns the ABI and transfers. No hardware protocol interpretation is
  required to use standard operations.
- Python owns device choice, validation, retry/timeouts, recovery and business
  protocol; **no automatic reconnect** is performed by the generic layer.
- CLI finalization closes open devices; authorization persists in the browser.

## Hardware/transport capability limits

1. The standard USB control/Bulk packet mailbox (protocol v9) carries its
   own device ID, so distinct PyUSB devices can issue ordinary operations
   without depending on the global selected device. The separate high-speed pipe has *not* acquired multi-device routing: it is still tied
   to one preselected probe per job. Do not claim multi-device high-speed
   pipeline capability.
2. The high-throughput USB pipe still includes pyOCD-inspired DAP
   macro/span extensions in the browser service. Raw bulk, control, batch,
   and opaque stream operations are application-independent; these specialized
   optimizations are available as optional accelerated features and have not yet been moved
   entirely to the C layer. They are not part of the generic device contract.
3. Browser Web Serial does not provide a POSIX fd; browser permissions are
   granted on explicit user activation and cannot be requested from C alone.
4. Emscripten compilation and physical WebHID/WebUSB/WebSerial unplug/replug
   testing must be performed in the target browser environment.
