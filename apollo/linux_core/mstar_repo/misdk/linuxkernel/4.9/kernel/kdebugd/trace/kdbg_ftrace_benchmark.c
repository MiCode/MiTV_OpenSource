/*
 *  kdbg_ftrace_benchmark.c
 *  Copyright (C) 2010 Samsung Electronics
 *  Created by umesh.tiwari (umesh.t@samsung.com)
 *
 *  NOTE:
 */

#include <linux/kernel.h>
#include <kdebugd.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kallsyms.h>
#include <linux/ftrace.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include "kdbg_util.h"
#include <trace/kdbg_ftrace_helper.h>
#include <trace/kdbg-ftrace.h>

#include <linux/fs.h>
#include <linux/uaccess.h>

/* default ftrace benchmark configuration values */
struct kdbg_ftrace_benchmark_conf bconf = {
	NULL,
	NULL,
	FTRACE_BENCH_ITERATIONS,
	FTRACE_BENCH_COUNT,
	E_FTRACE_BENCHMARK_IDLE,
	0,
	{0}
};

/* kernel thread to benchmark
 * 1. Function Tracer
 * 2. Function graph Tracer
 * 3. Sched_Switch Tracer
 * 4. Stack Tracer
 * 5. Branch Tracer
 * 6. Blk Tracer
 * 7. Function profile
 * 8. Kernel backtrace Tracer
 * 9. User backtrace Tracer
 */
int trace_benchmark_thread(void *data)
{
	struct kdbg_ftrace_conf *pfconf = &fconf;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
	struct timespec ts1, ts2, ts;
	struct timespec avg_ts;
	long i;
	int ctr = 0;
	struct file *fp;
	char *file_name = NULL;
	char buffer[] = "hello!!!World";
	mm_segment_t oldfs;
	int ret = 0;
	s64 total;
	volatile int cond = 1;
	int file_error = 0;

	ssleep(1);

	/*init average timespec to*/
	memset(&avg_ts, 0, sizeof(struct timespec));

	if (!strcmp(pbconf->trace_name, "blk"))
		file_name = TRACER_BLK_FILE_NAME;
	else
		file_name = TRACER_FILE_NAME;


	menu("\n");
	info("Benchmark Started.");
	menu("\n------------------------------------");
	menu("\nTracer      : \"%s\"", pbconf->trace_name);
	switch (pfconf->trace_state) {
	case E_FTRACE_TRACE_STATE_IDLE:
		menu("\nTrace State : \"Idle\".");
		break;
	case E_FTRACE_TRACE_STATE_START:
		menu("\nTrace State : \"Running\".");
		break;
	case E_FTRACE_TRACE_STATE_STOP:
		menu("\nTrace State : \"Stopped\".");
		break;
	case E_FTRACE_TRACE_STATE_DUMP:
		menu("\nTrace State : \"Dump\".");
		break;
	default:
		break;
	}

	menu("\n------------------------------------");
	menu("\nIterations      Time");
	menu("\n------------------------------------");
	if (strcmp(pbconf->trace_name, "branch")) {

		/* Kernel memory access setting */
		oldfs = get_fs();
		set_fs(KERNEL_DS);

		while (ctr++ < pbconf->count && !kthread_should_stop()) {
			do_posix_clock_monotonic_gettime(&ts1);
			for (i = pbconf->iterations; i; i--) {
				fp = filp_open(file_name, O_CREAT | O_TRUNC | O_RDWR , 0600);
				if (IS_ERR(fp)) {
					error("fp NULL\n");
					file_error = 1;
					break;
				}

				ret = fp->f_op->write(fp, buffer, strlen(buffer),  &fp->f_pos);
				if (ret < 0) {
					filp_close(fp, NULL);
					file_error = 1;
					break;
				}

				/* close file */
				filp_close(fp, NULL);
			}

			/* file open error(read/write) get out of while loop*/
			if (file_error)
				break;

			do_posix_clock_monotonic_gettime(&ts2);
			ts = timespec_sub(ts2, ts1);
			menu("\n%ld\t   %ld.%09ld s", pbconf->iterations,
					ts.tv_sec, ts.tv_nsec);
			/*add timespec to get average*/
			avg_ts = timespec_add(avg_ts, ts);
		}

		/* restore kernel memory setting */
		set_fs(oldfs);

	} else {
		while (ctr++ < pbconf->count && !kthread_should_stop()) {
			do_posix_clock_monotonic_gettime(&ts1);
			for (i = pbconf->iterations; i; i--) {
				if (likely(cond))
					cond = 0;
				else
					cond = 1;
			}
			do_posix_clock_monotonic_gettime(&ts2);
			ts = timespec_sub(ts2, ts1);
			menu("\n%ld\t   %ld.%09ld s", pbconf->iterations,
					ts.tv_sec, ts.tv_nsec);

			/*add timespec to get average*/
			avg_ts = timespec_add(avg_ts, ts);

			ssleep(1);
		}
	}

	if (ctr - 1 > 0) {

		/*convert timespec to nanoseconds*/
		total = timespec_to_ns(&avg_ts);

		/*devide total nanoseconds with count*/
		total = div_s64(total, ctr - 1);

		/*get average timespec from nanoseconds*/
		avg_ts = ns_to_timespec(total);

		menu("\n------------------------------------");
		menu("\nAverage ==>\t%ld.%09ld s", avg_ts.tv_sec, avg_ts.tv_nsec);
		menu("\n------------------------------------");
		menu("\n");

	}

	while (!kthread_should_stop())
		msleep(1);

	return 0;
}

