/*
 * kernel/power/suspend.c - Suspend to RAM and standby functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#define pr_fmt(fmt) "PM: " fmt

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/syscalls.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/swait.h>
#include <linux/ftrace.h>
#include <trace/events/power.h>
#include <linux/compiler.h>
#include <linux/moduleparam.h>
#include <linux/wakeup_reason.h>

#include "power.h"
#ifdef CONFIG_MP_CMA_PATCH_MBOOT_STR_USE_CMA
#include <mdrv_miu.h>
#include <mdrv_cma_pool.h>
#endif

#if defined(CONFIG_MSTAR_PM)
#include <mdrv_pm.h>
#endif


#ifdef CONFIG_MP_R2_STR_ENABLE
#include "../../drivers/mstar2/include/mdrv_types.h"
#include "../../drivers/mstar2/include/mdrv_mstypes.h"
#include "../../drivers/mstar2/drv/mbx/mdrv_mbx.h"
#include "../../drivers/mstar2/drv/mbx/mapi_mbx.h"
extern unsigned long get_str_handshake_addr(void);
#endif

#if defined(CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL)
#if defined(CONFIG_MSTAR_CPU_CLUSTER_CALIBRATING)
extern atomic_t ac_str_cpufreq;
extern void change_cpus_timer(char *caller, unsigned int target_freq, unsigned int cpu_id);
extern atomic_t disable_dvfs;
extern void Mdrv_CpuFreq_All_Lock(char *caller);
extern void Mdrv_CpuFreq_All_UnLock(char *caller);
#endif
#endif

#ifdef CONFIG_MSTAR_CHIP
struct task_struct *str_task; // str task
EXPORT_SYMBOL(str_task);
#endif

#if (MP_USB_STR_PATCH==1)
typedef enum
{
	E_STR_NONE,
	E_STR_IN_SUSPEND,
	E_STR_IN_RESUME
} EN_STR_STATUS;

static EN_STR_STATUS enStrStatus = E_STR_NONE;

bool is_suspending(void)
{
	return (enStrStatus == E_STR_IN_SUSPEND);
}
EXPORT_SYMBOL_GPL(is_suspending);
#endif

const char * const pm_labels[] = {
	[PM_SUSPEND_TO_IDLE] = "freeze",
	[PM_SUSPEND_STANDBY] = "standby",
	[PM_SUSPEND_MEM] = "mem",
};
const char *pm_states[PM_SUSPEND_MAX];
static const char * const mem_sleep_labels[] = {
	[PM_SUSPEND_TO_IDLE] = "s2idle",
	[PM_SUSPEND_STANDBY] = "shallow",
	[PM_SUSPEND_MEM] = "deep",
};
const char *mem_sleep_states[PM_SUSPEND_MAX];

suspend_state_t mem_sleep_current = PM_SUSPEND_TO_IDLE;
suspend_state_t mem_sleep_default = PM_SUSPEND_MAX;
suspend_state_t pm_suspend_target_state;
EXPORT_SYMBOL_GPL(pm_suspend_target_state);

unsigned int pm_suspend_global_flags;
EXPORT_SYMBOL_GPL(pm_suspend_global_flags);

static const struct platform_suspend_ops *suspend_ops;
static const struct platform_s2idle_ops *s2idle_ops;
static DECLARE_SWAIT_QUEUE_HEAD(s2idle_wait_head);

enum s2idle_states __read_mostly s2idle_state;
static DEFINE_RAW_SPINLOCK(s2idle_lock);

static struct wakeup_source *suspend_finish_pm_ws = NULL;
static struct timer_list suspend_finish_timer;
#define SUSPEND_FINISH_WAKELOCK_TIME 2*1000

bool pm_suspend_via_s2idle(void)
{
	return mem_sleep_current == PM_SUSPEND_TO_IDLE;
}
EXPORT_SYMBOL_GPL(pm_suspend_via_s2idle);

void s2idle_set_ops(const struct platform_s2idle_ops *ops)
{
	lock_system_sleep();
	s2idle_ops = ops;
	unlock_system_sleep();
}
EXPORT_SYMBOL_GPL(s2idle_set_ops);

static void s2idle_begin(void)
{
	s2idle_state = S2IDLE_STATE_NONE;
}

static void s2idle_enter(void)
{
	trace_suspend_resume(TPS("machine_suspend"), PM_SUSPEND_TO_IDLE, true);

	raw_spin_lock_irq(&s2idle_lock);
	if (pm_wakeup_pending())
		goto out;

	s2idle_state = S2IDLE_STATE_ENTER;
	raw_spin_unlock_irq(&s2idle_lock);

	get_online_cpus();
	cpuidle_resume();

	/* Push all the CPUs into the idle loop. */
	wake_up_all_idle_cpus();
	/* Make the current CPU wait so it can enter the idle loop too. */
	swait_event_exclusive(s2idle_wait_head,
		    s2idle_state == S2IDLE_STATE_WAKE);

	cpuidle_pause();
	put_online_cpus();

	raw_spin_lock_irq(&s2idle_lock);

 out:
	s2idle_state = S2IDLE_STATE_NONE;
	raw_spin_unlock_irq(&s2idle_lock);

	trace_suspend_resume(TPS("machine_suspend"), PM_SUSPEND_TO_IDLE, false);
}

