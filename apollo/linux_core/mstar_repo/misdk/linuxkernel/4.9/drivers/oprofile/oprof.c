/**
 * @file oprof.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <mstar/mpatch_macro.h>

#include "oprof.h"
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprofile_stats.h"

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
#include <linux/slab.h>
#include <kdebugd/kdebugd.h>
#include <linux/dcookies.h>
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/

struct oprofile_operations oprofile_ops;

unsigned long oprofile_started;
unsigned long oprofile_backtrace_depth;
static unsigned long is_setup;
static DEFINE_MUTEX(start_mutex);

/* timer
   0 - use performance monitoring hardware if available
   1 - use the timer int mechanism regardless
 */
#if (defined(CONFIG_ADVANCE_OPROFILE) && !defined(CONFIG_CACHE_ANALYZERi)) && (MP_DEBUG_TOOL_OPROFILE == 1)
static int timer = 1;
#else
static int timer = 0;
#endif /*CONFIG_ADVANCE_OPROFILE && CONFIG_CACHE_ANALYZER && MP_DEBUG_TOOL_OPROFILE*/

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
#define AOP_BUFFER_SIZE_DEFAULT     131072
#define AOP_CPU_BUFFER_SIZE_DEFAULT     8192
#define AOP_BUFFER_WATERSHED_DEFAULT    32768

static atomic_t g_oprofile_driver_init;
/* the fucntion return the status of oprofile init*/
int aop_is_oprofile_init (void)
{
	return atomic_read (&g_oprofile_driver_init);
}
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /* MP_DEBUG_TOOL_OPROFILE */

int oprofile_setup(void)
{
	int err;

	mutex_lock(&start_mutex);

	if ((err = alloc_cpu_buffers()))
		goto out;

	if ((err = alloc_event_buffer()))
		goto out1;

	if (oprofile_ops.setup && (err = oprofile_ops.setup()))
		goto out2;

	/* Note even though this starts part of the
	 * profiling overhead, it's necessary to prevent
	 * us missing task deaths and eventually oopsing
	 * when trying to process the event buffer.
	 */
	if (oprofile_ops.sync_start) {
		int sync_ret = oprofile_ops.sync_start();
		switch (sync_ret) {
		case 0:
			goto post_sync;
		case 1:
			goto do_generic;
		case -1:
			goto out3;
		default:
			goto out3;
		}
	}
do_generic:
	if ((err = sync_start()))
		goto out3;

post_sync:
	is_setup = 1;
	mutex_unlock(&start_mutex);
	return 0;

out3:
	if (oprofile_ops.shutdown)
		oprofile_ops.shutdown();
out2:
	free_event_buffer();
out1:
	free_cpu_buffers();
out:
	mutex_unlock(&start_mutex);
	return err;
}

#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX

static void switch_worker(struct work_struct *work);
static DECLARE_DELAYED_WORK(switch_work, switch_worker);

static void start_switch_worker(void)
{
	if (oprofile_ops.switch_events)
		schedule_delayed_work(&switch_work, oprofile_time_slice);
}

static void stop_switch_worker(void)
{
	cancel_delayed_work_sync(&switch_work);
}

static void switch_worker(struct work_struct *work)
{
	if (oprofile_ops.switch_events())
		return;

	atomic_inc(&oprofile_stats.multiplex_counter);
	start_switch_worker();
}

/* User inputs in ms, converts to jiffies */
int oprofile_set_timeout(unsigned long val_msec)
{
	int err = 0;
	unsigned long time_slice;

	mutex_lock(&start_mutex);

	if (oprofile_started) {
		err = -EBUSY;
		goto out;
	}

	if (!oprofile_ops.switch_events) {
		err = -EINVAL;
		goto out;
	}

	time_slice = msecs_to_jiffies(val_msec);
	if (time_slice == MAX_JIFFY_OFFSET) {
		err = -EINVAL;
		goto out;
	}

	oprofile_time_slice = time_slice;

out:
	mutex_unlock(&start_mutex);
	return err;

}

#else

static inline void start_switch_worker(void) { }
static inline void stop_switch_worker(void) { }

#endif

/* Actually start profiling (echo 1>/dev/oprofile/enable) */
int oprofile_start(void)
{
	int err = -EINVAL;

	mutex_lock(&start_mutex);

	if (!is_setup)
		goto out;

	err = 0;

	if (oprofile_started)
		goto out;

	oprofile_reset_stats();

	if ((err = oprofile_ops.start()))
		goto out;

	start_switch_worker();

	oprofile_started = 1;
out:
	mutex_unlock(&start_mutex);
	return err;
}


/* echo 0>/dev/oprofile/enable */
void oprofile_stop(void)
{
	mutex_lock(&start_mutex);
	if (!oprofile_started)
		goto out;
	oprofile_ops.stop();
	oprofile_started = 0;

	stop_switch_worker();

	/* wake up the daemon to read what remains */
	wake_up_buffer_waiter();
out:
	mutex_unlock(&start_mutex);
}


void oprofile_shutdown(void)
{
	mutex_lock(&start_mutex);
	if (oprofile_ops.sync_stop) {
		int sync_ret = oprofile_ops.sync_stop();
		switch (sync_ret) {
		case 0:
			goto post_sync;
		case 1:
			goto do_generic;
		default:
			goto post_sync;
		}
	}
do_generic:
	sync_stop();
post_sync:
	if (oprofile_ops.shutdown)
		oprofile_ops.shutdown();
	is_setup = 0;
	free_event_buffer();
	free_cpu_buffers();
	mutex_unlock(&start_mutex);
}

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
void oprofile_set_backtrace(unsigned long val)
{
	int retval;

	retval = oprofile_set_ulong(&oprofile_backtrace_depth, val);

	return;
}
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/

