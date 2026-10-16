#!/usr/bin/env sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VENDOR="$SCRIPT_DIR/vendor/libjaylink"
if [ -e "$VENDOR/.git" ] || [ -f "$VENDOR/libjaylink/core.c" ]; then
    echo 'libjaylink already present; not modifying its source'
    exit 0
fi
# Prefer the maintained upstream. Pin with: LIBJAYLINK_REV=<commit> ./fetch-libjaylink.sh
URL=${LIBJAYLINK_URL:-https://gitlab.zapb.de/libjaylink/libjaylink.git}
git clone "$URL" "$VENDOR"
if [ -n "${LIBJAYLINK_REV:-}" ]; then
    git -C "$VENDOR" checkout "$LIBJAYLINK_REV"
fi
# Different versions use Meson or Autotools: the browser make fragment
# compiles the explicit C file list rather than invoking the original build.
if [ -f "$VENDOR/libjaylink/version.h.in" ] && [ ! -f "$VENDOR/libjaylink/version.h" ]; then
    sed -e 's/@JAYLINK_VERSION_PACKAGE_MAJOR@/0/g' \
        -e 's/@JAYLINK_VERSION_PACKAGE_MINOR@/0/g' \
        -e 's/@JAYLINK_VERSION_PACKAGE_MICRO@/0/g' \
        -e 's/@JAYLINK_VERSION_PACKAGE@/0.0.0-browser/g' \
        -e 's/@JAYLINK_VERSION_LIBRARY_CURRENT@/0/g' \
        -e 's/@JAYLINK_VERSION_LIBRARY_REVISION@/0/g' \
        -e 's/@JAYLINK_VERSION_LIBRARY_AGE@/0/g' \
        -e 's/@JAYLINK_VERSION_LIBRARY@/0:0:0/g' \
        "$VENDOR/libjaylink/version.h.in" > "$VENDOR/libjaylink/version.h"
fi
printf '\nSource revision: '
git -C "$VENDOR" rev-parse HEAD
printf 'Rebuild with: make VARIANT=standard clean && make VARIANT=standard -j4\n'
