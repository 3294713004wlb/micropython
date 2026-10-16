/* Minimal libusb ABI subset for libjaylink's browser-only USB transport.
 * Deliberately NOT a system-wide libusb replacement.
 */
#ifndef JLINK_BROWSER_LIBUSB_H
#define JLINK_BROWSER_LIBUSB_H
#include <stdint.h>
#include <sys/types.h>

#define LIBUSB_SUCCESS 0
#define LIBUSB_ERROR_IO -1
#define LIBUSB_ERROR_INVALID_PARAM -2
#define LIBUSB_ERROR_ACCESS -3
#define LIBUSB_ERROR_NO_DEVICE -4
#define LIBUSB_ERROR_NOT_FOUND -5
#define LIBUSB_ERROR_BUSY -6
#define LIBUSB_ERROR_TIMEOUT -7
#define LIBUSB_ERROR_OVERFLOW -8
#define LIBUSB_ERROR_PIPE -9
#define LIBUSB_ERROR_INTERRUPTED -10
#define LIBUSB_ERROR_NO_MEM -11
#define LIBUSB_ERROR_NOT_SUPPORTED -12
#define LIBUSB_ENDPOINT_IN 0x80
#define LIBUSB_CLASS_VENDOR_SPEC 0xff

struct libusb_context { int reserved; };
struct libusb_device { int reserved; };
struct libusb_device_handle { int reserved; };
struct libusb_device_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};
struct libusb_endpoint_descriptor {
    uint8_t bLength, bDescriptorType, bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval, bRefresh, bSynchAddress;
    const unsigned char *extra;
    int extra_length;
};
struct libusb_interface_descriptor {
    uint8_t bLength, bDescriptorType, bInterfaceNumber;
    uint8_t bAlternateSetting, bNumEndpoints;
    uint8_t bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol;
    uint8_t iInterface;
    const struct libusb_endpoint_descriptor *endpoint;
    const unsigned char *extra;
    int extra_length;
};
struct libusb_interface {
    const struct libusb_interface_descriptor *altsetting;
    int num_altsetting;
};
struct libusb_config_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue, iConfiguration;
    uint8_t bmAttributes;
    uint8_t MaxPower;
    const struct libusb_interface *interface;
    const unsigned char *extra;
    int extra_length;
};

int libusb_init(struct libusb_context **context);
void libusb_exit(struct libusb_context *context);
ssize_t libusb_get_device_list(struct libusb_context *context, struct libusb_device ***list);
void libusb_free_device_list(struct libusb_device **list, int unref_devices);
struct libusb_device *libusb_ref_device(struct libusb_device *device);
void libusb_unref_device(struct libusb_device *device);
int libusb_get_device_descriptor(struct libusb_device *device, struct libusb_device_descriptor *descriptor);
int libusb_get_active_config_descriptor(struct libusb_device *device, struct libusb_config_descriptor **config);
void libusb_free_config_descriptor(struct libusb_config_descriptor *config);
uint8_t libusb_get_bus_number(struct libusb_device *device);
uint8_t libusb_get_device_address(struct libusb_device *device);
int libusb_get_port_numbers(struct libusb_device *device, uint8_t *numbers, int length);
int libusb_open(struct libusb_device *device, struct libusb_device_handle **handle);
void libusb_close(struct libusb_device_handle *handle);
int libusb_claim_interface(struct libusb_device_handle *handle, int number);
int libusb_release_interface(struct libusb_device_handle *handle, int number);
int libusb_get_string_descriptor_ascii(struct libusb_device_handle *handle, uint8_t index, unsigned char *data, int length);
int libusb_bulk_transfer(struct libusb_device_handle *handle, unsigned char endpoint, unsigned char *data, int length, int *transferred, unsigned int timeout);
const char *libusb_error_name(int error);
#endif
