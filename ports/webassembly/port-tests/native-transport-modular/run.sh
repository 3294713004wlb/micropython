#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
for src in transport/transport_runtime.c transport/transport_terminal.c \
    transport/transport_serial.c transport/transport_mailbox.c \
    transport/transport_usb_pipe.c \
    transport/transport_regions.c modwasm_transport.c; do
    cc -std=c11 -O2 -Werror -Wno-unused-function -Wno-unused-variable \
        -Wno-unused-parameter -ffunction-sections -fdata-sections \
        -Iport-tests/native-mailbox-host/stubs -I. \
        -c "$src" -o "$BUILD/$(basename "$src" .c).o"
done
cc -std=c11 -O2 -pthread -Wl,--gc-sections \
    -Iport-tests/native-mailbox-host/stubs -I. \
    port-tests/native-transport-modular/test_transport.c "$BUILD"/*.o \
    -o "$BUILD/test-transport"
"$BUILD/test-transport"
