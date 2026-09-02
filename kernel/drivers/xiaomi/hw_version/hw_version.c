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
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <xiaomi/hw_version.h>


char speaker_version[64] = "2x1506r_main";

static int __init speaker_detect(char *s)
{
	if (s != NULL) {
		strcpy(speaker_version, s);
		printk(KERN_ERR"speaker_version=%s\n",speaker_version);
	}
	return 0;
}
__setup("androidboot.speaker=", speaker_detect);


char *get_xiaomi_hw_version_str(void)
{
    if(strlen(speaker_version))
		return speaker_version;
}

EXPORT_SYMBOL(get_xiaomi_hw_version_str);

/*
 * For force disable dolby dap use
 **/
static bool gXiaomiDisableDap = false;

static int __init dap_disable(char *s)
{
	static char dap[256] = {0};
	int ret = -1;

	if (s != NULL) {
		strcpy(dap, s);
		if (!strcmp(dap, "true"))
			gXiaomiDisableDap = true;
		else
			gXiaomiDisableDap = false;
	}

	return 0;
}
__setup("disable_dap=", dap_disable);

bool get_xiaomi_dap_disable(void)
{
	return gXiaomiDisableDap;
}

EXPORT_SYMBOL(get_xiaomi_dap_disable);