/* kernel thread to benchmark latency tracers
 * 1. Irqsoff Tracer
 * 2. Preemptoff Tracer
 * 3. wakeup Tracer
 */
int trace_benchmark_latency_thread(void *data)
{
	struct kdbg_ftrace_conf *pfconf = &fconf;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
	struct timespec ts1, ts2, ts;
	struct timespec avg_ts;
	long i;
	int ctr = 0;
	s64 total;

	ssleep(1);

	/*init average timespec to*/
	memset(&avg_ts, 0, sizeof(struct timespec));

	menu("\n");
	info("Benchmark Started.");
	menu("\n------------------------------------");
	menu("\nTracer      : \"%s\"", pbconf->trace_name);
	switch (pfconf->trace_state) {
	case E_FTRACE_TRACE_STATE_IDLE:
		menu("\nTrace State : \"Idle\".");
		break;
	case E_FTRACE_TRACE_STATE_START:
		menu("\nTrace State : \"Running\".");
		break;
	case E_FTRACE_TRACE_STATE_STOP:
		menu("\nTrace State : \"Stopped\".");
		break;
	case E_FTRACE_TRACE_STATE_DUMP:
		menu("\nTrace State : \"Dump\".");
		break;
	default:
		break;
	}
	menu("\n------------------------------------");
	menu("\nIterations      Time");
	menu("\n------------------------------------");

	if (!strcmp(pbconf->trace_name, "irqsoff")) {
		while (ctr++ < pbconf->count && !kthread_should_stop()) {
			do_posix_clock_monotonic_gettime(&ts1);
			for (i = pbconf->iterations; i; i--) {
				local_irq_disable();
				local_irq_enable();
			}
			do_posix_clock_monotonic_gettime(&ts2);
			ts = timespec_sub(ts2, ts1);
			menu("\n%ld\t   %ld.%09ld s", pbconf->iterations,
					ts.tv_sec, ts.tv_nsec);

			/*add timespec to get average*/
			avg_ts = timespec_add(avg_ts, ts);

			ssleep(1);
		}


	} else if (!strcmp(pbconf->trace_name, "preemptoff")) {

		while (ctr++ < pbconf->count && !kthread_should_stop()) {
			do_posix_clock_monotonic_gettime(&ts1);
			for (i = pbconf->iterations; i; i--) {
				preempt_disable();
				preempt_enable();
			}
			do_posix_clock_monotonic_gettime(&ts2);
			ts = timespec_sub(ts2, ts1);
			menu("\n%ld\t   %ld.%09ld s", pbconf->iterations,
					ts.tv_sec, ts.tv_nsec);

			/*add timespec to get average*/
			avg_ts = timespec_add(avg_ts, ts);

			ssleep(1);
		}

	} else if (!strcmp(pbconf->trace_name, "wakeup")) {
		set_current_state(TASK_INTERRUPTIBLE);
		while (!kthread_should_stop()) {
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);
		return 0;
	}

	if (ctr - 1 > 0) {

		/*convert timespec to nenoseconds*/
		total = timespec_to_ns(&avg_ts);

		/*devide total nenoseconds with count*/
		total = div_s64(total, ctr - 1);

		/*get average timespec from nenoseconds*/
		avg_ts = ns_to_timespec(total);

		menu("\n------------------------------------");
		menu("\nAverage ==>\t%ld.%09ld s", avg_ts.tv_sec, avg_ts.tv_nsec);
		menu("\n------------------------------------");
		menu("\n");

	}

	while (!kthread_should_stop())
		msleep(1);

	return 0;
}

