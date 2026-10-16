#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libusb.h"
#include "pyusb_bridge.h"
static const jlink_usb_config_t cfg = {
    .vid = 0x1366, .pid = 0x1015, .ep_in = 0x81, .ep_out = 0x02,
    .interface_number = 0, .serial = "123456789", .product = "J-Link WebUSB Test"
};
static int zero_write = 0;
const jlink_usb_config_t *jlink_usb_config(void) { return &cfg; }
int jlink_usb_is_ready(void) { return 1; }
int jlink_usb_transfer_read(uint8_t ep, uint8_t *buf, size_t n, unsigned t) {
    assert(ep == 0x81 && n == 3 && t == 1000);
    memcpy(buf,"ABC",3); return 3;
}
int jlink_usb_transfer_write(uint8_t ep, const uint8_t *buf, size_t n, unsigned t) {
    assert(ep == 0x02 && n == 2 && t == 1000 && memcmp(buf,"hi",2) == 0);
    return zero_write ? 0 : 2;
}
int main(void) {
    struct libusb_context *ctx;
    struct libusb_device **devs;
    struct libusb_device_descriptor desc;
    struct libusb_config_descriptor *config;
    struct libusb_device_handle *handle;
    assert(libusb_init(&ctx) == 0);
    assert(libusb_get_device_list(ctx,&devs) == 1);
    assert(libusb_get_device_descriptor(devs[0],&desc) == 0);
    assert(desc.idVendor == 0x1366 && desc.idProduct == 0x1015);
    assert(desc.iProduct == 2 && desc.iSerialNumber == 1);
    assert(libusb_get_active_config_descriptor(devs[0], &config) == 0);
    assert(config->bNumInterfaces == 1);
    assert(config->interface[0].altsetting[0].endpoint[0].bEndpointAddress == 0x81);
    libusb_free_config_descriptor(config);
    assert(libusb_open(devs[0], &handle) == 0);
    assert(libusb_claim_interface(handle,0) == 0);
    unsigned char serial[32]={0};
    assert(libusb_get_string_descriptor_ascii(handle,1,serial,sizeof(serial)) == 9);
    assert(strcmp((char*)serial,"123456789")==0);
    unsigned char product[64]={0};
    assert(libusb_get_string_descriptor_ascii(handle, 2, product, sizeof(product)) == 18);
    assert(strcmp((char*)product, "J-Link WebUSB Test") == 0);
    int sent=0; unsigned char write_data[]="hi";
    assert(libusb_bulk_transfer(handle,0x02,write_data,2,&sent,1000)==0 && sent==2);
    zero_write = 1;
    assert(libusb_bulk_transfer(handle,0x02,write_data,2,&sent,1000)==LIBUSB_ERROR_TIMEOUT);
    zero_write = 0;
    unsigned char recv[3];
    assert(libusb_bulk_transfer(handle,0x81,recv,3,&sent,1000)==0 && sent==3);
    assert(memcmp(recv,"ABC",3)==0);
    assert(libusb_release_interface(handle,0)==0);
    libusb_close(handle);
    libusb_free_device_list(devs,1);
    libusb_exit(ctx);
    puts("libusb PyUSB shim host smoke OK");
    return 0;
}
