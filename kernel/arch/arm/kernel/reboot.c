/*
 *  Copyright (C) 1996-2000 Russell King - Converted to ARM.
 *  Original Copyright (C) 1995  Linus Torvalds
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/reboot.h>

#include <asm/cacheflush.h>
#include <asm/idmap.h>
#include <asm/virt.h>

#include "reboot.h"

#include <mach/system.h>
#include <mstar/mpatch_macro.h>

typedef void (*phys_reset_t)(unsigned long, bool);

#ifdef CONFIG_MP_PLATFORM_ARM_32bit_PORTING
#if (MP_PLATFORM_PM == 1)
static void arm_machine_restart(char mode, const char *cmd)
{
	/*
	 * Tell the mm system that we are going to reboot -
	 * we may need it to insert some 1:1 mappings so that
	 * soft boot works.
	 */
	setup_mm_for_reboot();

#ifndef CONFIG_MP_PLATFORM_ARM
	/* Clean and invalidate caches */
	flush_cache_all();

	/* Turn off caching */
	cpu_proc_fin();

	/* Push out any further dirty data, and ensure cache is empty */
	flush_cache_all();
#endif
	/*
	 * Now call the architecture specific reboot code.
	 */
	arch_reset(mode, cmd);
}
#else
static void arm_machine_restart(char mode, const char *cmd){}
#endif
#endif
/*
 * Function pointers to optional machine specific functions
 */
#ifdef CONFIG_MP_PLATFORM_ARM_32bit_PORTING
void (*arm_pm_restart)(char mode, const char *cmd) = arm_machine_restart;
EXPORT_SYMBOL_GPL(arm_pm_restart);
#else
void (*arm_pm_restart)(enum reboot_mode reboot_mode, const char *cmd);
#endif

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/*
 * A temporary stack to use for CPU reset. This is static so that we
 * don't clobber it with the identity mapping. When running with this
 * stack, any references to the current task *will not work* so you
 * should really do as little as possible before jumping to your reset
 * code.
 */
static u64 soft_restart_stack[16];

static void __soft_restart(void *addr)
{
	phys_reset_t phys_reset;

	/* Take out a flat memory mapping. */
	setup_mm_for_reboot();

	/* Clean and invalidate caches */
	flush_cache_all();

	/* Turn off caching */
	cpu_proc_fin();

	/* Push out any further dirty data, and ensure cache is empty */
	flush_cache_all();

	/* Switch to the identity mapping. */
	phys_reset = (phys_reset_t)virt_to_idmap(cpu_reset);

	/* original stub should be restored by kvm */
	phys_reset((unsigned long)addr, is_hyp_mode_available());

	/* Should never get here. */
	BUG();
}

void _soft_restart(unsigned long addr, bool disable_l2)
{
	u64 *stack = soft_restart_stack + ARRAY_SIZE(soft_restart_stack);

	/* Disable interrupts first */
	raw_local_irq_disable();
	local_fiq_disable();

	/* Disable the L2 if we're the last man standing. */
	if (disable_l2)
		outer_disable();

	/* Change to the new stack and continue with the reset. */
	call_with_stack(__soft_restart, (void *)addr, (void *)stack);

	/* Should never get here. */
	BUG();
}

void soft_restart(unsigned long addr)
{
	_soft_restart(addr, num_online_cpus() == 1);
}

#ifdef CONFIG_MP_PLATFORM_ARM_32bit_PORTING
int __init reboot_setup(char *str)
{
	reboot_mode = str[0];
	return 1;
}
__setup("reboot=", reboot_setup);
#endif

/*
 * Called by kexec, immediately prior to machine_kexec().
 *
 * This must completely disable all secondary CPUs; simply causing those CPUs
 * to execute e.g. a RAM-based pin loop is not sufficient. This allows the
 * kexec'd kernel to use any and all RAM as it sees fit, without having to
 * avoid any code or data used by any SW CPU pin loop. The CPU hotplug
 * functionality embodied in disable_nonboot_cpus() to achieve this.
 */
void machine_shutdown(void)
{
	disable_nonboot_cpus();
}

/*
 * Halting simply requires that the secondary CPUs stop performing any
 * activity (executing tasks, handling interrupts). smp_send_stop()
 * achieves this.
 */
void machine_halt(void)
{
	local_irq_disable();
	smp_send_stop();
	while (1);
}

/*
 * Power-off simply requires that the secondary CPUs stop performing any
 * activity (executing tasks, handling interrupts). smp_send_stop()
 * achieves this. When the system power is turned off, it will take all CPUs
 * with it.
 */
void machine_power_off(void)
{
	local_irq_disable();
	smp_send_stop();

	if (pm_power_off)
		pm_power_off();
}

/*
 * Restart requires that the secondary CPUs stop performing any activity
 * while the primary CPU resets the system. Systems with a single CPU can
 * use soft_restart() as their machine descriptor's .restart hook, since that
 * will cause the only available CPU to reset. Systems with multiple CPUs must
 * provide a HW restart implementation, to ensure that all CPUs reset at once.
 * This is required so that any code running after reset on the primary CPU
 * doesn't have to co-ordinate with other CPUs to ensure they aren't still
 * executing pre-reset code, and using RAM that the primary CPU's code wishes
 * to use. Implementing such co-ordination would be essentially impossible.
 */
void machine_restart(char *cmd)
{
	local_irq_disable();
	smp_send_stop();

#ifdef CONFIG_MP_PLATFORM_ARM_32bit_PORTING
	/* Disable interrupts first */
	local_irq_disable();
	local_fiq_disable();

	smp_send_stop();
#endif

	if (arm_pm_restart)
		arm_pm_restart(reboot_mode, cmd);
	else
		do_kernel_restart(cmd);

	/* Give a grace period for failure to restart of 1s */
	mdelay(1000);

	/* Whoops - the platform was unable to reboot. Tell the user! */
	printk("Reboot failed -- System halted\n");
	while (1);
}
