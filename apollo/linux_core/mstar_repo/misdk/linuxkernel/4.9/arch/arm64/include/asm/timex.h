/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_TIMEX_H
#define __ASM_TIMEX_H

#include <asm/arch_timer.h>
#include <mstar/mpatch_macro.h>

/*
 * Use the current timer as a cycle counter since this is what we use for
 * the delay loop.
 */
#if (MP_PLATFORM_ARM_64bit_PORTING == 1)
#define get_cycles()   arch_counter_get_cntpct()
#else
#define get_cycles()	arch_counter_get_cntvct()
#endif

#include <asm-generic/timex.h>

#endif
