#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later

OS=`uname -s`
LIBTOOLIZE=libtoolize

if [ "x$OS" = "xDarwin" ]; then
	LIBTOOLIZE=glibtoolize
fi

echo "Generating build system..."

$LIBTOOLIZE --install --copy || exit 1
aclocal --warnings=all -I m4 || exit 1
autoheader --warnings=all || exit 1
autoconf --warnings=all || exit 1
automake --warnings=all --add-missing --copy || exit 1
