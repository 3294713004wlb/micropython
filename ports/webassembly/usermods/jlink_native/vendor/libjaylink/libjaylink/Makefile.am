# SPDX-License-Identifier: LGPL-2.1-or-later

if SUBPROJECT_BUILD
noinst_LTLIBRARIES = libjaylink.la
else
lib_LTLIBRARIES = libjaylink.la

library_includedir = $(includedir)/libjaylink
library_include_HEADERS = libjaylink.h
nodist_library_include_HEADERS = version.h
endif

libjaylink_la_SOURCES = \
	buffer.c \
	core.c \
	c2.c \
	device.c \
	discovery.c \
	discovery_tcp.c \
	emucom.c \
	error.c \
	fileio.c \
	jtag.c \
	list.c \
	log.c \
	socket.c \
	spi.c \
	strutil.c \
	swd.c \
	swo.c \
	target.c \
	transport.c \
	transport_tcp.c \
	util.c \
	version.c

libjaylink_la_CFLAGS = $(JAYLINK_CFLAGS)
libjaylink_la_LDFLAGS = $(JAYLINK_LDFLAGS) -no-undefined
libjaylink_la_LIBADD = $(JAYLINK_LIBS)

if HAVE_LIBUSB
libjaylink_la_SOURCES += discovery_usb.c transport_usb.c
libjaylink_la_CFLAGS += $(libusb_CFLAGS)
libjaylink_la_LIBADD += $(libusb_LIBS)
endif

noinst_HEADERS = libjaylink-internal.h