static void s2idle_loop(void)
{
	pm_pr_dbg("suspend-to-idle\n");

	for (;;) {
		int error;

		dpm_noirq_begin();

		/*
		 * Suspend-to-idle equals
		 * frozen processes + suspended devices + idle processors.
		 * Thus s2idle_enter() should be called right after
		 * all devices have been suspended.
		 *
		 * Wakeups during the noirq suspend of devices may be spurious,
		 * so prevent them from terminating the loop right away.
		 */
		error = dpm_noirq_suspend_devices(PMSG_SUSPEND);
		if (!error)
			s2idle_enter();
		else if (error == -EBUSY && pm_wakeup_pending())
			error = 0;

		if (!error && s2idle_ops && s2idle_ops->wake)
			s2idle_ops->wake();

		dpm_noirq_resume_devices(PMSG_RESUME);

		dpm_noirq_end();

		if (error)
			break;

		if (s2idle_ops && s2idle_ops->sync)
			s2idle_ops->sync();

		if (pm_wakeup_pending())
			break;

		pm_wakeup_clear(false);
		clear_wakeup_reasons();
	}

	pm_pr_dbg("resume from suspend-to-idle\n");
}

void s2idle_wake(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&s2idle_lock, flags);
	if (s2idle_state > S2IDLE_STATE_NONE) {
		s2idle_state = S2IDLE_STATE_WAKE;
		swake_up_one(&s2idle_wait_head);
	}
	raw_spin_unlock_irqrestore(&s2idle_lock, flags);
}
EXPORT_SYMBOL_GPL(s2idle_wake);

static bool valid_state(suspend_state_t state)
{
	/*
	 * PM_SUSPEND_STANDBY and PM_SUSPEND_MEM states need low level
	 * support and need to be valid to the low level
	 * implementation, no valid callback implies that none are valid.
	 */
	return suspend_ops && suspend_ops->valid && suspend_ops->valid(state);
}

static void __Suspend_Finish_PM_Wakelock(void)
{
    if(suspend_finish_pm_ws)
    {
        pr_info("%s\n",__func__);
        __pm_stay_awake(suspend_finish_pm_ws);
    }
}

static void __Suspend_Finish_PM_Wakeunlock (struct timer_list *t)
{
    if(suspend_finish_pm_ws)
    {
        pr_info("%s\n",__func__);
        __pm_relax(suspend_finish_pm_ws);
    }
}

void __init pm_states_init(void)
{
	/* "mem" and "freeze" are always present in /sys/power/state. */
	pm_states[PM_SUSPEND_MEM] = pm_labels[PM_SUSPEND_MEM];
	pm_states[PM_SUSPEND_TO_IDLE] = pm_labels[PM_SUSPEND_TO_IDLE];
	/*
	 * Suspend-to-idle should be supported even without any suspend_ops,
	 * initialize mem_sleep_states[] accordingly here.
	 */
	mem_sleep_states[PM_SUSPEND_TO_IDLE] = mem_sleep_labels[PM_SUSPEND_TO_IDLE];
}