/*
 * kernel thread to benchmark Memory Tracer
 */
int trace_benchmark_memory_thread(void *data)
{
	struct kdbg_ftrace_conf *pfconf = &fconf;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
	struct timespec ts1, ts2, ts;
	struct timespec avg_ts;
	long i;
	int ctr = 0;
	s64 total;
	char *q;
	int malloc_error = 0;

	ssleep(1);

	/*init average timespec to*/
	memset(&avg_ts, 0, sizeof(struct timespec));

	menu("\n");
	info("Benchmark Started.");
	menu("\n------------------------------------");
	menu("\nTracer      : \"%s\"", pbconf->trace_name);
	switch (pfconf->trace_state) {
	case E_FTRACE_TRACE_STATE_IDLE:
		menu("\nTrace State : \"Idle\".");
		break;
	case E_FTRACE_TRACE_STATE_START:
		menu("\nTrace State : \"Running\".");
		break;
	case E_FTRACE_TRACE_STATE_STOP:
		menu("\nTrace State : \"Stopped\".");
		break;
	case E_FTRACE_TRACE_STATE_DUMP:
		menu("\nTrace State : \"Dump\".");
		break;
	default:
		break;
	}
	menu("\n------------------------------------");
	menu("\nIterations      Time");
	menu("\n------------------------------------");

	while (ctr++ < pbconf->count && !kthread_should_stop()) {
		do_posix_clock_monotonic_gettime(&ts1);
		for (i = pbconf->iterations; i; i--) {
			q = kmalloc(10*4096, GFP_KERNEL);
			if (q == NULL) {
				error(" kmalloc failed\n");
				malloc_error = 1;
				break;
			}
			memset(q, 0, 10*4096);
			kfree(q);
			q = NULL;

		}

		/* error in malloc get out of while loop*/
		if (malloc_error)
			break;
		do_posix_clock_monotonic_gettime(&ts2);
		ts = timespec_sub(ts2, ts1);
		menu("\n%ld\t   %ld.%09ld s", pbconf->iterations,
				ts.tv_sec, ts.tv_nsec);

		/*add timespec to get average*/
		avg_ts = timespec_add(avg_ts, ts);
	}

	if (ctr - 1 > 0) {
		/*convert timespec to nenoseconds*/
		total = timespec_to_ns(&avg_ts);

		/*devide total nenoseconds with count*/
		total = div_s64(total, ctr - 1);

		/*get average timespec from nenoseconds*/
		avg_ts = ns_to_timespec(total);

		menu("\n------------------------------------");
		menu("\nAverage ==>\t%ld.%09ld s", avg_ts.tv_sec, avg_ts.tv_nsec);
		menu("\n------------------------------------");
		menu("\n");

	}

	while (!kthread_should_stop())
		msleep(1);

	return 0;
}

