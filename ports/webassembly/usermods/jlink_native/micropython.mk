# MicroPython make user module. Absolute sources are remapped by py/py.mk.
JLINK_DIR := $(USERMOD_DIR)
JLINK_UPSTREAM := $(JLINK_DIR)/vendor/libjaylink/libjaylink

SRC_USERMOD_C += $(JLINK_DIR)/modjaylink.c
SRC_USERMOD_C += $(JLINK_DIR)/swd_engine.c
SRC_USERMOD_LIB_C += $(JLINK_DIR)/compat/libusb_mpy.c
CFLAGS_USERMOD += -I$(JLINK_DIR) -I$(JLINK_DIR)/compat

# The upstream tree is intentionally an external vendor checkout.
# The ZIP must still compile its transport module when it is absent.
ifneq ($(wildcard $(JLINK_UPSTREAM)/core.c),)
# Browser/WebUSB only: do not compile POSIX TCP, broadcast discovery or
# socket code. The upstream shared transport dispatch still refers to TCP,
# so provide explicit "unsupported" implementations in tcp_disabled.c.
JLINK_SOURCES := \
    buffer.c core.c device.c discovery.c \
    emucom.c error.c fileio.c jtag.c list.c log.c \
    strutil.c swd.c swo.c target.c transport.c \
    util.c version.c discovery_usb.c transport_usb.c

# Additional protocol files appeared in newer libjaylink releases.
# Include them only when they are available in the pinned upstream checkout.
JLINK_OPTIONAL_SOURCES := c2.c spi.c
JLINK_SOURCES += $(foreach source,$(JLINK_OPTIONAL_SOURCES),$(if $(wildcard $(JLINK_UPSTREAM)/$(source)),$(source)))

SRC_USERMOD_LIB_C += $(addprefix $(JLINK_UPSTREAM)/,$(JLINK_SOURCES))
SRC_USERMOD_LIB_C += $(JLINK_DIR)/compat/tcp_disabled.c
CFLAGS_USERMOD += -I$(JLINK_UPSTREAM) -DHAVE_LIBUSB=1 -DJLINK_LIBJAYLINK_AVAILABLE=1
else
CFLAGS_USERMOD += -DJLINK_LIBJAYLINK_AVAILABLE=0
endif
