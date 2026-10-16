// SPDX-License-Identifier: LGPL-2.1-or-later

#include <stdlib.h>

#include "libjaylink-internal.h"

/**
 * @file
 *
 * Singly-linked list functions.
 */

/** @private */
JAYLINK_PRIV struct list *list_prepend(struct list *list, void *data)
{
	struct list *item;

	item = malloc(sizeof(struct list));

	if (!item)
		return NULL;

	item->data = data;
	item->next = list;

	return item;
}

/** @private */
JAYLINK_PRIV struct list *list_remove(struct list *list, const void *data)
{
	struct list *item;
	struct list *tmp;

	if (!list)
		return NULL;

	item = list;

	if (item->data == data) {
		tmp = item->next;
		free(item);
		return tmp;
	}

	while (item->next) {
		if (item->next->data == data) {
			tmp = item->next;
			item->next = item->next->next;
			free(tmp);
			break;
		}

		item = item->next;
	}

	return list;
}

/** @private */
JAYLINK_PRIV struct list *list_find_custom(struct list *list,
		list_compare_callback callback, const void *user_data)
{
	if (!callback)
		return NULL;

	while (list) {
		if (callback(list->data, user_data))
			return list;

		list = list->next;
	}

	return NULL;
}

/** @private */
JAYLINK_PRIV size_t list_length(struct list *list)
{
	size_t length;

	for (length = 0; list; length++)
		list = list->next;

	return length;
}

/** @private */
JAYLINK_PRIV void list_free(struct list *list)
{
	struct list *tmp;

	while (list) {
		tmp = list;
		list = list->next;
		free(tmp);
	}
}
