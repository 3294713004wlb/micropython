/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2022 Damien P. George
 * Copyright (c) 2017, 2018 Rami Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Options to control how MicroPython is built for this port, overriding
// defaults in py/mpconfig.h.

#include <stdint.h>
#include <stdlib.h> // for malloc/free in the WebAssembly port

// Variant-specific definitions.
#include "mpconfigvariant.h"

#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#endif

#define MICROPY_ALLOC_PATH_MAX      (256)
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)
#define MICROPY_READER_VFS          (MICROPY_VFS)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_ENABLE_PYSTACK      (1)
#define MICROPY_KBD_EXCEPTION       (1)
#define MICROPY_REPL_EVENT_DRIVEN   (1)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_ENABLE_DOC_STRING   (1)
#define MICROPY_WARNINGS            (1)
#define MICROPY_ERROR_PRINTER       (&mp_stderr_print)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_USE_INTERNAL_ERRNO  (1)
#define MICROPY_USE_INTERNAL_PRINTF (0)

#define MICROPY_EPOCH_IS_1970       (1)
#define MICROPY_PY_ASYNCIO_TASK_QUEUE_PUSH_CALLBACK (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_js_random_u32())
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS (1)
#define MICROPY_PY_TIME_INCLUDEFILE "ports/webassembly/modtime.c"
#ifndef MICROPY_VFS
#define MICROPY_VFS                 (1)
#endif
#define MICROPY_VFS_POSIX           (MICROPY_VFS)

// Keep the POSIX filesystem for Emscripten FS, while FATFS and LittleFS are
// registered by extmod.mk based on MICROPY_VFS_FAT/LFS1/LFS2 Make variables.
// FATFS requires GC finalisers; the full-featured variants already provide
// them, but set it explicitly so VFS support is not profile-dependent.
#define MICROPY_ENABLE_FINALISER   (1)

#if MICROPY_VFS_FAT
// Enable long FAT filenames, including paths inside ESP32 partition images.
// 2 allocates the LFN working buffer on the C stack, avoiding shared globals.
#define MICROPY_FATFS_ENABLE_LFN   (2)
// Accept either 512-byte or 4096-byte FAT sectors in a block-device image.
#define MICROPY_FATFS_MAX_SS      (4096)
// Support paths relative to the FAT volume working directory.
#define MICROPY_FATFS_RPATH       (2)
#endif
#define MICROPY_PY_SYS_PLATFORM     "webassembly"

#ifndef MICROPY_PY_JS
#define MICROPY_PY_JS (MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_EXTRA_FEATURES)
#endif

#ifndef MICROPY_PY_JSFFI
#define MICROPY_PY_JSFFI (MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_EXTRA_FEATURES)
#endif

// Whether the VM will periodically call mp_js_hook(), which checks for
// browser JS interrupt requests and VM event polling.
#ifndef MICROPY_VARIANT_ENABLE_JS_HOOK
#define MICROPY_VARIANT_ENABLE_JS_HOOK (0)
#endif

// The native CLI runs synchronously in a dedicated Worker. Hardware I/O and
// cancellation are handled via shared memory by another Worker/main thread.
// Automatically unwinding the entire VM stack every N bytecodes is unnecessary
// for CLI execution and can make large imports both slow and fragile.
// Keep a compile-time opt-in for applications that require VM cooperation.
#ifndef MICROPY_WASM_VM_AUTO_YIELD
#define MICROPY_WASM_VM_AUTO_YIELD (0)
#endif

#ifndef MICROPY_WASM_CLI_BUSY_DELAY
#define MICROPY_WASM_CLI_BUSY_DELAY (1)
#endif

#ifndef MICROPY_WASM_VM_YIELD_INTERVAL
#define MICROPY_WASM_VM_YIELD_INTERVAL (255)
#endif

#if MICROPY_VARIANT_ENABLE_JS_HOOK
#define MICROPY_VM_HOOK_COUNT (10)
#if MICROPY_WASM_VM_AUTO_YIELD
#define MICROPY_VM_HOOK_INIT \
    static uint vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
    static uint vm_yield_divisor = MICROPY_WASM_VM_YIELD_INTERVAL;
#define MICROPY_VM_HOOK_POLL do { \
    if (--vm_hook_divisor == 0) { \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
        extern void mp_hal_process_events(void); \
        mp_hal_process_events(); \
    } \
    if (--vm_yield_divisor == 0) { \
        vm_yield_divisor = MICROPY_WASM_VM_YIELD_INTERVAL; \
        extern void mp_wasm_vm_yield(void); \
        mp_wasm_vm_yield(); \
    } \
} while (0)
#else
// Poll for VM events without suspending, unwinding, or resuming Asyncify.
#define MICROPY_VM_HOOK_INIT \
    static uint vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL do { \
    if (--vm_hook_divisor == 0) { \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
        extern void mp_hal_process_events(void); \
        mp_hal_process_events(); \
    } \
} while (0)
#endif
#define MICROPY_VM_HOOK_LOOP MICROPY_VM_HOOK_POLL;
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_POLL;
#endif

// type definitions for the specific machine

#define MP_SSIZE_MAX (0x7fffffff)

typedef long mp_off_t;

#define MICROPY_HW_BOARD_NAME "JS"
#define MICROPY_HW_MCU_NAME "Emscripten"

#define MP_STATE_PORT MP_STATE_VM

#if MICROPY_VFS
// _GNU_SOURCE must be defined to get definitions of DT_xxx symbols from dirent.h.
#define _GNU_SOURCE
#endif

extern const struct _mp_print_t mp_stderr_print;

uint32_t mp_js_random_u32(void);
