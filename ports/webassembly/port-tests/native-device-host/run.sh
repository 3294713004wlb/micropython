#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$ROOT/port-tests/native-device-host" -I"$ROOT" \
    "$ROOT/device/device_host.c" \
    "$ROOT/port-tests/native-device-host/test_device_host.c" -o "$TMP/test_host"
"$TMP/test_host"