static void tmp_hold_suspend_finish_pm_wakelock()
{
    __Suspend_Finish_PM_Wakelock();
    mod_timer(&suspend_finish_timer,jiffies + msecs_to_jiffies(SUSPEND_FINISH_WAKELOCK_TIME));//2*1000
}

static int __init mem_sleep_default_setup(char *str)
{
	suspend_state_t state;

	for (state = PM_SUSPEND_TO_IDLE; state <= PM_SUSPEND_MEM; state++)
		if (mem_sleep_labels[state] &&
		    !strcmp(str, mem_sleep_labels[state])) {
			mem_sleep_default = state;
			break;
		}

	return 1;
}
__setup("mem_sleep_default=", mem_sleep_default_setup);

/**
 * suspend_set_ops - Set the global suspend method table.
 * @ops: Suspend operations to use.
 */
void suspend_set_ops(const struct platform_suspend_ops *ops)
{
	lock_system_sleep();

	suspend_ops = ops;

	if (valid_state(PM_SUSPEND_STANDBY)) {
		mem_sleep_states[PM_SUSPEND_STANDBY] = mem_sleep_labels[PM_SUSPEND_STANDBY];
		pm_states[PM_SUSPEND_STANDBY] = pm_labels[PM_SUSPEND_STANDBY];
		if (mem_sleep_default == PM_SUSPEND_STANDBY)
			mem_sleep_current = PM_SUSPEND_STANDBY;
	}
	if (valid_state(PM_SUSPEND_MEM)) {
		mem_sleep_states[PM_SUSPEND_MEM] = mem_sleep_labels[PM_SUSPEND_MEM];
		if (mem_sleep_default >= PM_SUSPEND_MEM)
			mem_sleep_current = PM_SUSPEND_MEM;
	}

	unlock_system_sleep();
}
EXPORT_SYMBOL_GPL(suspend_set_ops);

/**
 * suspend_valid_only_mem - Generic memory-only valid callback.
 *
 * Platform drivers that implement mem suspend only and only need to check for
 * that in their .valid() callback can use this instead of rolling their own
 * .valid() callback.
 */
int suspend_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}
EXPORT_SYMBOL_GPL(suspend_valid_only_mem);

static bool sleep_state_supported(suspend_state_t state)
{
	return state == PM_SUSPEND_TO_IDLE || (suspend_ops && suspend_ops->enter);
}

static int platform_suspend_prepare(suspend_state_t state)
{
	return state != PM_SUSPEND_TO_IDLE && suspend_ops->prepare ?
		suspend_ops->prepare() : 0;
}

static int platform_suspend_prepare_late(suspend_state_t state)
{
	return state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->prepare ?
		s2idle_ops->prepare() : 0;
}

static int platform_suspend_prepare_noirq(suspend_state_t state)
{
	return state != PM_SUSPEND_TO_IDLE && suspend_ops->prepare_late ?
		suspend_ops->prepare_late() : 0;
}

static void platform_resume_noirq(suspend_state_t state)
{
	if (state != PM_SUSPEND_TO_IDLE && suspend_ops->wake)
		suspend_ops->wake();
}

static void platform_resume_early(suspend_state_t state)
{
	if (state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->restore)
		s2idle_ops->restore();
}

static void platform_resume_finish(suspend_state_t state)
{
	if (state != PM_SUSPEND_TO_IDLE && suspend_ops->finish)
		suspend_ops->finish();
}

static int platform_suspend_begin(suspend_state_t state)
{
	if (state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->begin)
		return s2idle_ops->begin();
	else if (suspend_ops && suspend_ops->begin)
		return suspend_ops->begin(state);
	else
		return 0;
}

#ifdef CONFIG_MP_R2_STR_ENABLE
u32 kernel_read_phys(u64 phys_addr)
{
	u32 phys_addr_page = phys_addr & 0xFFFFE000;
	u32 phys_offset    = phys_addr & 0x00001FFF;
	u32 map_size       = phys_offset + sizeof(u32);
	u32 ret = 0xDEADBEEF;
	void *mem_mapped = ioremap_nocache(phys_addr_page, map_size);
	if (NULL != mem_mapped) {
		ret = (u32)ioread32(((u8*)mem_mapped) + phys_offset);
		iounmap(mem_mapped);
	}

	return ret;
}
#endif

