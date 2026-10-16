// SPDX-License-Identifier: LGPL-2.1-or-later

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include "libjaylink.h"
#include "libjaylink-internal.h"

/*
 * libusb.h includes windows.h and therefore must be included after anything
 * that includes winsock2.h.
 */
#include <libusb.h>

/**
 * @file
 *
 * Device discovery (USB).
 */

/** @cond PRIVATE */
/** USB Vendor ID (VID) of SEGGER products. */
#define USB_VENDOR_ID			0x1366

/* USB Product IDs (PID) and their corresponding USB addresses. */
static const struct {
	uint16_t pid;
	enum jaylink_usb_address usb_address;
} pids[] = {
	{0x0101, JAYLINK_USB_ADDRESS_0},
	{0x0102, JAYLINK_USB_ADDRESS_1},
	{0x0103, JAYLINK_USB_ADDRESS_2},
	{0x0104, JAYLINK_USB_ADDRESS_3},
	{0x0105, JAYLINK_USB_ADDRESS_0},
	{0x0107, JAYLINK_USB_ADDRESS_0},
	{0x0108, JAYLINK_USB_ADDRESS_0},

	{0x1010, JAYLINK_USB_ADDRESS_0},
	{0x1011, JAYLINK_USB_ADDRESS_0},
	{0x1012, JAYLINK_USB_ADDRESS_0},
	{0x1013, JAYLINK_USB_ADDRESS_0},
	{0x1014, JAYLINK_USB_ADDRESS_0},
	{0x1015, JAYLINK_USB_ADDRESS_0},
	{0x1016, JAYLINK_USB_ADDRESS_0},
	{0x1017, JAYLINK_USB_ADDRESS_0},
	{0x1018, JAYLINK_USB_ADDRESS_0},
	{0x1019, JAYLINK_USB_ADDRESS_0},
	{0x101a, JAYLINK_USB_ADDRESS_0},
	{0x101b, JAYLINK_USB_ADDRESS_0},
	{0x101c, JAYLINK_USB_ADDRESS_0},
	{0x101d, JAYLINK_USB_ADDRESS_0},
	{0x101e, JAYLINK_USB_ADDRESS_0},
	{0x101f, JAYLINK_USB_ADDRESS_0},

	{0x1020, JAYLINK_USB_ADDRESS_0},
	{0x1021, JAYLINK_USB_ADDRESS_0},
	{0x1022, JAYLINK_USB_ADDRESS_0},
	{0x1023, JAYLINK_USB_ADDRESS_0},
	{0x1024, JAYLINK_USB_ADDRESS_0},
	{0x1025, JAYLINK_USB_ADDRESS_0},
	{0x1026, JAYLINK_USB_ADDRESS_0},
	{0x1027, JAYLINK_USB_ADDRESS_0},
	{0x1028, JAYLINK_USB_ADDRESS_0},
	{0x1029, JAYLINK_USB_ADDRESS_0},
	{0x102a, JAYLINK_USB_ADDRESS_0},
	{0x102b, JAYLINK_USB_ADDRESS_0},
	{0x102c, JAYLINK_USB_ADDRESS_0},
	{0x102d, JAYLINK_USB_ADDRESS_0},
	{0x102e, JAYLINK_USB_ADDRESS_0},
	{0x102f, JAYLINK_USB_ADDRESS_0},

	{0x1050, JAYLINK_USB_ADDRESS_0},
	{0x1051, JAYLINK_USB_ADDRESS_0},
	{0x1052, JAYLINK_USB_ADDRESS_0},
	{0x1053, JAYLINK_USB_ADDRESS_0},
	{0x1054, JAYLINK_USB_ADDRESS_0},
	{0x1055, JAYLINK_USB_ADDRESS_0},
	{0x1056, JAYLINK_USB_ADDRESS_0},
	{0x1057, JAYLINK_USB_ADDRESS_0},
	{0x1058, JAYLINK_USB_ADDRESS_0},
	{0x1059, JAYLINK_USB_ADDRESS_0},
	{0x105a, JAYLINK_USB_ADDRESS_0},
	{0x105b, JAYLINK_USB_ADDRESS_0},
	{0x105c, JAYLINK_USB_ADDRESS_0},
	{0x105d, JAYLINK_USB_ADDRESS_0},
	{0x105e, JAYLINK_USB_ADDRESS_0},
	{0x105f, JAYLINK_USB_ADDRESS_0},

	{0x1060, JAYLINK_USB_ADDRESS_0},
	{0x1061, JAYLINK_USB_ADDRESS_0},
	{0x1062, JAYLINK_USB_ADDRESS_0},
	{0x1063, JAYLINK_USB_ADDRESS_0},
	{0x1064, JAYLINK_USB_ADDRESS_0},
	{0x1065, JAYLINK_USB_ADDRESS_0},
	{0x1066, JAYLINK_USB_ADDRESS_0},
	{0x1067, JAYLINK_USB_ADDRESS_0},
	{0x1068, JAYLINK_USB_ADDRESS_0},
	{0x1069, JAYLINK_USB_ADDRESS_0},
	{0x106a, JAYLINK_USB_ADDRESS_0},
	{0x106b, JAYLINK_USB_ADDRESS_0},
	{0x106c, JAYLINK_USB_ADDRESS_0},
	{0x106d, JAYLINK_USB_ADDRESS_0},
	{0x106e, JAYLINK_USB_ADDRESS_0},
	{0x106f, JAYLINK_USB_ADDRESS_0},
};

