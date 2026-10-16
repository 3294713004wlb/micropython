/*
 * USB-only browser build of libjaylink
 *
 * Upstream discovery.c and transport.c still reference TCP entry points even
 * when the caller exclusively requests JAYLINK_HIF_USB.  Implement those
 * entry points here instead of compiling socket.c, discovery_tcp.c or
 * transport_tcp.c into the browser WebAssembly application.
 *
 * Do not open a socket, and never silently report TCP operations as success.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "libjaylink.h"
#include "libjaylink-internal.h"

JAYLINK_PRIV int discovery_tcp_scan(struct jaylink_context *ctx) {
    (void)ctx;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_open(struct jaylink_device_handle *devh) {
    (void)devh;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_close(struct jaylink_device_handle *devh) {
    (void)devh;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_start_write_read(
    struct jaylink_device_handle *devh,
    size_t write_length,
    size_t read_length,
    bool has_command
) {
    (void)devh;
    (void)write_length;
    (void)read_length;
    (void)has_command;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_start_write(
    struct jaylink_device_handle *devh,
    size_t length,
    bool has_command
) {
    (void)devh;
    (void)length;
    (void)has_command;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_start_read(
    struct jaylink_device_handle *devh,
    size_t length
) {
    (void)devh;
    (void)length;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_write(
    struct jaylink_device_handle *devh,
    const uint8_t *buffer,
    size_t length
) {
    (void)devh;
    (void)buffer;
    (void)length;
    return JAYLINK_ERR;
}

JAYLINK_PRIV int transport_tcp_read(
    struct jaylink_device_handle *devh,
    uint8_t *buffer,
    size_t length
) {
    (void)devh;
    (void)buffer;
    (void)length;
    return JAYLINK_ERR;
}