static void platform_resume_end(suspend_state_t state)
{
	if (state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->end)
		s2idle_ops->end();
	else if (suspend_ops && suspend_ops->end)
		suspend_ops->end();
}

static void platform_recover(suspend_state_t state)
{
	if (state != PM_SUSPEND_TO_IDLE && suspend_ops->recover)
		suspend_ops->recover();
}

static bool platform_suspend_again(suspend_state_t state)
{
	return state != PM_SUSPEND_TO_IDLE && suspend_ops->suspend_again ?
		suspend_ops->suspend_again() : false;
}

#ifdef CONFIG_PM_DEBUG
static unsigned int pm_test_delay = 5;
module_param(pm_test_delay, uint, 0644);
MODULE_PARM_DESC(pm_test_delay,
		 "Number of seconds to wait before resuming from suspend test");
#endif

static int suspend_test(int level)
{
#ifdef CONFIG_PM_DEBUG
	if (pm_test_level == level) {
		pr_info("suspend debug: Waiting for %d second(s).\n",
				pm_test_delay);
		mdelay(pm_test_delay * 1000);
		return 1;
	}
#endif /* !CONFIG_PM_DEBUG */
	return 0;
}

/**
 * suspend_prepare - Prepare for entering system sleep state.
 *
 * Common code run for every system sleep state that can be entered (except for
 * hibernation).  Run suspend notifiers, allocate the "suspend" console and
 * freeze processes.
 */
static int suspend_prepare(suspend_state_t state)
{
	int error, nr_calls = 0;

	if (!sleep_state_supported(state))
		return -EPERM;

	pm_prepare_console();

	error = __pm_notifier_call_chain(PM_SUSPEND_PREPARE, -1, &nr_calls);
	if (error) {
		nr_calls--;
		goto Finish;
	}

#if defined(CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL)
#if defined(CONFIG_MSTAR_CPU_CLUSTER_CALIBRATING)
	int i = 0;
	/* Disable DVFS before suspend */
	Mdrv_CpuFreq_All_Lock((char *)__func__);
	atomic_set(&disable_dvfs, 1);
	pr_info("%s(%d): Disable DVFS\n", __func__, __LINE__);
	Mdrv_CpuFreq_All_UnLock((char *)__func__);

	/* Reset cpufreq and voltage to default setting */
	pr_info("%s(%d): setting cpufreq\n", __func__, __LINE__);
	for (i = 0; i < CONFIG_NR_CPUS; i ++)
		change_cpus_timer((char *)__func__, 54472, i);

	mdelay(100);
#endif
#endif

	trace_suspend_resume(TPS("freeze_processes"), 0, true);
	error = suspend_freeze_processes();
	trace_suspend_resume(TPS("freeze_processes"), 0, false);
	if (!error) {
#ifdef CONFIG_MP_CMA_PATCH_MBOOT_STR_USE_CMA
		/* allocate all freed cma_memory from a mboot co-buffer cma_region,
		 * to prevent the kernel data is still @ the mboot co-buffer cma_region,
		 * and thus, the kernel data will be corrupted by mboot
		 */
#ifdef CONFIG_MSTAR_CMAPOOL
		str_reserve_mboot_cma_buffer();
#endif
#endif
		return 0;
	}

	log_suspend_abort_reason("One or more tasks refusing to freeze");
	suspend_stats.failed_freeze++;
	dpm_save_failed_step(SUSPEND_FREEZE);
 Finish:
	__pm_notifier_call_chain(PM_POST_SUSPEND, nr_calls, NULL);

#if defined(CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL)
#if defined(CONFIG_MSTAR_CPU_CLUSTER_CALIBRATING)
	/* Enable DVFS after resume, this is error case */
	Mdrv_CpuFreq_All_Lock((char *)__func__);
	atomic_set(&disable_dvfs, 0);
	pr_info("%s(%d): Enabled DVFS in STR (Error case)\n", __func__, __LINE__);
	Mdrv_CpuFreq_All_UnLock((char *)__func__);
#endif
#endif
	pm_restore_console();

	return error;
}

/* default implementation */
void __weak arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __weak arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

/**
 * suspend_enter - Make the system enter the given sleep state.
 * @state: System sleep state to enter.
 * @wakeup: Returns information that the sleep state should not be re-entered.
 *
 * This function should be called after devices have been suspended.
 */
