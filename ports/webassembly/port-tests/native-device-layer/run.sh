#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$ROOT" \
    "$ROOT/device/device_serial.c" "$ROOT/device/device_hid.c" \
    "$ROOT/device/device_usb.c" \
    "$ROOT/port-tests/native-device-layer/test_device.c" \
    -o "$TMP/test_device"
"$TMP/test_device"
