################################################################################
# Initial setup of Makefile environment.

# Select the variant to build for:
ifdef VARIANT_DIR
# Custom variant path - remove trailing slash and get the final component of
# the path as the variant name.
VARIANT ?= $(notdir $(VARIANT_DIR:/=))
else
# If not given on the command line, then default to standard.
VARIANT ?= standard
VARIANT_DIR ?= variants/$(VARIANT)
endif

ifeq ($(wildcard $(VARIANT_DIR)/.),)
$(error Invalid VARIANT specified: $(VARIANT_DIR))
endif

# If the build directory is not given, make it reflect the variant name.
BUILD ?= build-$(VARIANT)

# Register the browser J-Link native module without replacing other user modules.
# Setting JLINK_NATIVE=0 retains the original port build.
JLINK_NATIVE ?= 1
ifeq ($(JLINK_NATIVE),1)
override USER_C_MODULES += $(CURDIR)/usermods
endif

# Native CLI transports data synchronously through shared WASM memory.
# Asyncify is needed only to snapshot VM locals for a safe synchronous GC.
WASM_NATIVE_CLI_PROFILE ?= 1
WASM_GC_REGISTER_SCAN_ASYNCIFY ?= 1

include ../../py/mkenv.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

# Build FATFS and both LittleFS formats into every browser/Worker variant.
# extmod/extmod.mk translates these options into preprocessor definitions,
# pulls in lib/oofatfs and lib/littlefs sources, and registers VFS classes.
# Keep the Make and C feature switches in sync by setting them here, before
# including extmod.mk.  Each feature can be disabled with make NAME=0.
MICROPY_VFS_FAT ?= 1
MICROPY_VFS_LFS1 ?= 1
MICROPY_VFS_LFS2 ?= 1

# Use the default frozen manifest, variants may override this.
FROZEN_MANIFEST ?= variants/manifest.py

# Qstr definitions (must come before including py.mk).
QSTR_DEFS = qstrdefsport.h

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

################################################################################
# Project specific settings and compiler/linker flags.

CC = emcc
LD = emcc
INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(VARIANT_DIR)

CFLAGS += -std=c99 -Wall -Werror -Wdouble-promotion -Wfloat-conversion
CFLAGS += -Os -DNDEBUG
CFLAGS += $(INC)

# Native synchronous CLI: keep the VM on its Worker while device operations
# proceed independently through shared WASM memory. For browser asyncio apps,
# build with WASM_NATIVE_CLI_PROFILE=0 to opt back into cooperative yields.
WASM_NATIVE_CLI_PROFILE ?= 1
# Native Wasm SjLj avoids JS invoke_*/setThrew handlers during CLI imports.
# Use one consistent setjmp/longjmp mechanism for compilation and linking.
ifeq ($(WASM_NATIVE_CLI_PROFILE),1)
CFLAGS += -sSUPPORT_LONGJMP=wasm
JSFLAGS += -sSUPPORT_LONGJMP=wasm
else
CFLAGS += -sSUPPORT_LONGJMP=emscripten
JSFLAGS += -sSUPPORT_LONGJMP=emscripten
endif

# Diagnose unbalanced MicroPython NLR frames before they damage wasm memory.
WASM_NLR_GUARD ?= 1
CFLAGS += -DMICROPY_WASM_NLR_GUARD=$(WASM_NLR_GUARD)

ifeq ($(WASM_NATIVE_CLI_PROFILE),1)
CFLAGS += -DMICROPY_WASM_VM_AUTO_YIELD=0
CFLAGS += -DMICROPY_WASM_CLI_BUSY_DELAY=1
else
CFLAGS += -DMICROPY_WASM_VM_AUTO_YIELD=1
CFLAGS += -DMICROPY_WASM_CLI_BUSY_DELAY=0
endif

# Produce a function-index to function-name symbol map for matching the
# browser stack: wasm-function[521], wasm-function[539], etc.
# Keep this optional because symbols are not useful in production bundles.
WASM_DEBUG_SYMBOLS ?= 0
ifeq ($(WASM_DEBUG_SYMBOLS),1)
CFLAGS += -g3
JSFLAGS += --emit-symbol-map
endif

# Opt-in diagnostics for locating stack/null-pointer corruption.
WASM_DEBUG_MEMORY ?= 0
ifeq ($(WASM_DEBUG_MEMORY),1)
CFLAGS += -g3
JSFLAGS += -s STACK_OVERFLOW_CHECK=2
JSFLAGS += -s ASSERTIONS=2
JSFLAGS += -s SAFE_HEAP=1
endif

# Optional C-level null pointer diagnostics; do not enable in production
# Both compile and link stages need the UBSan runtime
WASM_DEBUG_NULL ?= 0
ifeq ($(WASM_DEBUG_NULL),1)
CFLAGS += -fsanitize=null -fno-sanitize-recover=null
LDFLAGS += -fsanitize=null
endif

# WASM native shared transport: C and JS operate the same linear memory.
CFLAGS += -matomics -mbulk-memory

