/*
 *  linux/arch/arm/plat-versatile/hotplug.c
 *
 *  Copyright (C) 2010 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/completion.h>
#include <linux/version.h>
#include <asm/cacheflush.h>

#include <plat/hotplug.h>
#include <asm/cputype.h>
#include <asm/psci.h>
#include "mdrv_types.h"
#include <linux/delay.h>
#include "mdrv_tee_general.h"

static DECLARE_COMPLETION(cpu_killed);


static unsigned int get_smp_ctrl_mask(void)
{
	/* here to get Main ID Register by using read_cpuid_id, bit_4 ~ bit_15 is Primary part number, CA7 is 0xC07, CA15 is 0xC0F, CA12 is 0xC0D
	 * the return value is the SMP_Bit of ACTLR of that processor type
	 */
    switch (read_cpuid_id() & 0xfff0)
    {
        case 0xb020: /* 11mpcore */
            return 0x20;
        case 0xc090: /* Cortex-A9 */
            return 0x40;
        case 0xc0D0: /* Cortex-A12 */
            return 0x40;
        default:
            return 0;
    }
}
extern void Chip_Flush_Cache_All(void);

static inline void cpu_enter_lowpower(void)
{
    unsigned int v, smp_ctrl = get_smp_ctrl_mask();

    flush_cache_all();
    //Chip_Flush_Cache_All();
    dsb();
    asm volatile(
    /*
     * Turn off coherency
     */
    "   mrc p15, 0, %0, c1, c0, 1\n"
    "   bic %0, %0, %1\n"
    "   mcr p15, 0, %0, c1, c0, 1\n"
	/* DSB */
    "   mcr     p15, 0, %2, c7, c10, 4\n"
	/* Disable D-cache */
    "   mrc p15, 0, %0, c1, c0, 0\n"
    "   bic %0, %0, #0x04\n"
    "   mcr p15, 0, %0, c1, c0, 0\n"
      : "=&r" (v)
      : "r" (smp_ctrl), "r" (0)
      : "memory");
	isb();
}

static inline void cpu_leave_lowpower(void)
{
    unsigned int v, smp_ctrl = get_smp_ctrl_mask();

    flush_cache_all();
    //Chip_Flush_Cache_All();
    dsb();

	/* enable D-cache and Turn on coherency */
    asm volatile(
	"   mrc p15, 0, %0, c1, c0, 0\n"
    "   orr %0, %0, #0x04\n"
    "   mcr p15, 0, %0, c1, c0, 0\n"
	"   mrc p15, 0, %0, c1, c0, 1\n"
    "   orr %0, %0, %1\n"
    "   mcr p15, 0, %0, c1, c0, 1\n"
      : "=&r" (v)
      : "r" (smp_ctrl)
      : "memory");
    isb();
}

extern uint32_t isPSCI;
extern uint32_t isPMU_SUPPORT;

