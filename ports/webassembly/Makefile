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

EXPORTED_FUNCTIONS_EXTRA += ,\
	_mp_js_do_exec,\
	_mp_js_do_exec_async,\
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
	_mp_sched_keyboard_interrupt$(EXPORTED_FUNCTIONS_EXTRA)"
JSFLAGS += -s EXPORTED_RUNTIME_METHODS="\
	ccall,\
	cwrap,\
	FS$(EXPORTED_RUNTIME_METHODS_EXTRA)"
JSFLAGS += --js-library library.js
JSFLAGS += -s SUPPORT_LONGJMP=emscripten
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
	modwasm_transport.c \
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
