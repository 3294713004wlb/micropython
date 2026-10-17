// Preserve the latest standard variant's browser/Worker JS polling hook.
#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)

// Use MicroPython's default single-area, synchronous tracing collector.
// Do not enable MICROPY_GC_SPLIT_HEAP or MICROPY_GC_SPLIT_HEAP_AUTO:
// the auto-split mode defers gc_collect() until the outermost JS call,
// which cannot happen while a synchronous CLI runs in the Worker.
// Emscripten linear memory may still grow for native allocations and FS.
// The Python GC heap itself is the fixed size supplied to mp_js_init().

// Enable the broadest MicroPython built-in feature profile.  This does not
// make the runtime fully CPython-compatible.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_FULL_FEATURES)
