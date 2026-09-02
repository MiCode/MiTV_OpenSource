// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/power/autosleep.c
 *
 * Opportunistic sleep support.
 *
 * Copyright (C) 2012 Rafael J. Wysocki <rjw@sisk.pl>
 */

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>

#include "power.h"

static suspend_state_t autosleep_state;
static struct workqueue_struct *autosleep_wq;
/*
 * Note: it is only safe to mutex_lock(&autosleep_lock) if a wakeup_source
 * is active, otherwise a deadlock with try_to_suspend() is possible.
 * Alternatively mutex_lock_interruptible() can be used.  This will then fail
 * if an auto_sleep cycle tries to freeze processes.
 */
static DEFINE_MUTEX(autosleep_lock);
static struct wakeup_source *autosleep_ws;

static void try_to_suspend(struct work_struct *work)
{
	unsigned int initial_count, final_count;

	if (!pm_get_wakeup_count(&initial_count, true))
		goto out;

	mutex_lock(&autosleep_lock);

	if (!pm_save_wakeup_count(initial_count) ||
		system_state != SYSTEM_RUNNING) {
		mutex_unlock(&autosleep_lock);
		goto out;
	}

	if (autosleep_state == PM_SUSPEND_ON) {
		mutex_unlock(&autosleep_lock);
		return;
	}
	if (autosleep_state >= PM_SUSPEND_MAX)
		hibernate();
	else
		pm_suspend(autosleep_state);

	mutex_unlock(&autosleep_lock);

	if (!pm_get_wakeup_count(&final_count, false))
		goto out;

	/*
	 * If the wakeup occured for an unknown reason, wait to prevent the
	 * system from trying to suspend and waking up in a tight loop.
	 */
	if (final_count == initial_count)
		schedule_timeout_uninterruptible(HZ / 2);

 out:
	queue_up_suspend_work();
        /*
         * wait to prevent the  system from trying to suspend and waking up in a tight loop.
         */
        schedule_timeout_uninterruptible(HZ / 2);
}

static DECLARE_WORK(suspend_work, try_to_suspend);

void queue_up_suspend_work(void)
{
	if (autosleep_state > PM_SUSPEND_ON)
		queue_work(autosleep_wq, &suspend_work);
}

suspend_state_t pm_autosleep_state(void)
{
	return autosleep_state;
}

int pm_autosleep_lock(void)
{
	return mutex_lock_interruptible(&autosleep_lock);
}

void pm_autosleep_unlock(void)
{
	mutex_unlock(&autosleep_lock);
}



/*
 * pm cust
 */
#ifdef CONFIG_PM_CUST
static volatile bool ret = false;
#ifdef CONFIG_NATIVE_CB2
typedef unsigned int	UINT32;
extern void PDWNC_PowerStateNotify(UINT32 u4PowerState, UINT32 u4Arg1,
				   UINT32 u4Arg2, UINT32 u4Arg3, UINT32 u4Arg4);
#endif

static struct workqueue_struct *pm_cust_wq;
static struct wakeup_source *pm_cust_ws;

static int __init pm_cust_init(void)
{

	pm_cust_ws = wakeup_source_register(NULL, "Mstar.pm_cust");
	if (!pm_cust_ws)
		return -ENOMEM;

	pm_cust_wq = create_singlethread_workqueue("pm_cust");
	if (pm_cust_wq)
		return 0;

	wakeup_source_unregister(pm_cust_ws);
	return -ENOMEM;
}
core_initcall(pm_cust_init);

static void pre_autosleep(struct work_struct *work)
{
	printk(KERN_DEBUG "pm_cust: start pre_autosleep work\n");

#ifdef CONFIG_NATIVE_CB2
	PDWNC_PowerStateNotify(0,0,0,0,0);
#endif

	__pm_relax(pm_cust_ws);
	printk(KERN_DEBUG "pm_cust: end pre_autosleep work\n");
}
static DECLARE_WORK(pre_autosleep_work, pre_autosleep);

/**
 * 1. accquire a wakesource
 * 2. queue up pre_autosleep work
 * 3. the wakesource will be relaxed at pre_autosleep work end
 */
void pm_cust_pre_autosleep(void)
{
	printk(KERN_DEBUG "pm_cust: do pre_autosleep queue_work start.\n");
	__pm_stay_awake(pm_cust_ws);

	do{
        ret = queue_work(pm_cust_wq, &pre_autosleep_work);
    }
    while(ret == 0);
	printk(KERN_DEBUG "pm_cust: do pre_autosleep queue_work end, ret=%d.\n",ret);
}

static void post_autosleep(struct work_struct *work)
{
	printk(KERN_DEBUG "pm_cust: start post_autosleep work\n");

#ifdef CONFIG_NATIVE_CB2
	PDWNC_PowerStateNotify(1,0,0,0,0);
#endif
	printk(KERN_DEBUG "pm_cust: end post_autosleep work\n");
}
static DECLARE_WORK(post_autosleep_work, post_autosleep);

void pm_cust_post_autosleep(void)
{
	printk(KERN_DEBUG "pm_cust: do post_autosleep queue_work start.\n");
	do{
    	ret = queue_work(pm_cust_wq, &post_autosleep_work);
    }
    while(ret == 0);
	printk(KERN_DEBUG "pm_cust: do post_autosleep queue_work end, ret=%d.\n",ret);
}

#else

void pm_cust_pre_autosleep(void)
{
}
void pm_cust_post_autosleep(void)
{
}

#endif /* CONFIG_PM_CUST */

int pm_autosleep_set_state(suspend_state_t state)
{
	suspend_state_t old_state;

#ifndef CONFIG_HIBERNATION
	if (state >= PM_SUSPEND_MAX)
		return -EINVAL;
#endif

	__pm_stay_awake(autosleep_ws);

	mutex_lock(&autosleep_lock);

	old_state = autosleep_state;
	autosleep_state = state;

	__pm_relax(autosleep_ws);

	/* ON -> MEM, do pre_autosleep */
	if (old_state == PM_SUSPEND_ON && state == PM_SUSPEND_MEM)
		pm_cust_pre_autosleep();

	if (state > PM_SUSPEND_ON) {
		pm_wakep_autosleep_enabled(true);
		queue_up_suspend_work();
	} else {
		pm_wakep_autosleep_enabled(false);
	}

	/* MEM -> ON, do post_autosleep */
	if (old_state == PM_SUSPEND_MEM && state == PM_SUSPEND_ON)
		pm_cust_post_autosleep();

	mutex_unlock(&autosleep_lock);
	return 0;
}

int __init pm_autosleep_init(void)
{
	autosleep_ws = wakeup_source_register(NULL, "Mstar.autosleep");
	if (!autosleep_ws)
		return -ENOMEM;

	autosleep_wq = alloc_ordered_workqueue("autosleep", 0);
	if (autosleep_wq)
		return 0;

	wakeup_source_unregister(autosleep_ws);
	return -ENOMEM;
}
