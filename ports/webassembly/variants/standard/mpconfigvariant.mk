# With the normal MicroPython GC, emscripten_scan_registers() must be able
# to spill all WebAssembly locals. Emscripten requires Asyncify for this API.
# This does NOT change the native CLI's SharedArrayBuffer device I/O model:
# runPython() awaits the GC-safe Wasm entrypoint while Web Serial/USB/HID
# remain synchronous in C and use the shared-memory transport.
# Keep a separate explicit switch, because the CLI I/O profile and the
# GC register scanning requirement are independent concerns.
ifeq ($(WASM_NATIVE_CLI_PROFILE),1)
ifeq ($(WASM_GC_REGISTER_SCAN_ASYNCIFY),1)
JSFLAGS += -sASYNCIFY=1
JSFLAGS += -sASYNCIFY_STACK_SIZE=262144
CFLAGS += -DMICROPY_WASM_GC_REGISTER_SCAN_ASYNCIFY=1
else
$(error Synchronous GC requires WebAssembly register scanning: set WASM_GC_REGISTER_SCAN_ASYNCIFY=1 or use an alternate GC implementation)
endif
else
JSFLAGS += -sASYNCIFY=1
JSFLAGS += -sASYNCIFY_STACK_SIZE=262144
CFLAGS += -DMICROPY_WASM_GC_REGISTER_SCAN_ASYNCIFY=1
endif

JSFLAGS += -s EXIT_RUNTIME=0
