/*
 * Copyright (C) 2016 Xiaomi, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
*/
#include <xiaomi/mi_notifier.h>

static BLOCKING_NOTIFIER_HEAD(mi_notifier_list);

int mi_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mi_notifier_list, nb);
}

EXPORT_SYMBOL(mi_register_client);

int mi_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mi_notifier_list, nb);
}

EXPORT_SYMBOL(mi_unregister_client);

int mi_notify_client(unsigned long event, void *val)
{
	return blocking_notifier_call_chain(&mi_notifier_list, event, val);
}

EXPORT_SYMBOL(mi_notify_client);