static int suspend_enter(suspend_state_t state, bool *wakeup)
{
	int error, last_dev;

	error = platform_suspend_prepare(state);
	if (error) {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Platform_finish;
	}

	error = dpm_suspend_late(PMSG_SUSPEND);
	if (error) {
		last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
		last_dev %= REC_FAILED_NUM;
		pr_err("late suspend of devices failed\n");

		log_suspend_abort_reason("late suspend of %s device failed",
					 suspend_stats.failed_devs[last_dev]);
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Platform_finish;
	}

	error = platform_suspend_prepare_late(state);
	if (error) {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Devices_early_resume;
	}

	if (state == PM_SUSPEND_TO_IDLE && pm_test_level != TEST_PLATFORM) {
		s2idle_loop();
		goto Platform_early_resume;
	}

	error = dpm_suspend_noirq(PMSG_SUSPEND);
	if (error) {
		last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
		last_dev %= REC_FAILED_NUM;
		pr_err("noirq suspend of devices failed\n");
		log_suspend_abort_reason("noirq suspend of %s device failed",
					 suspend_stats.failed_devs[last_dev]);
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Platform_early_resume;
	}

	error = platform_suspend_prepare_noirq(state);
	if (error) {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Platform_wake;
	}

	if (suspend_test(TEST_PLATFORM)) {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Platform_wake;
	}

	error = disable_nonboot_cpus();
	if (error || suspend_test(TEST_CPUS)) {
		log_suspend_abort_reason("Disabling non-boot cpus failed");
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Enable_cpus;
	}

	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());

	system_state = SYSTEM_SUSPEND;

	error = syscore_suspend();
	if (!error) {
#ifdef CONFIG_MP_MSTAR_STR_BASE
        if (is_mstar_str()) {
		*wakeup = false;
        } else
#endif
		*wakeup = pm_wakeup_pending();
		if (!(suspend_test(TEST_CORE) || *wakeup)) {
			trace_suspend_resume(TPS("machine_suspend"),
				state, true);
			error = suspend_ops->enter(state);
			trace_suspend_resume(TPS("machine_suspend"),
				state, false);
#ifdef CONFIG_MSTAR_CHIP
			add_timestamp("machine_suspend end");
#endif
#ifdef CONFIG_MP_MSTAR_STR_BASE
			set_state_value(STENT_RESUME_FROM_SUSPEND);
#endif
		} else if (*wakeup) {
			error = -EBUSY;
		}
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		syscore_resume();
	} else {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
	}

	system_state = SYSTEM_RUNNING;

	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

 Enable_cpus:
#ifdef CONFIG_MSTAR_CHIP
	add_timestamp("enable_nonboot_cpus begin");
#endif
	enable_nonboot_cpus();
#ifdef CONFIG_MSTAR_CHIP
	add_timestamp("enable_nonboot_cpus end");
#endif

 Platform_wake:
	platform_resume_noirq(state);
	dpm_resume_noirq(PMSG_RESUME);

 Platform_early_resume:
	platform_resume_early(state);

 Devices_early_resume:
	dpm_resume_early(PMSG_RESUME);

 Platform_finish:
	platform_resume_finish(state);
	return error;
}

