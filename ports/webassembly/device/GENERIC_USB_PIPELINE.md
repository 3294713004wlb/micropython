# Universal USB pipeline ABI

The shared USB pipeline stores exactly 32 fixed-size records in each direction, each carrying up to 4096 payload bytes plus a 16-byte header. The two rings occupy 263,200 bytes in total. Layout is defined by `transport/protocol.json`, version 11; regenerate `protocol.h` and the Python Uploader `transport-protocol.js` together after changes.

The native `wasm_device.usb_pipe_submit(out_ep, in_ep, request, expected_reply_bytes, timeout_ms, send_zlp=False)` only stages a request and returns after the shared ring accepts it. `wasm_device.usb_pipe_take(out_ep, in_ep, capacity, timeout_ms)` waits for the next reply. Python controls device discovery, report framing, outstanding window, recovery, and retries.

The browser-side service uses two independent ordered asynchronous I/O lanes, but neither device nor protocol automatically recovers after a disconnect. Hardware-reported packet sizes and protocol window limits must be respected regardless of the larger host buffers.

For an example of standard Python usage and tests, see the matching Python Uploader package's `docs/generic-usb-hid-async-pipeline.md`.