/*
 * kernel thread to benchmark Event(timer) Tracer
 */
static struct hrtimer event_timer;
static struct timespec tv = {.tv_sec = 1, .tv_nsec = 0};
static enum hrtimer_restart event_timer_handler(struct hrtimer *ht)
{
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
	struct timespec ts1, ts2, ts, total_ts;
	long i;
	static struct timespec avg_ts;
	s64 total;

	/*init average timespec to*/
	if (!pbconf->time_count)
		memset(&avg_ts, 0, sizeof(struct timespec));

	memset(&total_ts, 0, sizeof(struct timespec));

	for (i = pbconf->iterations; i; i--) {
		do_posix_clock_monotonic_gettime(&ts1);
		hrtimer_forward(&event_timer, ktime_get(), timespec_to_ktime(tv));
		do_posix_clock_monotonic_gettime(&ts2);
		ts = timespec_sub(ts2, ts1);

		/*add timespec to get average*/
		total_ts = timespec_add(total_ts, ts);

	}

	if (pbconf->time_count++ < pbconf->count) {
		avg_ts = timespec_add(avg_ts, total_ts);
		menu("\n%ld\t   %ld.%09ld s", pbconf->iterations, total_ts.tv_sec, total_ts.tv_nsec);
	}

	if (pbconf->time_count > 0) {
		if (pbconf->time_count - 1 == pbconf->count) {
			/*convert timespec to nenoseconds*/
			total = timespec_to_ns(&avg_ts);

			/*devide total nenoseconds with count*/
			total = div_s64(total, (pbconf->time_count - 1));

			/*get average timespec from nenoseconds*/
			avg_ts = ns_to_timespec(total);

			menu("\n------------------------------------");
			menu("\nAverage ==>\t%ld.%09ld s", avg_ts.tv_sec, avg_ts.tv_nsec);
			menu("\n------------------------------------");
			menu("\n");
		}
	}

	return HRTIMER_RESTART;
}

/*
 * Kernel thread to start timer event.
 */
int trace_benchmark_event_thread(void *data)
{
	struct kdbg_ftrace_conf *pfconf = &fconf;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;

	ssleep(1);

	menu("\n");
	info("Benchmark Started.");
	menu("\n------------------------------------");
	menu("\nTracer ==> \"%s\"(timer)", pbconf->trace_name);
	switch (pfconf->trace_state) {
	case E_FTRACE_TRACE_STATE_IDLE:
		menu("\nTrace State ==> \"Idle\".");
		break;
	case E_FTRACE_TRACE_STATE_START:
		menu("\nTrace State ==> \"Running\".");
		break;
	case E_FTRACE_TRACE_STATE_STOP:
		menu("\nTrace State ==> \"Stopped\".");
		break;
	case E_FTRACE_TRACE_STATE_DUMP:
		menu("\nTrace State ==> \"Dump\".");
		break;
	default:
		break;
	}
	menu("\n------------------------------------");
	menu("\nIterations      Time");
	menu("\n------------------------------------");

	/*start hrtimer*/
	hrtimer_init(&event_timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL);
	event_timer._softexpires = timespec_to_ktime(tv);
	event_timer.function = event_timer_handler;
	hrtimer_start(&event_timer,
			event_timer._softexpires,
			HRTIMER_MODE_REL);

	while (!kthread_should_stop())
		msleep(1);

	/*cancel hrtimer*/
	hrtimer_cancel(&event_timer);
	pbconf->time_count = 0;

	return 0;
}

/*
 * kernel thread to benchmark wakeup tracer
 */
