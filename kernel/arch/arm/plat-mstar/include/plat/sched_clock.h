#ifndef ARM_PLAT_SCHED_CLOCK_H
#define ARM_PLAT_SCHED_CLOCK_H

#ifdef CONFIG_MP_STATIC_TIMER_CLOCK_SOURCE
void mstar_sched_clock_init(void *, unsigned long);
#else
void mstar_sched_clock_init(void __iomem *, unsigned long);
#endif // CONFIG_MP_STATIC_TIMER_CLOCK_SOURCE

#endif
