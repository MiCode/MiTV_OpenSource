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

static XiaomiHwVerType gXiaomiHwType = HW_V52Y;

static XiaomiHwVerType __init hw_detect(char *s)
{
	static char hw_version[256] = {0};
	int ret = -1;

	if (s != NULL) {
		strcpy(hw_version, s);
		if (!strcmp(hw_version, AMELIE_V52Y))
			gXiaomiHwType = HW_V52Y;
		else if (!strcmp(hw_version, AMELIE_V53Y))
			gXiaomiHwType = HW_V53Y;
		else if (!strcmp(hw_version, AMELIE_V57Y))
			gXiaomiHwType = HW_V57Y;
		else if (!strcmp(hw_version, AMELIE_V52S))
			gXiaomiHwType = HW_V52S;
		else if (!strcmp(hw_version, AMELIE_V53S))
			gXiaomiHwType = HW_V53S;
		else if (!strcmp(hw_version, AMELIE_V74S))
			gXiaomiHwType = HW_V74S;
		else if (!strcmp(hw_version, AMELIE_V79I))
			gXiaomiHwType = HW_V79I;
		else
			gXiaomiHwType = HW_V52Y;
	}

	return gXiaomiHwType;
}
__setup("androidboot.hardware_version=", hw_detect);

XiaomiHwVerType get_xiaomi_hw_version(void)
{
	return gXiaomiHwType;
}

EXPORT_SYMBOL(get_xiaomi_hw_version);
