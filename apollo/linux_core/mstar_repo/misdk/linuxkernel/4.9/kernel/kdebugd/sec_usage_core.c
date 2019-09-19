/*
 *  linux/kernel/sec_usage_core.c
 *
 *  Copyright (C) 2009 Samsung Electronics
 *
 *  2009-11-05  Created by Choi Young-Ho (yh2005.choi@samsung.com)
 *
 *  Counter Monitor  kusage_cored  register_counter_monitor_func
 *
 *  Disk Usage hrtimer Mutex wait
 *  scheduling while atomic
 */

#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <kdebugd.h>

#define MAX_COUNTER_MONITOR	10

wait_queue_head_t usage_cored_wq;

static counter_monitor_func_t counter_monitor_funcs[MAX_COUNTER_MONITOR] = {NULL};

int kdbg_usage_core_init(void);

/*
 * Name: register_counter_monitor_func
 * Desc: Counter Monitor
 */
int register_counter_monitor_func(counter_monitor_func_t func)
{
	int i;
	static int kdbg_core_init_flag;

	if (func == NULL) {
		PRINT_KD("No Function Pointer specified\n");
		return -1;
	}

	/* Check flag for Initialization */
	if (!kdbg_core_init_flag) {
		/* This is the first fucntion to register.
		 * Usage core should Initialized first,
		 * no need to initialize it at boot time, initialize at
		 * time the of use*/
		kdbg_usage_core_init();
		kdbg_core_init_flag = 1;
	}

	for (i = 0; i < MAX_COUNTER_MONITOR - 1; ++i) {
		if (counter_monitor_funcs[i] == NULL) {
			counter_monitor_funcs[i] = func;
			return 0;
		}
	}

	PRINT_KD("full of counter_monitor_funcs\n");
	return -1;
}

/*
 * Name: usage_cored
 * Desc: Counter Monitor
 */
static int usage_cored(void *p)
{
	int rc = 0;

/*	daemonize("kusage_cored");*/

	while (1) {
		counter_monitor_func_t *funcp = &counter_monitor_funcs[0];

		while (*funcp != NULL) {
			rc = (*funcp++) ();
		}
		wait_event_interruptible_timeout(usage_cored_wq, 0, 1 * HZ);	/* sleep 1 sec */
	}

	BUG();

	return 0;
}

/*
 * Name: kdbg_usage_core_init
 * Desc: kusage_cored
 */
int kdbg_usage_core_init(void)
{
	static int init_flag;
	pid_t pid;
	struct task_struct *sec_usage_core_tsk = NULL;

	BUG_ON(init_flag);
	init_waitqueue_head(&usage_cored_wq);

	/* changing state to running is done by thread function, if
	 * success in creating thread */
	sec_usage_core_tsk = kthread_create(usage_cored, NULL, "sec_usage_core_thread");
	if (IS_ERR(sec_usage_core_tsk)) {
		sec_usage_core_tsk = NULL;
		printk("sec_usage_core_thread: bp_thread Thread Creation\n");
		return -1;
	}

	/* update task flag and wakeup process */
	sec_usage_core_tsk->flags |= PF_NOFREEZE;
	wake_up_process(sec_usage_core_tsk);

	pid = sec_usage_core_tsk->pid;
	BUG_ON(pid < 0);

	init_flag = 1;

	return 0;
}
