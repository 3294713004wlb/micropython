#ifndef JLINK_PYUSB_BRIDGE_H
#define JLINK_PYUSB_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint8_t ep_in;
    uint8_t ep_out;
    uint8_t interface_number;
    char serial[64];
    char product[128];
} jlink_usb_config_t;
const jlink_usb_config_t *jlink_usb_config(void);
int jlink_usb_is_ready(void);
/* These functions return transferred size or negative libusb error. */
int jlink_usb_transfer_read(uint8_t endpoint, uint8_t *dest, size_t size, unsigned timeout);
int jlink_usb_transfer_write(uint8_t endpoint, const uint8_t *src, size_t size, unsigned timeout);
#endif