# Enable Emscripten's zlib port at compilation and link time. This powers
# Python's zlib.compress() without browser-side firmware preprocessing.
CFLAGS += -sUSE_ZLIB=1
JSFLAGS += -sUSE_ZLIB=1

EXPORTED_FUNCTIONS_EXTRA += ,\
	_mp_js_do_exec,\
	_mp_js_do_exec_async,\
	_mp_js_get_main_dict,\
	_mp_js_native_cli_profile,\
	_mp_js_gc_register_scan_asyncify,\
	_mp_js_do_import,\
	_mp_js_register_js_module,\
	_mp_js_repl_init,\
	_mp_js_repl_process_char,\
	_mp_js_repl_process_char_async,\
	_proxy_c_free_obj,\
	_proxy_c_init,\
	_proxy_c_to_js_call,\
	_proxy_c_to_js_delete_attr,\
	_proxy_c_to_js_dir,\
	_proxy_c_to_js_get_iter,\
	_proxy_c_to_js_get_type_and_data,\
	_proxy_c_to_js_has_attr,\
	_proxy_c_to_js_iternext,\
	_proxy_c_to_js_lookup_attr,\
	_proxy_c_to_js_resume,\
	_proxy_c_to_js_store_attr,\
	_proxy_convert_mp_to_js_obj_cside

EXPORTED_RUNTIME_METHODS_EXTRA += ,\
	HEAPU8,\
	PATH,\
	PATH_FS,\
	UTF8ToString,\
	getValue,\
	lengthBytesUTF8,\
	setValue,\
	stringToUTF8

JSFLAGS += -s EXPORTED_FUNCTIONS="\
	_free,\
	_malloc,\
	_mp_js_init,\
	_mp_hal_get_interrupt_char,\
	_mp_sched_keyboard_interrupt,\
	_wasm_transport_region,\
	_wasm_transport_region_size,\
	_wasm_transport_region_count,\
	_wasm_transport_protocol_hash32$(EXPORTED_FUNCTIONS_EXTRA)"
JSFLAGS += -s EXPORTED_RUNTIME_METHODS="\
	ccall,\
	cwrap,\
	FS$(EXPORTED_RUNTIME_METHODS_EXTRA)"
JSFLAGS += --js-library library.js
JSFLAGS += -s MODULARIZE -s EXPORT_NAME=_createMicroPythonModule
# Browser main thread and dedicated Web Worker only; exclude Node.js / shell shims.
JSFLAGS += -s ENVIRONMENT=web,worker

# Grow the WASM linear memory when the MicroPython GC/C allocator needs it.
# These can be overridden on the make command line.  The maximum is an upper
# bound, not a reservation or a promise that the browser can allocate it.
WASM_INITIAL_MEMORY ?= 67108864
WASM_MAXIMUM_MEMORY ?= 2147483648
WASM_STACK_SIZE ?= 131072
WASM_MEMORY_GROWTH_STEP ?= 0.2
JSFLAGS += -s SHARED_MEMORY=1
JSFLAGS += -s IMPORTED_MEMORY=1
JSFLAGS += -s ALLOW_MEMORY_GROWTH=1
JSFLAGS += -s INITIAL_MEMORY=$(WASM_INITIAL_MEMORY)
JSFLAGS += -s MAXIMUM_MEMORY=$(WASM_MAXIMUM_MEMORY)
JSFLAGS += -s MEMORY_GROWTH_GEOMETRIC_STEP=$(WASM_MEMORY_GROWTH_STEP)
JSFLAGS += -s STACK_SIZE=$(WASM_STACK_SIZE)

################################################################################
# Source files and libraries.

SRC_SHARED = $(addprefix shared/,\
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

SRC_C += \
	modzlib_native.c \
	modhashlib_native.c \
	md5_core.c \
	transport/transport_runtime.c \
	transport/transport_terminal.c \
	transport/transport_serial.c \
	transport/transport_mailbox.c \
	transport/transport_usb_pipe.c \
	transport/transport_regions.c \
	device/device_host.c \
	device/device_serial.c \
	device/device_hid.c \
	device/device_usb.c \
	modwasm_device.c \
	fatfs_port.c \
	lexer_dedent.c \
	main.c \
	modjs.c \
	modjsffi.c \
	mphalport.c \
	objjsproxy.c \
	proxy_c.c \

# List of sources for qstr extraction.
SRC_QSTR += $(SRC_C) $(SRC_SHARED)

SRC_JS += \
	api.js \
	objpyproxy.js \
	proxy_js.js \

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

################################################################################
# Main targets.

.PHONY: all

all: $(BUILD)/micropython.mjs

$(BUILD)/micropython.mjs: $(OBJ) library.js $(SRC_JS)
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(OBJ) $(LIBS) $(JSFLAGS)
	$(Q)cat $(SRC_JS) >> $@

################################################################################
# Remaining make rules.

include $(TOP)/py/mkrules.mk