/**
 * suspend_devices_and_enter - Suspend devices and enter system sleep state.
 * @state: System sleep state to enter.
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error;
	bool wakeup = false;

	if (!sleep_state_supported(state))
		return -ENOSYS;

	pm_suspend_target_state = state;

	error = platform_suspend_begin(state);
	if (error) {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Close;
	}

	suspend_console();
	suspend_test_start();
#ifdef CONFIG_MSTAR_CHIP
	str_task = current;
#endif
	error = dpm_suspend_start(PMSG_SUSPEND);
	if (error) {
		pr_err("Some devices failed to suspend, or early wake event detected\n");
		log_suspend_abort_reason(
				"Some devices failed to suspend, or early wake event detected");
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES)) {
#if (MP_USB_STR_PATCH==1)
		enStrStatus = E_STR_IN_RESUME;
#endif
		goto Recover_platform;
	}

	do {
		error = suspend_enter(state, &wakeup);
	} while (!error && !wakeup && platform_suspend_again(state));

 Resume_devices:
	suspend_test_start();
	dpm_resume_end(PMSG_RESUME);
	suspend_test_finish("resume devices");
	trace_suspend_resume(TPS("resume_console"), state, true);
#ifdef CONFIG_MSTAR_CHIP
	add_timestamp("resume_console begin");
#endif
	resume_console();
#ifdef CONFIG_MSTAR_CHIP
	add_timestamp("resume_console end");
#endif
	trace_suspend_resume(TPS("resume_console"), state, false);

 Close:
	platform_resume_end(state);
	pm_suspend_target_state = PM_SUSPEND_ON;
	return error;

 Recover_platform:
	platform_recover(state);
	goto Resume_devices;
}

/**
 * suspend_finish - Clean up before finishing the suspend sequence.
 *
 * Call platform code to clean up, restart processes, and free the console that
 * we've allocated. This routine is not called for hibernation.
 */
