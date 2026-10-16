// SPDX-License-Identifier: LGPL-2.1-or-later

#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "libjaylink.h"
#include "libjaylink-internal.h"

/**
 * @file
 *
 * Device discovery.
 */

static void clear_discovery_list(struct jaylink_context *ctx)
{
	struct list *item;
	struct list *tmp;
	struct jaylink_device *dev;

	item = ctx->discovered_devs;

	while (item) {
		dev = (struct jaylink_device *)item->data;
		jaylink_unref_device(dev);

		tmp = item;
		item = item->next;
		free(tmp);
	}

	ctx->discovered_devs = NULL;
}

/**
 * Scan for devices.
 *
 * @param[in,out] ctx libjaylink context.
 * @param[in] ifaces Host interfaces to scan for devices. Use bitwise OR to
 *                   specify multiple interfaces, or 0 to use all available
 *                   interfaces. See #jaylink_host_interface for a description
 *                   of the interfaces.
 *
 * @retval JAYLINK_OK Success.
 * @retval JAYLINK_ERR_ARG Invalid arguments.
 * @retval JAYLINK_ERR_IO Input/output error.
 * @retval JAYLINK_ERR Other error conditions.
 *
 * @see jaylink_get_devices()
 *
 * @since 0.1.0
 */
JAYLINK_API int jaylink_discovery_scan(struct jaylink_context *ctx,
		uint32_t ifaces)
{
	int ret;

	if (!ctx)
		return JAYLINK_ERR_ARG;

	if (!ifaces)
		ifaces = JAYLINK_HIF_USB | JAYLINK_HIF_TCP;

	clear_discovery_list(ctx);

#ifdef HAVE_LIBUSB
	if (ifaces & JAYLINK_HIF_USB) {
		ret = discovery_usb_scan(ctx);

		if (ret != JAYLINK_OK) {
			log_err(ctx, "USB device discovery failed");
			return ret;
		}
	}
#endif

	if (ifaces & JAYLINK_HIF_TCP) {
		ret = discovery_tcp_scan(ctx);

		if (ret != JAYLINK_OK) {
			log_err(ctx, "TCP/IP device discovery failed");
			return ret;
		}
	}

	return JAYLINK_OK;
}
