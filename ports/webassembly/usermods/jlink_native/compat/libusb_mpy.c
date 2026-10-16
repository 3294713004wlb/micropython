#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "libusb.h"
#include "pyusb_bridge.h"

static struct libusb_context mock_context;
static struct libusb_device mock_device;
static struct libusb_device_handle mock_handle;

int libusb_init(struct libusb_context **ctx) {
    if (!ctx) return LIBUSB_ERROR_INVALID_PARAM;
    *ctx = &mock_context;
    return 0;
}
void libusb_exit(struct libusb_context *ctx) { (void)ctx; }
ssize_t libusb_get_device_list(struct libusb_context *ctx, struct libusb_device ***out) {
    (void)ctx;
    if (!out) return LIBUSB_ERROR_INVALID_PARAM;
    struct libusb_device **list = calloc(2, sizeof(*list));
    if (!list) return LIBUSB_ERROR_NO_MEM;
    ssize_t count = jlink_usb_is_ready() ? 1 : 0;
    if (count) list[0] = &mock_device;
    *out = list;
    return count;
}
void libusb_free_device_list(struct libusb_device **list, int unref) {
    (void)unref;
    free(list);
}
struct libusb_device *libusb_ref_device(struct libusb_device *dev) { return dev; }
void libusb_unref_device(struct libusb_device *dev) { (void)dev; }
int libusb_get_device_descriptor(struct libusb_device *dev, struct libusb_device_descriptor *desc) {
    if (dev != &mock_device || !desc || !jlink_usb_is_ready()) return LIBUSB_ERROR_NO_DEVICE;
    memset(desc, 0, sizeof(*desc));
    desc->idVendor = jlink_usb_config()->vid;
    desc->idProduct = jlink_usb_config()->pid;
    desc->iSerialNumber = jlink_usb_config()->serial[0] ? 1 : 0;
    desc->iProduct = 2;
    desc->bNumConfigurations = 1;
    return 0;
}
int libusb_get_active_config_descriptor(struct libusb_device *dev, struct libusb_config_descriptor **out) {
    if (dev != &mock_device || !out || !jlink_usb_is_ready()) return LIBUSB_ERROR_NO_DEVICE;
    /* Allocate all four descriptors as a single region, freed by free(). */
    size_t total = sizeof(struct libusb_config_descriptor) + sizeof(struct libusb_interface) +
                   sizeof(struct libusb_interface_descriptor) + 2 * sizeof(struct libusb_endpoint_descriptor);
    unsigned char *block = calloc(1, total);
    if (!block) return LIBUSB_ERROR_NO_MEM;
    struct libusb_config_descriptor *cfg = (void *)block;
    struct libusb_interface *iface = (void *)(block + sizeof(*cfg));
    struct libusb_interface_descriptor *alt = (void *)(block + sizeof(*cfg) + sizeof(*iface));
    struct libusb_endpoint_descriptor *ep = (void *)(block + sizeof(*cfg) + sizeof(*iface) + sizeof(*alt));
    cfg->bNumInterfaces = 1;
    cfg->interface = iface;
    iface->altsetting = alt;
    iface->num_altsetting = 1;
    alt->bInterfaceClass = LIBUSB_CLASS_VENDOR_SPEC;
    alt->bInterfaceSubClass = LIBUSB_CLASS_VENDOR_SPEC;
    alt->bInterfaceNumber = jlink_usb_config()->interface_number;
    alt->bNumEndpoints = 2;
    alt->endpoint = ep;
    ep[0].bEndpointAddress = jlink_usb_config()->ep_in;
    ep[1].bEndpointAddress = jlink_usb_config()->ep_out;
    *out = cfg;
    return 0;
}
void libusb_free_config_descriptor(struct libusb_config_descriptor *cfg) { free(cfg); }
uint8_t libusb_get_bus_number(struct libusb_device *dev) { (void)dev; return 1; }
uint8_t libusb_get_device_address(struct libusb_device *dev) { (void)dev; return 1; }
int libusb_get_port_numbers(struct libusb_device *dev, uint8_t *ports, int len) {
    (void)dev;
    if (len < 1 || !ports) return LIBUSB_ERROR_OVERFLOW;
    ports[0] = 1;
    return 1;
}
int libusb_open(struct libusb_device *dev, struct libusb_device_handle **out) {
    if (dev != &mock_device || !out || !jlink_usb_is_ready()) return LIBUSB_ERROR_NO_DEVICE;
    *out = &mock_handle;
    return 0;
}
void libusb_close(struct libusb_device_handle *handle) { (void)handle; }
int libusb_claim_interface(struct libusb_device_handle *handle, int num) {
    if (handle != &mock_handle || num != (int)jlink_usb_config()->interface_number) return LIBUSB_ERROR_INVALID_PARAM;
    return 0;
}
int libusb_release_interface(struct libusb_device_handle *handle, int num) {
    return libusb_claim_interface(handle, num);
}
int libusb_get_string_descriptor_ascii(struct libusb_device_handle *handle, uint8_t idx, unsigned char *buf, int len) {
    if (handle != &mock_handle || !idx || !buf || len <= 0) return LIBUSB_ERROR_INVALID_PARAM;
    const char *value;
    if (idx == 1) {
        value = jlink_usb_config()->serial;
    } else if (idx == 2) {
        value = jlink_usb_config()->product[0] ? jlink_usb_config()->product : "J-Link";
    } else {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    if (!*value) return LIBUSB_ERROR_NOT_FOUND;
    size_t n = strlen(value);
    if (n >= (size_t)len) n = (size_t)len - 1;
    memcpy(buf, value, n);
    buf[n] = 0;
    return (int)n;
}
int libusb_bulk_transfer(struct libusb_device_handle *h, unsigned char endpoint,
    unsigned char *buffer, int size, int *transferred, unsigned int timeout) {
    if (transferred) *transferred = 0;
    if (h != &mock_handle || !transferred || size < 0 || (!buffer && size)) return LIBUSB_ERROR_INVALID_PARAM;
    int count;
    if (endpoint == jlink_usb_config()->ep_in) {
        count = jlink_usb_transfer_read(endpoint, buffer, (size_t)size, timeout);
    } else if (endpoint == jlink_usb_config()->ep_out) {
        count = jlink_usb_transfer_write(endpoint, buffer, (size_t)size, timeout);
    } else return LIBUSB_ERROR_INVALID_PARAM;
    if (count < 0) return count;
    /* A successful zero-byte bulk write otherwise spins forever in
     * libjaylink transport_usb.c:usb_send(). Treat it as timeout. */
    if (size > 0 && count == 0) return LIBUSB_ERROR_TIMEOUT;
    if (count > size) return LIBUSB_ERROR_OVERFLOW;
    *transferred = count;
    return 0;
}
const char *libusb_error_name(int error) {
    switch (error) {
        case 0: return "SUCCESS";
        case LIBUSB_ERROR_TIMEOUT: return "TIMEOUT";
        case LIBUSB_ERROR_NO_DEVICE: return "NO_DEVICE";
        case LIBUSB_ERROR_INVALID_PARAM: return "INVALID_PARAM";
        case LIBUSB_ERROR_NO_MEM: return "NO_MEM";
        case LIBUSB_ERROR_NOT_FOUND: return "NOT_FOUND";
        default: return "IO_ERROR";
    }
}
