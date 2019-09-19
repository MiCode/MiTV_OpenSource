/**
 * @file timer_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/profile.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/hrtimer.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>
#include <mstar/mpatch_macro.h>

#include "oprof.h"

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
#include <kdebugd/kdebugd.h>
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
static atomic_t g_aop_pc_sample_count;
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/

static DEFINE_PER_CPU(struct hrtimer, oprofile_hrtimer);
static int ctr_running;

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
/* get the vale of pc sample Count */
int aop_get_pc_sample_count(void)
{
      return atomic_read(&g_aop_pc_sample_count);
}

#ifdef CONFIG_CACHE_ANALYZER
void aop_inc_pc_sample_count(void)
{
	atomic_inc(&g_aop_pc_sample_count);
}
#endif

/* reset the PC sample count to zero */
void  aop_reset_pc_sample_count(void)
{
      atomic_set(&g_aop_pc_sample_count, 0);
}

/* Change for 100us granularity */

#ifdef CONFIG_AOP_GRANULARITY_100us
#define AOP_100USEC (MSEC_PER_SEC * 10)
#define AOP_GRANULARITY  ((((NSEC_PER_SEC + AOP_100USEC/2)/AOP_100USEC)))
#elif defined(CONFIG_AOP_GRANULARITY_1ms)
#define AOP_GRANULARITY  (((NSEC_PER_SEC + MSEC_PER_SEC/2) / MSEC_PER_SEC))
#elif defined(CONFIG_AOP_GRANULARITY_4ms)
#define AOP_GRANULARITY  TICK_NSEC 
#else
#define AOP_GRANULARITY  TICK_NSEC 
#endif

#define AOP_TICK_TIME    (AOP_GRANULARITY)

#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/


static enum hrtimer_restart oprofile_hrtimer_notify(struct hrtimer *hrtimer)
{
	oprofile_add_sample(get_irq_regs(), 0);
#if !defined(CONFIG_ADVANCE_OPROFILE) || (MP_DEBUG_TOOL_OPROFILE == 0) 
	hrtimer_forward_now(hrtimer, ns_to_ktime(TICK_NSEC));
#else
	hrtimer_forward_now(hrtimer, ns_to_ktime(AOP_TICK_TIME));
	atomic_inc(&g_aop_pc_sample_count);
#endif/*CONFIG_ADVANCE_OPROFILE, && MP_DEBUG_TOOL_OPROFILE*/

	return HRTIMER_RESTART;
}

static void __oprofile_hrtimer_start(void *unused)
{
	struct hrtimer *hrtimer = this_cpu_ptr(&oprofile_hrtimer);

	if (!ctr_running)
		return;

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = oprofile_hrtimer_notify;

#if !defined(CONFIG_ADVANCE_OPROFILE) || (MP_DEBUG_TOOL_OPROFILE == 1)
	hrtimer_start(hrtimer, ns_to_ktime(TICK_NSEC),
		      HRTIMER_MODE_REL_PINNED);
#else
	hrtimer_start(hrtimer, ns_to_ktime(AOP_TICK_TIME),
		      HRTIMER_MODE_REL_PINNED);
#endif/* CONFIG_ADVANCE_OPROFILE && MP_DEBUG_TOOL_OPROFILE*/

}

static int oprofile_hrtimer_start(void)
{
	get_online_cpus();
	ctr_running = 1;
	on_each_cpu(__oprofile_hrtimer_start, NULL, 1);
	put_online_cpus();
	return 0;
}

static void __oprofile_hrtimer_stop(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(oprofile_hrtimer, cpu);

	if (!ctr_running)
		return;

	hrtimer_cancel(hrtimer);
}

static void oprofile_hrtimer_stop(void)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		__oprofile_hrtimer_stop(cpu);
	ctr_running = 0;
	put_online_cpus();
}

static int oprofile_timer_online(unsigned int cpu)
{
	local_irq_disable();
	__oprofile_hrtimer_start(NULL);
	local_irq_enable();
	return 0;
}

static int oprofile_timer_prep_down(unsigned int cpu)
{
	__oprofile_hrtimer_stop(cpu);
	return 0;
}

static enum cpuhp_state hp_online;

static int oprofile_hrtimer_setup(void)
{
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"oprofile/timer:online",
					oprofile_timer_online,
					oprofile_timer_prep_down);
	if (ret < 0)
		return ret;
	hp_online = ret;
	return 0;
}

static void oprofile_hrtimer_shutdown(void)
{
	cpuhp_remove_state_nocalls(hp_online);
}

int oprofile_timer_init(struct oprofile_operations *ops)
{
	ops->create_files	= NULL;
	ops->setup		= oprofile_hrtimer_setup;
	ops->shutdown		= oprofile_hrtimer_shutdown;
	ops->start		= oprofile_hrtimer_start;
	ops->stop		= oprofile_hrtimer_stop;
	ops->cpu_type		= "timer";
	printk(KERN_INFO "oprofile: using timer interrupt.\n");
	return 0;
}