int trace_wakeup_thread(void *data)
{
	struct timespec ts1, ts2, ts;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
	struct timespec avg_ts, total_ts;
	long i;
	int ctr = 0;
	s64 total;

	ssleep(1);

	memset(&avg_ts, 0, sizeof(struct timespec));
	memset(&total_ts, 0, sizeof(struct timespec));

	while (ctr++ < pbconf->count && !kthread_should_stop()) {
		if (pbconf->bench_th != NULL) {
			for (i = pbconf->iterations; i; i--) {
				do_posix_clock_monotonic_gettime(&ts1);
				wake_up_process(pbconf->bench_th);
				do_posix_clock_monotonic_gettime(&ts2);
				ts = timespec_sub(ts2, ts1);

				/*add timespec to get average*/
				total_ts = timespec_add(total_ts, ts);
			}
			avg_ts = timespec_add(avg_ts, total_ts);
			menu("\n%ld\t  %ld.%09ld s", pbconf->iterations, total_ts.tv_sec, total_ts.tv_nsec);
		}
		memset(&total_ts, 0, sizeof(struct timespec));
		ssleep(1);
	}

	if (ctr - 1 > 0) {
		/*convert timespec to nenoseconds*/
		total = timespec_to_ns(&avg_ts);

		/*devide total nenoseconds with count*/
		total = div_s64(total, ctr - 1);

		/*get average timespec from nenoseconds*/
		avg_ts = ns_to_timespec(total);

		menu("\n------------------------------------");
		menu("\naverage ==>\t%ld.%09ld s", avg_ts.tv_sec, avg_ts.tv_nsec);
		menu("\n------------------------------------");
		menu("\n");

	}

	while (!kthread_should_stop())
		msleep(1);

	return 0;
}

/*
 * Set Benchmark configutaion
 * 1. Iterations
 * 2. Count.
 */
void handle_ftrace_benchmark_config(void)
{
	int operation = 0;
	int event = 0;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;

	do {
		menu("\n");
		menu("-------------------------------------------------------------------\n");
		menu("Ftrace Benchmark: Set Configuration Menu.\n");
		menu("-------------------------------------------------------------------\n");
		menu("1)  Ftrace Benchmark: Set Iterations.\n");
		menu("2)  Ftrace Benchmark: Set Count.\n");
		menu("-------------------------------------------------------------------\n");
		menu("99) Ftrace Benchmark: Configuration Exit Menu.\n");
		menu("-------------------------------------------------------------------\n");
		menu("\n");
		menu("Select Option==>  ");

		operation = debugd_get_event_as_numeric(NULL, NULL);
		menu("\n");
		switch (operation) {
		case E_SUBMENU_BENCHMARK_CONFIG_SET_ITER:
			{
				menu("Iterations (Default 100000, Max 1000000) ==> ");
				event = debugd_get_event_as_numeric(NULL, NULL);
				if (event <= 0 || event < 1000 ||
					event > FTRACE_BENCH_ITERATIONS_MAX) {
					menu("\n");
					warn("Invalid Iterations, [Range (>= 1000 && <= FTRACE_BENCH_ITERATIONS_MAX(%d))].\n",
							FTRACE_BENCH_ITERATIONS_MAX);
					break;
				}
				/*set benchmark iterations*/
				pbconf->iterations = event;
				menu("\n");
				info("Set Iterations (%ld) Done.\n", pbconf->iterations);
				break;
			}
		case E_SUBMENU_BENCHMARK_CONFIG_SET_COUNT:
			{
				menu("Count (Default 10, Max 20)==> ");
				event = debugd_get_event_as_numeric(NULL, NULL);
				if (event <= 0 ||
						event > FTRACE_BENCH_COUNT_MAX) {
					menu("\n");
					warn("Invalid Count, [Range (> 0 && <= FTRACE_BENCH_COUNT_MAX(%d))].\n",
						FTRACE_BENCH_COUNT_MAX);
					break;
				}
				/*set benchmark count*/
				pbconf->count = event;
				menu("\n");
				info("Set Count(%d) Done.\n", pbconf->count);
				break;
			}
		default:
			break;
		}
	} while (operation != E_SUBMENU_BENCHMARK_CONFIG_EXIT);

}