int oprofile_set_ulong(unsigned long *addr, unsigned long val)
{
	int err = -EBUSY;

	mutex_lock(&start_mutex);
	if (!oprofile_started) {
		*addr = val;
		err = 0;
	}
	mutex_unlock(&start_mutex);

	return err;
}

static int timer_mode;

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
void aop_dcookie_release(void *dcookie_user_data)
{
	dcookie_unregister(dcookie_user_data);
}


int kdebugd_aop_oprofile_init(void)
{
	struct aop_register *reg_ptr = NULL;
	reg_ptr = aop_alloc();
	if (reg_ptr) {
		reg_ptr->aop_is_oprofile_init = aop_is_oprofile_init;
		reg_ptr->aop_oprofile_start = oprofile_start;
		reg_ptr->aop_oprofile_stop = oprofile_stop;
		reg_ptr->aop_event_buffer_open = aop_event_buffer_open;
		reg_ptr->aop_read_event_buffer = aop_read_event_buffer;
		reg_ptr->aop_event_buffer_clear = aop_event_buffer_clear;
		reg_ptr->aop_clear_cpu_buffers = aop_clear_cpu_buffers;
		reg_ptr->aop_wake_up_buffer_waiter = wake_up_buffer_waiter;
		reg_ptr->aop_dcookie_release = aop_dcookie_release;
		reg_ptr->aop_get_pc_sample_count = aop_get_pc_sample_count;
		reg_ptr->aop_reset_pc_sample_count = aop_reset_pc_sample_count;
		reg_ptr->aop_oprofile_buffer_size = oprofile_buffer_size;
		return 0;
	} else {
		printk(KERN_INFO "AOP:AOP Init failed.\n");
		return -1;
	}
}

void kdebugd_aop_oprofile_exit(void)
{
	aop_dealloc();
}

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
int aop_get_sampling_mode(void)
{
	if (!oprofile_ops.cpu_type)
		return INVALID_SAMPLING_MODE;

	return strcmp(oprofile_ops.cpu_type, "timer") ?
			PERF_EVENTS_SAMPLING : TIMER_SAMPLING;
}

int aop_switch_to_timer_sampling(void)
{
	int ret = aop_get_sampling_mode();

	if (ret == TIMER_SAMPLING)
		return 0;

	if (ret == PERF_EVENTS_SAMPLING)
		oprofile_arch_exit();

	/* else previous mode was invalid sampling mode */
	ret = oprofile_timer_init(&oprofile_ops);

	return ret;
}

int aop_switch_to_perf_events_sampling(void)
{
	int ret = aop_get_sampling_mode();

	if (ret == PERF_EVENTS_SAMPLING)
		return 0;

	/* else previous mode was invalid sampling mode */
	ret = oprofile_arch_init(&oprofile_ops);

	/* memory is already freed in case of error */
	return ret;

}
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/

static int __init oprofile_init(void)
{
	int err;

	/* always init architecture to setup backtrace support */
	timer_mode = 0;
	err = oprofile_arch_init(&oprofile_ops);
	if (!err) {
		if (!timer && !oprofilefs_register()) {
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
			/* for ARCH mode */
			oprofile_buffer_size      = AOP_BUFFER_SIZE_DEFAULT;
			oprofile_cpu_buffer_size  = AOP_CPU_BUFFER_SIZE_DEFAULT;
			oprofile_buffer_watershed = AOP_BUFFER_WATERSHED_DEFAULT;

			if (!kdebugd_aop_oprofile_init()) {
				atomic_set (&g_oprofile_driver_init, 1);
				printk ("AOP:Oprofile Driver init done\n");
			}
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/
			return 0;
		}
		oprofile_arch_exit();
	}

	/* setup timer mode: */
	timer_mode = 1;
	/* no nmi timer mode if oprofile.timer is set */
	if (timer || op_nmi_timer_init(&oprofile_ops)) {
		err = oprofile_timer_init(&oprofile_ops);
		if (err)
			return err;
	}

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
	/* called only when timer mode is set successfully */
	oprofile_buffer_size      = AOP_BUFFER_SIZE_DEFAULT;
	oprofile_cpu_buffer_size  = AOP_CPU_BUFFER_SIZE_DEFAULT;
	oprofile_buffer_watershed = AOP_BUFFER_WATERSHED_DEFAULT;

	if (!kdebugd_aop_oprofile_init()) {
		atomic_set (&g_oprofile_driver_init, 1);
		printk ("AOP:Oprofile Driver init done in timer mode\n");
	}
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /* MP_DEBUG_TOOL_OPROFILE */

	return oprofilefs_register();
}


static void __exit oprofile_exit(void)
{
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_ADVANCE_OPROFILE
	aop_release();
	kdebugd_aop_oprofile_exit();
#endif /* CONFIG_ADVANCE_OPROFILE */
#endif /*MP_DEBUG_TOOL_OPROFILE*/
	oprofilefs_unregister();
	if (!timer_mode)
		oprofile_arch_exit();
}


module_init(oprofile_init);
module_exit(oprofile_exit);

module_param_named(timer, timer, int, 0644);
MODULE_PARM_DESC(timer, "force use of timer interrupt");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Levon <levon@movementarian.org>");
MODULE_DESCRIPTION("OProfile system profiler");
