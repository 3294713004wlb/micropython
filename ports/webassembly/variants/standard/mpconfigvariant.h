// Preserve the latest standard variant's browser/Worker JS polling hook.
#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)

// The GC grows by adding regions (malloc); WebAssembly linear memory is
// independently allowed to grow in Makefile.
#define MICROPY_GC_SPLIT_HEAP (1)
#define MICROPY_GC_SPLIT_HEAP_AUTO (1)

// Enable the broadest MicroPython built-in feature profile.  This does not
// make the runtime fully CPython-compatible.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_FULL_FEATURES)