/** Maximum length of the USB string descriptor for the serial number. */
#define USB_SERIAL_NUMBER_LENGTH	12

/**
 * Maximum number of digits in a serial number
 *
 * The serial number of a device consists of at most 9 digits but user defined
 * serial numbers are allowed with up to 10 digits.
 */
#define MAX_SERIAL_NUMBER_DIGITS	10
/** @endcond */

static bool parse_serial_number(const char *str, uint32_t *serial_number)
{
	size_t length;

	length = strlen(str);

	/*
	 * Skip the first digits which are not part of a valid serial number.
	 * This is necessary because some devices erroneously use random digits
	 * instead of zeros for padding.
	 */
	if (length > MAX_SERIAL_NUMBER_DIGITS)
		str = str + (length - MAX_SERIAL_NUMBER_DIGITS);

	if (jaylink_parse_serial_number(str, serial_number) != JAYLINK_OK)
		return false;

	return true;
}

static bool compare_devices(const void *a, const void *b)
{
	const struct jaylink_device *dev;
	const struct libusb_device *usb_dev;

	dev = a;
	usb_dev = b;

	if (dev->iface != JAYLINK_HIF_USB)
		return false;

	if (dev->usb_dev == usb_dev)
		return true;

	return false;
}

static struct jaylink_device *find_device(const struct jaylink_context *ctx,
		const struct libusb_device *usb_dev)
{
	struct list *item;

	item = list_find_custom(ctx->devs, &compare_devices, usb_dev);

	if (item)
		return item->data;

	return NULL;
}

static struct jaylink_device *probe_device(struct jaylink_context *ctx,
		struct libusb_device *usb_dev)
{
	int ret;
	struct libusb_device_descriptor desc;
	struct libusb_device_handle *usb_devh;
	struct jaylink_device *dev;
	char buf[USB_SERIAL_NUMBER_LENGTH + 1];
	char product[JAYLINK_PRODUCT_NAME_MAX_LENGTH + JAYLINK_NICKNAME_MAX_LENGTH + 1];
	bool has_nickname;
	char *nickname;
	enum jaylink_usb_address usb_address;
	uint32_t serial_number;
	bool has_serial_number;
	bool found_device;

	ret = libusb_get_device_descriptor(usb_dev, &desc);

	if (ret != LIBUSB_SUCCESS) {
		log_warn(ctx, "Failed to get device descriptor: %s",
			libusb_error_name(ret));
		return NULL;
	}

	if (desc.idVendor != USB_VENDOR_ID)
		return NULL;

	found_device = false;

	for (size_t i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
		if (pids[i].pid == desc.idProduct) {
			usb_address = pids[i].usb_address;
			found_device = true;
			break;
		}
	}

	if (!found_device)
		return NULL;

	log_dbg(ctx, "Found device (VID:PID = %04x:%04x, bus:address = "
		"%03u:%03u)", desc.idVendor, desc.idProduct,
		libusb_get_bus_number(usb_dev),
		libusb_get_device_address(usb_dev));