static void suspend_finish(void)
{
#if defined(CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE) || \
	defined(CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL)
#if (defined CONFIG_MSTAR_CPU_CLUSTER_CALIBRATING)
	/* Enable DVFS after resume, this is error case */
	Mdrv_CpuFreq_All_Lock((char *)__func__);
	atomic_set(&disable_dvfs, 0);
	pr_info("%s(%d): Enabled DVFS in STR\n", __func__, __LINE__);
	Mdrv_CpuFreq_All_UnLock((char *)__func__);
#endif
#endif

#ifdef CONFIG_MP_CMA_PATCH_MBOOT_STR_USE_CMA
	/* free all pre-allocated cma_memory from a mboot co-buffer cma_region */
#ifdef CONFIG_MSTAR_CMAPOOL
	str_release_mboot_cma_buffer();
#endif
#endif
	tmp_hold_suspend_finish_pm_wakelock();
	suspend_thaw_processes();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

#ifdef CONFIG_MP_R2_STR_ENABLE
#define MIU0_BASE               0x20000000
#define STR_FLAG_SUSPEND_FINISH 0xFFFF8888
static void _nuttx_str_notify(void)
{
	volatile unsigned long u64TEESTRBOOTFLAG = get_str_handshake_addr();

	if (u64TEESTRBOOTFLAG != 0) {
		pr_info("%s(%d): TEE mode: Nuttx\n", __func__, __LINE__);
		pr_info("PM: Send MBX to TEE for STR_Suspend  ... \n");
		//1. Setup Suspend Flag to 0
		pr_info("PM: u64TEESTRBOOTFLAG => Addr = 0x%x  !!!!\n",
				u64TEESTRBOOTFLAG);
		pr_info("PM: u64TEESTRBOOTFLAG => Value = 0x%x !!!!\n",
				kernel_read_phys(u64TEESTRBOOTFLAG));
		//2. Send Mailbox to TEE (PA!!!)
		MApi_MBX_NotifyTeetoSuspend(u64TEESTRBOOTFLAG - MIU0_BASE);

		//3. Waiting TEE to finish susepnd jobs
		while (kernel_read_phys(u64TEESTRBOOTFLAG) !=
			STR_FLAG_SUSPEND_FINISH) {
			mdelay(400);
			pr_info("PM: Waiting TEE suspend done signal!!! 0x%x\n",
				kernel_read_phys(u64TEESTRBOOTFLAG));
		}
	} else {
		pr_info("%s(%d): Normal STR flow\n", __func__, __LINE__);
	}
}
#endif

/**
 * enter_state - Do common work needed to enter system sleep state.
 * @state: System sleep state to enter.
 *
 * Make sure that no one else is trying to put the system into a sleep state.
 * Fail if that's not the case.  Otherwise, prepare for system suspend, make the
 * system enter the given sleep state and clean up after wakeup.
 */
static int enter_state(suspend_state_t state)
{
	int error;
#ifdef CONFIG_MP_MSTAR_STR_BASE
	int bresumefromsuspend = 0;
#endif

	trace_suspend_resume(TPS("suspend_enter"), state, true);
	if (state == PM_SUSPEND_TO_IDLE) {
#ifdef CONFIG_PM_DEBUG
		if (pm_test_level != TEST_NONE && pm_test_level <= TEST_CPUS) {
			pr_warn("Unsupported test mode for suspend to idle, please choose none/freezer/devices/platform.\n");
			return -EAGAIN;
		}
#endif
	} else if (!valid_state(state)) {
		return -EINVAL;
	}
	if (!mutex_trylock(&system_transition_mutex))
		return -EBUSY;

	if (state == PM_SUSPEND_TO_IDLE)
		s2idle_begin();

#if defined(CONFIG_MP_MSTAR_STR_BASE)
	set_state_entering();
#if defined(CONFIG_MP_USB_STR_PATCH)
	enStrStatus = E_STR_IN_SUSPEND;
#endif
try_again:
#endif
#ifndef CONFIG_SUSPEND_SKIP_SYNC
	trace_suspend_resume(TPS("sync_filesystems"), 0, true);
	pr_info("Syncing filesystems ... ");
	ksys_sync();
	pr_cont("done.\n");
	trace_suspend_resume(TPS("sync_filesystems"), 0, false);
#endif

	pm_pr_dbg("Preparing system for sleep (%s)\n", mem_sleep_labels[state]);
	pm_suspend_clear_flags();
	error = suspend_prepare(state);
	if (error)
		goto Unlock;

#ifdef CONFIG_MP_R2_STR_ENABLE
	if (TEEINFO_TYPTE == SECURITY_TEEINFO_OSTYPE_NUTTX)
		_nuttx_str_notify();
#endif
	if (suspend_test(TEST_FREEZER))
		goto Finish;

	trace_suspend_resume(TPS("suspend_enter"), state, false);
	pm_pr_dbg("Suspending system (%s)\n", mem_sleep_labels[state]);
	pm_restrict_gfp_mask();
	error = suspend_devices_and_enter(state);
	pm_restore_gfp_mask();

 Finish:
	events_check_enabled = false;
	pm_pr_dbg("Finishing wakeup.\n");
#if defined(CONFIG_MP_MSTAR_STR_BASE)
	if(STENT_RESUME_FROM_SUSPEND == get_state_value()) {
		clear_state_entering();
		bresumefromsuspend=1;
	}
#endif
	suspend_finish();
 Unlock:
#if defined(CONFIG_MP_MSTAR_STR_BASE)
#if defined(CONFIG_MSTAR_STR_ACOFF_ON_ERR)
	if(error) {
		extern void mstar_str_notifypmerror_off(void);
		mstar_str_notifypmerror_off(); //it won't return, wait pm to power off
	}
#endif

	if (is_mstar_str() && bresumefromsuspend == 0) {
		schedule_timeout_interruptible(HZ);
		goto try_again;
	}
#if (MP_USB_STR_PATCH==1)
	enStrStatus = E_STR_NONE;
#endif
#endif
	mutex_unlock(&system_transition_mutex);

	return error;
}

/**
 * pm_suspend - Externally visible function for suspending the system.
 * @state: System sleep state to enter.
 *
 * Check if the value of @state represents one of the supported states,
 * execute enter_state() and update system suspend statistics.
 */
int pm_suspend(suspend_state_t state)
{
	int error;

	if (state <= PM_SUSPEND_ON || state >= PM_SUSPEND_MAX)
		return -EINVAL;

	pr_info("suspend entry (%s)\n", mem_sleep_labels[state]);
	error = enter_state(state);
	if (error) {
		suspend_stats.fail++;
		dpm_save_failed_errno(error);
	} else {
		suspend_stats.success++;
	}
	pr_info("suspend exit\n");
	return error;
}
EXPORT_SYMBOL(pm_suspend);

static int __init suspend_finish_pm_ws_register(void)
{
	//add wake lock for kernel_suspend_finish -> kernel_suspend_prepare
	if(suspend_finish_pm_ws == NULL){
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 105)
		suspend_finish_pm_ws = wakeup_source_register(NULL, "kernel_pm_ws");
	#else
		suspend_finish_pm_ws = wakeup_source_register("kernel_pm_ws");
	#endif
	}
	timer_setup(&suspend_finish_timer, __Suspend_Finish_PM_Wakeunlock, 0);
	return 0;
}
late_initcall(suspend_finish_pm_ws_register);
