/*
 * include/asm-arm/arch-at91/debug-macro.S
 *
 *  Copyright (C) 2003-2005 SAN People
 *
 * Debugging macro include header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <mach/hardware.h>

	.macro	addruart,rx
	mrc	p15, 0, \rx, c1, c0
	.endm

	.macro	senduart,rd,rx
	.endm

	.macro	waituart,rd,rx
	.endm

	.macro	busyuart,rd,rx
	.endm

