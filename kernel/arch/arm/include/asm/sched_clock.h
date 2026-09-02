/*
 * sched_clock.h: support for extending counters to full 64-bit ns counter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <mstar/mpatch_macro.h>

#ifdef CONFIG_MP_PLATFORM_ARM_32bit_PORTING
#include <linux/clocksource.h>
#include <linux/types.h>
#endif
#ifndef ASM_SCHED_CLOCK
#define ASM_SCHED_CLOCK

struct clock_data {
       u64 epoch_ns;
       u32 epoch_cyc;
       u32 epoch_cyc_copy;
       unsigned long rate;
       u32 mult;
       u32 shift;
       bool suspended;
       bool needs_suspend;
};

extern void sched_clock_postinit(void);
extern void setup_sched_clock(u32 (*read)(void), int bits, unsigned long rate);

extern unsigned long long (*sched_clock_func)(void);

#ifdef CONFIG_MP_PLATFORM_ARM_32bit_PORTING
#define DEFINE_CLOCK_DATA(name) struct clock_data name

#if (MP_CA7_QUAD_CORE_PATCH == 1)
#ifdef CONFIG_MP_STATIC_TIMER_CLOCK_SOURCE
#if defined(CONFIG_MSTAR_CLIPPERS) || defined(CONFIG_MSTAR_MONACO)
extern unsigned long long RTC1_timer_read64(void);
#else
extern unsigned long long RTC1_timer_read32(void);
#endif // CONFIG_MSTAR_CLIPPERS
#endif // CONFIG_MP_STATIC_TIMER_CLOCK_SOURCE
static inline u64 arch_counter_get_cntpct(void)
{
	u32 cvall, cvalh;
#ifdef CONFIG_MP_STATIC_TIMER_CLOCK_SOURCE
#if defined(CONFIG_MSTAR_CLIPPERS) || defined(CONFIG_MSTAR_MONACO)
    cvall = RTC1_timer_read64();

    return cvall;
#else
    cvall = RTC1_timer_read32();

    return cvall;
#endif // CONFIG_MSTAR_CLIPPERS
#else

	asm volatile("mrrc p15, 0, %0, %1, c14" : "=r" (cvall), "=r" (cvalh));

	return ((u64) cvalh << 32) | cvall;
#endif // CONFIG_MP_STATIC_TIMER_CLOCK_SOURCE
}
extern u64 arch_counter_read(void);
#endif
#endif
#endif
