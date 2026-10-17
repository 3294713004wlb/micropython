#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc -std=c11 -O2 -pthread -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wno-unused-function -Wno-unused-variable \
    -Istubs test_mailbox.c -o /tmp/micropython-native-mailbox-host
/tmp/micropython-native-mailbox-host