#if LINUX_VERSION_CODE > KERNEL_VERSION(3,1,10)
int platform_mstar_cpu_kill(unsigned int cpu)
#else
int platform_cpu_kill(unsigned int cpu)
#endif
{
	if(isPSCI == PSCI_RET_SUCCESS && TEEINFO_TYPTE==SECURITY_TEEINFO_OSTYPE_OPTEE)
		return psci_smp_ops.cpu_kill(cpu);
	else
		return wait_for_completion_timeout(&cpu_killed, 5000);
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
 
void HotPlug_unlock(void)
{
    complete(&cpu_killed);
}

unsigned int MdrvSecurePwMng_CoreOn(unsigned int u32CoreNum, unsigned long u64WakeupAddress)
{
    printk(KERN_DEBUG "\033[33mFunction = %s, Line = %d, CPU%u @ 0x%lx: wakeup\033[m\n", __PRETTY_FUNCTION__, __LINE__, u32CoreNum, u64WakeupAddress);
#if 0
    __asm__ volatile(
        ".arch_extension sec\n\t"
        "ldr r0, =0x8400ff0f\n\t"
        "ldr r1, %0\n\t"
        "ldr r2, %1\n\t"
        "smc #0\n\t"
        :
        : "m" (u32CoreNum), "m" (u64WakeupAddress)
    );
#else
    struct arm_smccc_res_mstar input_param = {0};
    input_param.a0 = 0x8400ff0f;
    input_param.a1 = u32CoreNum;
    input_param.a2 = u64WakeupAddress;
    tee_fast_call_cmd(&input_param);
#endif
    return 0;
}

unsigned int MdrvSecurePwMng_CoreOff(unsigned int u32CoreNum)
{
    extern void git_dist_subset_save(void);

    printk(KERN_DEBUG "\033[33mFunction = %s, Line = %d, CPU%u: shutdown\033[m\n", __PRETTY_FUNCTION__, __LINE__, u32CoreNum);
#if 0
    __asm__ volatile(
        ".arch_extension sec\n\t"
        "ldr r0, =0x8400ff0e\n\t"
        "ldr r1, %0\n\t"
        "smc #0\n\t"
        :
        : "m" (u32CoreNum)
    );
#else
    struct arm_smccc_res_mstar input_param = {0};
    input_param.a0 = 0x8400ff0e;
    input_param.a1 = u32CoreNum;
    tee_fast_call_cmd(&input_param);
#endif
    return 0;
}

unsigned int MdrvSecurePwMng_PMUIrqOff(unsigned int u32CoreNum)
{
#if 0
    __asm__ volatile(
        ".arch_extension sec\n\t"
        "ldr r0, =0x8400ee21\n\t"
        "ldr r1, %0\n\t"
        "smc #0\n\t"
        :
        : "m" (u32CoreNum)
    );
#else
    struct arm_smccc_res_mstar input_param = {0};
    input_param.a0 = 0x8400ee21;
    input_param.a1 = u32CoreNum;
    tee_fast_call_cmd(&input_param);
#endif
    return 0;
}

 void platform_mstar_cpu_done_kill(unsigned int cpu)
{
	complete(&cpu_killed);
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(3,1,10)
void platform_mstar_cpu_die(unsigned int cpu)
#else
void platform_cpu_die(unsigned int cpu)
#endif
{
#ifdef DEBUG
    unsigned int this_cpu = hard_smp_processor_id();

    if (cpu != this_cpu) {
        printk(KERN_CRIT "Eek! platform_cpu_die running on %u, should be %u\n",
               this_cpu, cpu);
        BUG();
    }
#endif

	printk(KERN_DEBUG "\033[33mFunction = %s, Line = %d, CPU%u: shutdown\033[m\n", __PRETTY_FUNCTION__, __LINE__, cpu);
	complete(&cpu_killed);

	cpu_enter_lowpower();

	   /*
     * there is no power-control hardware on this platform, so all
     * we can do is put the core into WFI; this is safe as the calling
     * code will have already disabled interrupts
     */
    for (;;) {
        /*
         * here's the WFI
         */
        extern volatile int pen_release ;
        asm volatile("wfi" : : : "memory");

        if (pen_release == cpu) {
            /*
             * OK, proper wakeup, we're done
             */
            break;
        }

        /*
         * getting here, means that we have come out of WFI without
         * having been woken up - this shouldn't happen
         *
         * The trouble is, letting people know about this is not really
         * possible, since we are currently running incoherently, and
         * therefore cannot safely call printk() or anything else
         */
#ifdef DEBUG
        printk("CPU%u: spurious wakeup call\n", cpu);
#endif
    }
    cpu_leave_lowpower();
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(3,1,10)
int platform_mstar_cpu_disable(unsigned int cpu)
#else
int platform_cpu_disable(unsigned int cpu)
#endif
{
    /*
     * we don't allow CPU 0 to be shutdown (it is still too special
     * e.g. clock tick interrupts)
     */
    return cpu == 0 ? -EPERM : 0;
}
