// SPDX-License-Identifier: LGPL-2.1-or-later

#include <stdbool.h>

#include "libjaylink.h"

/**
 * @file
 *
 * Utility functions.
 */

/**
 * Check for a capability.
 *
 * The capabilities are expected to be stored in a bit array consisting of one
 * or more bytes where each individual bit represents a capability. The first
 * bit of this array is the least significant bit of the first byte and the
 * following bits are sequentially numbered in order of increasing bit
 * significance and byte index. A set bit indicates a supported capability.
 *
 * @param[in] caps Buffer with capabilities.
 * @param[in] cap Bit position of the capability to check for.
 *
 * @retval true Capability is supported.
 * @retval false Capability is not supported or invalid argument.
 *
 * @since 0.1.0
 */
JAYLINK_API bool jaylink_has_cap(const uint8_t *caps, uint32_t cap)
{
	if (!caps)
		return false;

	if (caps[cap / 8] & (1 << (cap % 8)))
		return true;

	return false;
}