	/*
	 * Search for an already allocated device instance for this device and
	 * if found return a reference to it.
	 */
	dev = find_device(ctx, usb_dev);

	if (dev) {
		log_dbg(ctx, "Device: USB address = %u", dev->usb_address);

		if (dev->has_serial_number)
			log_dbg(ctx, "Device: Serial number = %u",
				dev->serial_number);
		else
			log_dbg(ctx, "Device: Serial number = N/A");

		log_dbg(ctx, "Using existing device instance");
		return jaylink_ref_device(dev);
	}

	/*
	 * Open the device to be able to retrieve its product name and serial
	 * number.
	 */
	ret = libusb_open(usb_dev, &usb_devh);

	if (ret != LIBUSB_SUCCESS) {
		log_warn(ctx, "Failed to open device: %s",
			libusb_error_name(ret));
		return NULL;
	}

	/*
	 * Retrieve the product string descriptor from the device and extract
	 * the nickname, if available. The string has the following format:
	 * <product> [ "_" <nickname> ].
	 */
	ret = libusb_get_string_descriptor_ascii(usb_devh, desc.iProduct,
		(unsigned char *)product, sizeof(product));

	if (ret < 0) {
		nickname = NULL;
		log_warn(ctx, "Failed to retrieve product string: %s",
			libusb_error_name(ret));
	} else {
		nickname = strrchr(product, '_');
		if (nickname != NULL)
			nickname++;
	}

	has_nickname = nickname != NULL && strlen(nickname) > 0;

	ret = libusb_get_string_descriptor_ascii(usb_devh, desc.iSerialNumber,
		(unsigned char *)buf, USB_SERIAL_NUMBER_LENGTH + 1);

	libusb_close(usb_devh);

	serial_number = 0;
	has_serial_number = true;

	if (ret < 0) {
		log_warn(ctx, "Failed to retrieve serial number: %s",
			libusb_error_name(ret));
		has_serial_number = false;
	}

	if (has_serial_number) {
		if (!parse_serial_number(buf, &serial_number)) {
			log_warn(ctx, "Failed to parse serial number");
			return NULL;
		}
	}

	log_dbg(ctx, "Device: USB address = %u", usb_address);

	if (has_serial_number)
		log_dbg(ctx, "Device: Serial number = %u", serial_number);
	else
		log_dbg(ctx, "Device: Serial number = N/A");

	if (nickname != NULL)
		log_dbg(ctx, "Device: Nickname = %s", nickname);
	else
		log_dbg(ctx, "Device: Nickname = N/A");

	log_dbg(ctx, "Allocating new device instance");

	dev = device_allocate(ctx);

	if (!dev) {
		log_warn(ctx, "Device instance malloc failed");
		return NULL;
	}

	dev->iface = JAYLINK_HIF_USB;
	dev->usb_dev = libusb_ref_device(usb_dev);
	dev->usb_address = usb_address;
	dev->serial_number = serial_number;
	dev->has_serial_number = has_serial_number;

	if (has_nickname)
		snprintf(dev->nickname, sizeof(dev->nickname), "%s", nickname);

	dev->has_nickname = has_nickname;

	return dev;
}

JAYLINK_PRIV int discovery_usb_scan(struct jaylink_context *ctx)
{
	ssize_t ret;
	struct libusb_device **devs;
	struct jaylink_device *dev;
	size_t num;

	ret = libusb_get_device_list(ctx->usb_ctx, &devs);

	if (ret == LIBUSB_ERROR_IO) {
		log_err(ctx, "Failed to retrieve device list: input/output "
			"error");
		return JAYLINK_ERR_IO;
	} else if (ret < 0) {
		log_err(ctx, "Failed to retrieve device list: %s",
			libusb_error_name(ret));
		return JAYLINK_ERR;
	}

	num = 0;

	for (size_t i = 0; devs[i]; i++) {
		dev = probe_device(ctx, devs[i]);

		if (!dev)
			continue;

		ctx->discovered_devs = list_prepend(ctx->discovered_devs, dev);
		num++;
	}

	libusb_free_device_list(devs, true);
	log_dbg(ctx, "Found %zu USB device(s)", num);

	return JAYLINK_OK;
}
