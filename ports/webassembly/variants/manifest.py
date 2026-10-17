# The asyncio package is built from the standard implementation but with the
# core scheduler replaced with a custom scheduler that uses the JavaScript
# runtime (with setTimeout an Promise's) to contrtol the scheduling.

package(
    "asyncio",
    (
        "event.py",
        "funcs.py",
        "lock.py",
    ),
    base_path="$(MPY_DIR)/extmod",
    opt=3,
)

package(
    "asyncio",
    (
        "__init__.py",
        "core.py",
    ),
    base_path="$(PORT_DIR)",
    opt=3,
)

# Lightweight browser backend for pylink-square (no ctypes/DLL import).
# The actual upstream pylink package remains an external Python library.
module("jlink_backend.py", base_path="$(PORT_DIR)/python/pylink_integration", opt=2)

# Public CPython platform module calls commonly used for backend selection.
# Frozen into the WASM port so third-party tools need no per-package shim.
module("platform.py", base_path="$(PORT_DIR)/python/compat310", opt=2)
