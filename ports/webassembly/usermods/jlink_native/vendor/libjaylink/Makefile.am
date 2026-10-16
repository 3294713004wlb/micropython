# SPDX-License-Identifier: LGPL-2.1-or-later

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = libjaylink

if !SUBPROJECT_BUILD
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libjaylink.pc
endif

EXTRA_DIST = README.md HACKING.md contrib/60-libjaylink.rules
