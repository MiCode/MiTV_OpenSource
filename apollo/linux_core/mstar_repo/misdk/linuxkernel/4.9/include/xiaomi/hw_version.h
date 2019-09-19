/*
 * Copyright (C) 2018 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __HW_VERSION_H
#define __HW_VERSION_H

#define AMELIE_V52Y "v52y"
#define AMELIE_V53Y "v53y"
#define AMELIE_V57Y "v57y"
#define AMELIE_V52S "v52s"
#define AMELIE_V53S "v53s"
#define AMELIE_V74S "v74s"
#define AMELIE_V79I "v79i"

typedef enum {
	HW_V52Y,
	HW_V53Y,
	HW_V57Y,
	HW_V52S,
	HW_V53S,
	HW_V74S,
	HW_V79I,
	HW_MAX
} XiaomiHwVerType;

XiaomiHwVerType get_xiaomi_hw_version(void);

#endif /* __HW_VERSION_H */
