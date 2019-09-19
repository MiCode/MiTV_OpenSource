/*
 *  kernel/kdebugd/sec_perfcounters.c
 *
 *  Performance Counters Profiling Solution
 *
 *  Copyright (C) 2011  Samsung
 *
 *  2011-05-31  Created by Vladimir Podyapolskiy
 *
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/kobj_map.h>
#include <linux/buffer_head.h>
#include <linux/mutex.h>

#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>

#include <linux/proc_fs.h>
#include "kdebugd/sec_perfcounters.h"
#include <linux/perf_event.h>
#include "kdebugd/kdebugd.h"
#include "kdbg_util.h"

#define SEC_PMU_EVENTS_NR_ENTRIES	CONFIG_KDEBUGD_PMU_EVENTS_BUFFER_SIZE

struct sec_perf_groups {
	int id;
	int active;
	unsigned char *name;
};

static struct sec_perf_groups perf_groups[] = {
#if defined(CONFIG_KDEBUGD_PMU_EVENTS_L1D_CACHE_MISS_COUNTER) || \
		defined(CONFIG_KDEBUGD_PMU_EVENTS_L1I_CACHE_MISS_COUNTER)
	{
		.id = 0,
		.name = "L1 Miss ",
	},
#endif
#if defined(CONFIG_KDEBUGD_PMU_EVENTS_DTLB_MISS_COUNTER) || \
		defined(CONFIG_KDEBUGD_PMU_EVENTS_ITLB_MISS_COUNTER)
	{
		.id = 1,
		.name = "TLB Miss",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_BPU_MISS_COUNTER
	{
		.id = 2,
		.name = "BPU Miss",
	},
#endif
};

#define NUM_PERF_GROUPS	\
		(sizeof(perf_groups) / sizeof(struct sec_perf_groups))

/* Description of desired counters */
struct sec_perf_types {
	u64 config;
	u32 type;
	int group_id;
	unsigned char *name;
	unsigned int active; /* We assume all CPUs has
				identical number of emplemented counters */
	unsigned int available;
};

static struct sec_perf_types perf_types[] = {
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_CPU_CYCLES_COUNTER
	{
		.type = PERF_TYPE_HARDWARE,
		.group_id = -1,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.name = "CPU cycl",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_INSTRUCTIONS_COUNTER
	{
		.type = PERF_TYPE_HARDWARE,
		.group_id = -1,
		.config = PERF_COUNT_HW_INSTRUCTIONS,
		.name = "instrts ",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_L1D_CACHE_MISS_COUNTER
	{
		.type = PERF_TYPE_HW_CACHE,
		.group_id = 0,
		.config = PERF_COUNT_HW_CACHE_L1D  |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
		.name = "L1D Miss",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_L1I_CACHE_MISS_COUNTER
	{
		.type = PERF_TYPE_HW_CACHE,
		.group_id = 0,
		.config = PERF_COUNT_HW_CACHE_L1I  |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
		.name = "L1I Miss",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_LL_CACHE_MISS_COUNTER
	{
		.type = PERF_TYPE_HW_CACHE,
		.group_id = -1,
		.config = PERF_COUNT_HW_CACHE_LL  |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
		.name = "L2 Miss ",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_DTLB_MISS_COUNTER
	{
		.type = PERF_TYPE_HW_CACHE,
		.group_id = 1,
		.config = PERF_COUNT_HW_CACHE_DTLB |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
		.name = "DTLB Mis",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_ITLB_MISS_COUNTER
	{
		.type = PERF_TYPE_HW_CACHE,
		.group_id = 1,
		.config = PERF_COUNT_HW_CACHE_ITLB |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
		.name = "ITLB Mis",
	},
#endif
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_BPU_MISS_COUNTER
	{
		.type = PERF_TYPE_HW_CACHE,
		.group_id = 2,
		.config = PERF_COUNT_HW_CACHE_BPU  |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
		.name = "BPU Miss",
	}
#endif
};

#define NUM_PERF_COUNTERS	\
		(sizeof(perf_types) / sizeof(struct sec_perf_types))

/* The buffer that will store cache usage data. */
struct sec_perfstats_struct {
	unsigned long sec;
	unsigned long long val[NR_CPUS][NUM_PERF_COUNTERS];
};

static struct sec_perfstats_struct
		stat_buffer[SEC_PMU_EVENTS_NR_ENTRIES];

static int perfcounters_init_flag;
/*
 * Variable that will keep track of the performance counter
 * selected by the user. A value of -1 means all counters
 * selected by default
 */
static int perfcounter_num;

/* Usage mode of counters */
static enum {
	GLOBAL_COUNTERS,
	PER_THREAD_COUNTERS
} perfcounters_type = GLOBAL_COUNTERS;

/* Type of statistic's visualization */
static enum {
	SUMMARY_OUTPUT,
	DETAIL_OUTPUT
} display_type = SUMMARY_OUTPUT;

/* check whether state is running or not..*/
static int perfcounters_run_state;

static int func_register;

/* Buffer Index */
static unsigned int perfcounters_idx;

/* Which CPU's counters to dump */
static int cpu_to_dump = -1;

static int num_active_counters_per_cpu;

/* Performance Counters description */
struct sec_perfcounters_t {
	struct perf_event *event;
	struct sec_perf_types *type;
};
static struct sec_perfcounters_t perfcounters[NR_CPUS][NUM_PERF_COUNTERS];

/* Mutex lock variable to save data structure while printing gnuplot data*/
DEFINE_MUTEX(perfdump_lock);

/*
 * Turn ON the performance counters processing.
 */
static bool sec_perfcounters_init(void);

/*
 * The flag which incates whether the buffer array is full(value is 1)
 * or is partially full(value is 0).
 */
static int perfcounters_is_buffer_full;

/*
 * The flag which will be turned on or off when sysrq feature will
 * turn on or off respectively.
 * 1 = ON
 * 0 = OFF
 */
static int perfcounters_status;

/* Check separator necessity */
#define CHECK_SEPARATOR(c)						\
{									\
	if (cpu_to_dump == -1 && cpu_current < (num_online_cpus() - 1))	\
		PRINT_KD("%c ", c);					\
	cpu_current++;							\
}

/*
 * Check that cpu is requested CPU to dump if we dump only one
 * specific CPU statistics
 */
#define CHECK_MISS_CPU(cpu)						\
	do {								\
		if (cpu_to_dump != -1 && cpu != cpu_to_dump)		\
			break;						\
	} while (0);

/* Check that requested CPU is dumped already and break loop */
#define CHECK_END_CPU_LOOP(cpu)						\
	do {								\
		if (cpu_to_dump != -1 && cpu == cpu_to_dump)		\
			break;						\
	} while (0);

#define repeat_char(c, num)						\
{									\
	int cnt = (int)(num);						\
	while (cnt-- > 0)						\
		PRINT_KD("%c", (c));					\
}

static void show_summary_header(void)
{
	int i, cpu, cpu_current;

	/* Print CPUs line */
	repeat_char(' ', 5);
	cpu_current = 0;
	for_each_online_cpu(cpu) {
		CHECK_MISS_CPU(cpu);
		repeat_char(' ', (NUM_PERF_GROUPS / 2) * 9);
		PRINT_KD("  CPU%d   ", cpu);
		repeat_char(' ',
			(NUM_PERF_GROUPS * 9) - 9 - (NUM_PERF_GROUPS / 2) * 9);
		CHECK_END_CPU_LOOP(cpu);
		CHECK_SEPARATOR(' ');
	}
	PRINT_KD("\n");

	/* Print connections between CPUs and counter names */
	repeat_char(' ', 5);
	cpu_current = 0;
	for_each_online_cpu(cpu) {
		CHECK_MISS_CPU(cpu);
		for (i = 0; i < NUM_PERF_GROUPS / 2; i++)
			PRINT_KD("       / ");
		PRINT_KD("    |    ");
		for (i = NUM_PERF_GROUPS / 2 + 1; i < NUM_PERF_GROUPS; i++)
			PRINT_KD("\\        ");
		CHECK_END_CPU_LOOP(cpu);
		CHECK_SEPARATOR(' ');
	}
	PRINT_KD("\n");

	PRINT_KD("Time ");
	cpu_current = 0;
	for_each_online_cpu(cpu) {
		CHECK_MISS_CPU(cpu);
		for (i = 0; i < NUM_PERF_GROUPS; i++)
			PRINT_KD("%s ", perf_groups[i].name);
		CHECK_END_CPU_LOOP(cpu);
		CHECK_SEPARATOR('|');
	}
	PRINT_KD("\n==== ");
	cpu_current = 0;
	for_each_online_cpu(cpu) {
		CHECK_MISS_CPU(cpu);
		for (i = 0; i < NUM_PERF_GROUPS; i++)
			PRINT_KD("======== ");
		CHECK_END_CPU_LOOP(cpu);
		CHECK_SEPARATOR('|');
	}
}

#define for_each_active_counter(i) \
	for (i = 0; i < NUM_PERF_COUNTERS; i++) if (perf_types[i].active)

static void show_detail_header(void)
{
	int i, k;
	int cpu, cpu_current;

	if (num_active_counters_per_cpu <= 0)
		return;

	if (perfcounter_num == -1) {
		if (perfcounters_type != PER_THREAD_COUNTERS) {
			/* Output CPUs */
			PRINT_KD("%*c", 5, ' ');

			cpu_current = 0;
			for_each_online_cpu(cpu) {
				CHECK_MISS_CPU(cpu);
				repeat_char(' ', NUM_PERF_COUNTERS * 9 - 9);
				PRINT_KD("CPU%d", cpu);
				repeat_char(' ', 5);
				CHECK_END_CPU_LOOP(cpu);
				CHECK_SEPARATOR('|');
			}
			PRINT_KD("\n");
		}

		/* Output links to counter types */
		for (i = 0; i < NUM_PERF_COUNTERS; i++) {
			if (perfcounters_type == PER_THREAD_COUNTERS)
				repeat_char(' ', 5 + 6)
			else
				repeat_char(' ', 5);

			cpu_current = 0;
			for_each_online_cpu(cpu) {
				CHECK_MISS_CPU(cpu);
				/* Output preceding '|' */
				for (k = 0; k < i - 1; k++) {
					repeat_char(' ', 9 / 2 - 1);
					PRINT_KD("|");
					repeat_char(' ', 9 - 9 / 2);
				}

				/* Output following '/' */
				if (i > 0) {
					repeat_char(' ', 9 / 2 - 1);
					PRINT_KD("/");
					repeat_char(' ', 9 - 9 / 2);
				}
				if (i < NUM_PERF_COUNTERS - 1) {
					repeat_char(' ', 9 / 2 + 1);
					PRINT_KD("_");
					repeat_char('-',
						(NUM_PERF_COUNTERS - i)*9-2*9);
					PRINT_KD("=> ");
				}
				PRINT_KD("%s ", perf_types[i].name);

			/* Break loop for per-process dump */
				if (perfcounters_type == PER_THREAD_COUNTERS)
					break;

				CHECK_END_CPU_LOOP(cpu);
				CHECK_SEPARATOR('|');
			}
			PRINT_KD("\n");
		}
	} else {
		if (perfcounters_type != PER_THREAD_COUNTERS) {
			/* Output CPUs */
				PRINT_KD("%*c", 5, ' ');

				cpu_current = 0;
				for_each_online_cpu(cpu) {
					CHECK_MISS_CPU(cpu);
					repeat_char(' ', 2);
					PRINT_KD(" CPU%d ", cpu);
					repeat_char(' ', 3);
					CHECK_END_CPU_LOOP(cpu);
					CHECK_SEPARATOR('|');
				}
				PRINT_KD("\n");
		}
		if (perfcounters_type == PER_THREAD_COUNTERS)
			repeat_char(' ', 5 + 6)
		else
			repeat_char(' ', 5);

		cpu_current = 0;
		for_each_online_cpu(cpu) {
			CHECK_MISS_CPU(cpu);
			/* Output preceding '|' */
			PRINT_KD(" %s  ", perf_types[perfcounter_num].name);

			/* Break loop for per-process dump */
			if (perfcounters_type == PER_THREAD_COUNTERS)
				break;

			CHECK_END_CPU_LOOP(cpu);
			CHECK_SEPARATOR('|');
		}
		PRINT_KD("\n");
	}

	if (perfcounters_type == PER_THREAD_COUNTERS)
		PRINT_KD("Time  PID  ");
	else
		PRINT_KD("Time ");

	cpu_current = 0;
	for_each_online_cpu(cpu) {
		CHECK_MISS_CPU(cpu);
		if (perfcounter_num == -1) {
			for (i = 0; i < NUM_PERF_COUNTERS - 1; i++) {
				repeat_char(' ', 9 / 2 - 1);
				PRINT_KD("|");
				repeat_char(' ', 9 - 9 / 2);
			}
			repeat_char(' ', 9 / 2 - 1);
			PRINT_KD("/");
			repeat_char(' ', 9 - 9 / 2);
		} else {
			repeat_char(' ', 11 / 2 - 1);
			PRINT_KD("|");
			repeat_char(' ', 11 - 11 / 2);
		}
		if (perfcounters_type == PER_THREAD_COUNTERS)
			break;
		CHECK_END_CPU_LOOP(cpu);
		CHECK_SEPARATOR('|');
	}
	if (perfcounters_type == PER_THREAD_COUNTERS)
		PRINT_KD("    task\n");
	else
		PRINT_KD("\n");

	if (perfcounters_type == PER_THREAD_COUNTERS)
		PRINT_KD("==== ===== ");
	else
		PRINT_KD("==== ");

	cpu_current = 0;
	for_each_online_cpu(cpu) {
		CHECK_MISS_CPU(cpu);
		if (perfcounter_num == -1) {
			for (i = 0; i < NUM_PERF_COUNTERS; i++)
				PRINT_KD("======== ");
		} else {
			PRINT_KD("========== ");
		}
		if (perfcounters_type == PER_THREAD_COUNTERS)
				break;

		CHECK_END_CPU_LOOP(cpu);
		CHECK_SEPARATOR('|');
	}

	if (perfcounters_type == PER_THREAD_COUNTERS)
		repeat_char('=', 16);
}

/* This function is to show header */
static void perfcounters_show_header(void)
{
	switch (perfcounters_type) {
	case GLOBAL_COUNTERS:
		switch (display_type) {
		case SUMMARY_OUTPUT:
			show_summary_header();
			break;
		case DETAIL_OUTPUT:
			show_detail_header();
		};
		break;

	case PER_THREAD_COUNTERS:
		show_detail_header();
	}
	PRINT_KD("\n");
}

/* Dump all counters corresponding to one buffer line and to one CPU */
static void dump_perf_result_one_cpu_line(
		unsigned int buffer_idx, unsigned int output_idx, int cpu)
{
	unsigned int i, j, prev_idx =
				(buffer_idx - 1) % SEC_PMU_EVENTS_NR_ENTRIES;
	unsigned long long prev_value = 0, value;

	switch (display_type) {
	case SUMMARY_OUTPUT:
		for (i = 0; i < NUM_PERF_GROUPS; i++) {
			if (!perf_groups[i].active) {
				PRINT_KD("       - ");
				continue;
			}
			/* Calculate previous value */
			prev_value = 0;
			if (output_idx == 0) {
				if (perfcounters_is_buffer_full) {
					prev_idx =
						SEC_PMU_EVENTS_NR_ENTRIES - 1;
					for_each_active_counter(j) {
						if (perf_types[j].group_id ==
							perf_groups[i].id) {
							prev_value +=
								stat_buffer[
							prev_idx].val[cpu][j];
						}
					}
				}
			} else {
				for_each_active_counter(j) {
					if (perf_types[j].group_id ==
						perf_groups[i].id) {
						prev_value +=
							stat_buffer[
							prev_idx].val[cpu][j];
					}
				}
			}

			/* Calculate current value */
			value = 0;
			for_each_active_counter(j) {
				if (perf_types[j].group_id ==
						perf_groups[i].id) {
					value += stat_buffer[
						buffer_idx].val[cpu][j];
				}
			}
			PRINT_KD("%8lld ", value - prev_value);
		}
		break;
	case DETAIL_OUTPUT:
		if (perfcounter_num == -1)
			for (i = 0; i < NUM_PERF_COUNTERS; i++)	{
				unsigned long current_val;

				if (!perf_types[i].available) {
					PRINT_KD("       - ");
					continue;
				}
				if (perfcounters_is_buffer_full) {
					if (buffer_idx
						== 0) {
						prev_idx =
						SEC_PMU_EVENTS_NR_ENTRIES - 1;
						prev_value =
							stat_buffer[
							prev_idx].val[cpu][i];
					} else
						prev_value = stat_buffer[
							prev_idx].val[cpu][i];
				} else if (output_idx == 0)
					prev_value = 0;
				else
					prev_value = stat_buffer[
						prev_idx].val[cpu][i];

				/* actual counters on the target are only 32-bit.
				 * with 64-bit, we can't tell that counter has wrapped
				 */
				current_val = (unsigned long) ((long)stat_buffer[buffer_idx].val[cpu][i] -
						(long)prev_value);
				/* value should fit in 8 places, otherwise
				 * whole menu becomes very big
				 */
				if (current_val > 99999999) {
					/* show in <>k */
					current_val = current_val/1000;
					PRINT_KD("%7luk ", current_val);
				} else {
				PRINT_KD("%8lu ", current_val);
				}
			}
		else {
			/*
			 * If the counter is not available, just print "-"
			 * don't try to read the counter value.
			 * Issue was seen when streamline and kdebugd
			 * both try to use CCNT
			 */
			if (!perf_types[perfcounter_num].available) {
				PRINT_KD("       -   ");
				break;
			}
			if (perfcounters_is_buffer_full) {
				if (buffer_idx
					== 0) {
					prev_idx =
						SEC_PMU_EVENTS_NR_ENTRIES - 1;
					prev_value =
						stat_buffer[
							prev_idx].val[
							cpu][perfcounter_num];
				} else
					prev_value = stat_buffer[
							prev_idx].val[
							cpu][perfcounter_num];
			} else if (output_idx == 0)
				prev_value = 0;
			else
				prev_value = stat_buffer[
					prev_idx].val[cpu][perfcounter_num];

			/*
			 * actual counters on the target are only 32-bit.
			 * with 64-bit, we can't tell that counter has wrapped
			 */
			PRINT_KD("%10lu ",
			(unsigned long)((long)stat_buffer[buffer_idx].val[cpu][perfcounter_num] -
			(long)prev_value));
		}
	}
}

/* Dump the buffered data of counters from the buffer.
 * It prints history of counters values (max 120 sec).
 */
static void dump_perf_result(void)
{
	int i;
	int buffer_count = 0;
	unsigned int idx = 0;
	int cpu;
	int cpu_current;

	perfcounters_show_header();

	mutex_lock(&perfdump_lock);

	if (perfcounters_is_buffer_full) {
		buffer_count = SEC_PMU_EVENTS_NR_ENTRIES;
		idx = perfcounters_idx;
	} else {
		buffer_count = perfcounters_idx;
		idx = 0;
	}

	for (i = 0; i < buffer_count; i++, idx++) {
		idx = idx % SEC_PMU_EVENTS_NR_ENTRIES;
		if (!idx || i) {
			PRINT_KD("%4ld ", stat_buffer[idx].sec);
			cpu_current = 0;
			for_each_online_cpu(cpu) {
				CHECK_MISS_CPU(cpu);
				dump_perf_result_one_cpu_line(idx, i, cpu);
				CHECK_END_CPU_LOOP(cpu);
				CHECK_SEPARATOR('|');
			}
			PRINT_KD("\n");
		}
	}
	mutex_unlock(&perfdump_lock);
}

/* Dump current state of counters every seconds */
int sec_perfcounters_dump(void)
{
	u64 enabled, running;
	struct perf_event *event;
	struct task_struct *g, *t;
	unsigned int index;
	int cpu;
	int first;
	int cpu_current;
	int j;

	/* for automatic start at the boot time .. */
#ifdef CONFIG_SEC_PMU_EVENTS_AUTO_START
	static int boot = 1; /* only once at the time of bootup */
#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	static int boot_init;
	static int time;
#endif

	if (boot) {
#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
		if ((!boot_init) && time >= CONFIG_COUNTER_MON_START_SEC) {
			perfcounters_run_state = 1;
			boot_init = 1;
		}
		if (boot_init && (time >= CONFIG_COUNTER_MON_FINISHED_SEC
					|| (perfcounters_run_state == 0))) {
			perfcounters_run_state = 0;
			boot = 0;
			if (perfcounters_init_flag)
				sec_perfcounters_destroy();
		}
		time++;
#else
		perfcounters_run_state = 1;
		boot = 0;
#endif
	}
#endif

	if (!perfcounters_init_flag || !perfcounters_run_state)
		return 0;

	index = (perfcounters_idx % SEC_PMU_EVENTS_NR_ENTRIES);

	mutex_lock(&perfdump_lock);

	switch (perfcounters_type) {
	case GLOBAL_COUNTERS:
		stat_buffer[index].sec = kdbg_get_uptime();
		cpu_current = 0;
		if (perfcounters_status)
			PRINT_KD("%4ld ",
				stat_buffer[index].sec);

		for_each_online_cpu(cpu) {
			CHECK_MISS_CPU(cpu);
			for_each_active_counter(j) {
				stat_buffer[index].val[cpu][j] =
					perf_event_read_value(
						perfcounters[cpu][j].event,
						&enabled, &running);
			}
			if (perfcounters_status) {
				dump_perf_result_one_cpu_line(
						index, index, cpu);
				CHECK_SEPARATOR('|');
			}
			CHECK_END_CPU_LOOP(cpu);
		}
		if (perfcounters_status)
			PRINT_KD("\n");
		break;

	case PER_THREAD_COUNTERS:
		perfcounters_show_header();
		first = 1;

		/*
		 * If there is no active counter, print once.
		 * There can be many useless prints, otherwise
		 */
		if (!num_active_counters_per_cpu) {
			PRINT_KD("%4ld ", kdbg_get_uptime());
			PRINT_KD(" -");
			break;
		}

		do_each_thread(g, t) {
			if (unlikely(first)) {
				PRINT_KD("%4ld ", kdbg_get_uptime());
				first = 0;
			} else
				PRINT_KD("     ");
			PRINT_KD("%5d ", (int)t->pid);

			if ((t->perf_event_ctxp[perf_hw_context] == NULL)) {
				PRINT_KD("-- No event for %s, ", t->comm);
				PRINT_KD("skip rest threads for this group\n");
				break;
			}

			mutex_lock(&t->perf_event_mutex);
			j = 0;
			/* TODO: check what type of context we want to show */
			list_for_each_entry_reverse(event,
					&t->perf_event_ctxp[perf_hw_context]->event_list,
					event_entry)
				while (j < NUM_PERF_COUNTERS) {
					if (perf_types[j++].available) {
						PRINT_KD("%8lld ",
							perf_event_read_value(
								event,
								&enabled,
								&running));
						break;
					} else{
						PRINT_KD("       - ");
						continue;
					}
				}
			mutex_unlock(&t->perf_event_mutex);

			PRINT_KD("%s\n", t->comm);
		} while_each_thread(g, t);
		PRINT_KD("\n");
	}

	if (perfcounters_type == GLOBAL_COUNTERS &&
			++perfcounters_idx == SEC_PMU_EVENTS_NR_ENTRIES)
		perfcounters_is_buffer_full = 1;

	mutex_unlock(&perfdump_lock);

	return 1;
}

/* Create required counters */
static void create_counters(struct perf_event_attr *attr, int cpu,
	struct task_struct *task, struct sec_perfcounters_t result[NR_CPUS][NUM_PERF_COUNTERS])
{
	int i;
	struct perf_event *event;

	num_active_counters_per_cpu = 0;

	for (i = 0; i < NUM_PERF_GROUPS; i++)
		perf_groups[i].active = 0;
	if (perfcounter_num < 0) {
		for (i = 0; i < NUM_PERF_COUNTERS; i++) {
			attr->config = perf_types[i].config;
			attr->type = perf_types[i].type;
			event = perf_event_create_kernel_counter(attr,
							cpu, task, NULL, NULL);
			if (IS_ERR(event) || event->state ==
					PERF_EVENT_STATE_ERROR) {
				perf_types[i].active = 0;
				perf_types[i].available = 0;
				if (cpu != -1)
					PRINT_KD("can't create %s"
						" counter on CPU%d\n",
						perf_types[i].name, cpu);
				else
					PRINT_KD("can't create %s"
						" counter for PID %d\n",
						perf_types[i].name, task->pid);
			} else {
				num_active_counters_per_cpu++;
				if (perf_types[i].group_id >= 0)
					perf_groups[perf_types[
						i].group_id].active = 1;
				perf_types[i].active = 1;
				perf_types[i].available = 1;
				if (result) {
					result[cpu][i].event = event;
					result[cpu][i].type = &perf_types[i];
				}
			}
		}
	} else {
		attr->config = perf_types[perfcounter_num].config;
		attr->type = perf_types[perfcounter_num].type;
		event = perf_event_create_kernel_counter(attr, cpu, task, NULL, NULL);
		if (IS_ERR(event) || event->state == PERF_EVENT_STATE_ERROR) {
			perf_types[perfcounter_num].active = 0;
			perf_types[perfcounter_num].available = 0;
			if (cpu != -1)
				PRINT_KD("can't create %s"
					" counter on CPU%d\n",
					perf_types[perfcounter_num].name, cpu);
			else
				PRINT_KD("can't create %s"
					" counter for PID %d\n",
					perf_types[perfcounter_num].name, task->pid);
		} else {
			num_active_counters_per_cpu++;
			if (perf_types[perfcounter_num].group_id >= 0)
				perf_groups[perf_types[
					perfcounter_num].group_id].active = 1;
			perf_types[perfcounter_num].active = 1;
			perf_types[perfcounter_num].available = 1;
			if (result) {
				result[cpu][perfcounter_num].event = event;
				result[cpu][perfcounter_num].type =
						&perf_types[perfcounter_num];
			}
		}
	}
}

/* Initialize required perf events */
bool sec_perfcounters_init(void)
{
	struct perf_event_attr attr;
	struct task_struct *g, *t;
	int cpu, i;

	perfcounters_is_buffer_full = 0;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.pinned = 1;
	attr.inherit = 1;
	attr.inherit_indep = 1;

	switch (perfcounters_type) {
	case GLOBAL_COUNTERS:
		/* Create counters for chosen CPUs */
		for_each_online_cpu(cpu) {
			CHECK_MISS_CPU(cpu);
			create_counters(&attr, cpu, NULL, perfcounters);
			CHECK_END_CPU_LOOP(cpu);
		}
		break;

	case PER_THREAD_COUNTERS:
		/* Mark all counters as available before activation */
		for (i = 0; i < NUM_PERF_COUNTERS; i++)
			perf_types[i].available = 1;

		/* Create counters for each process */
		do_each_thread(g, t) {
			create_counters(&attr, -1, t, NULL);
		} while_each_thread(g, t);
	}

	perfcounters_init_flag = 1;

	if (!func_register) {
		register_counter_monitor_func(sec_perfcounters_dump);
		func_register = 1;
	}
	return true;
}

void sec_perfcounters_destroy(void)
{
	struct task_struct *g, *t;
	struct perf_event *event;
	int j, cpu;

	perfcounters_run_state = 0;

	mutex_lock(&perfdump_lock);

	if (perfcounters_init_flag) {

		switch (perfcounters_type) {
		case GLOBAL_COUNTERS:
			for_each_online_cpu(cpu)
				for_each_active_counter(j)
					if (perfcounters[cpu][j].event) {
						perf_event_release_kernel(
							perfcounters[cpu]
								[j].event);
						perfcounters[cpu][j].event = 0;
					}
			break;

		case PER_THREAD_COUNTERS:
			do_each_thread(g, t) {
				if (t->perf_event_ctxp[perf_hw_context] == NULL)
					continue;

			/* TODO: check what type of context we want to show */
				list_for_each_entry(
						event,
						&t->perf_event_ctxp[perf_hw_context]->event_list,
						event_entry)
					perf_event_release_kernel(event);
			} while_each_thread(g, t);
		}
		for (j = 0; j < NUM_PERF_COUNTERS; j++)
			perf_types[j].active = 0;
		perfcounters_init_flag = 0;

		PRINT_KD("Destroyed Successfuly\n");
	}

	mutex_unlock(&perfdump_lock);
}

void get_perfcounters_status(void)
{
	if (perfcounters_init_flag) {

		PRINT_KD("Initialized        ");
		if (perfcounters_run_state)
			PRINT_KD("Running\n");
		else
			PRINT_KD("Not Running\n");
	} else
		PRINT_KD("Not Initialized    Not Running\n");
}

/*
 *Turn off the prints of perfcounters
 */
void turnoff_perfcounters(void)
{
	if (perfcounters_status) {
		perfcounters_status = 0;
		sec_perfcounters_destroy();
		PRINT_KD("\n");
		PRINT_KD("Performance Counters Dump OFF\n");
	}
}

/*
 *Turn the prints of perfcounters on
 *or off depending on the previous status.
 */
void sec_perfcounters_prints_OnOff(void)
{
	if (!perfcounters_status) {
		PRINT_KD("Performance Counters Dump ON\n");

		if (perfcounters_type != PER_THREAD_COUNTERS)
				perfcounters_show_header();
	} else
		PRINT_KD("Performance Counters Dump OFF\n");

	perfcounters_status = (perfcounters_status) ? 0 : 1;
}

/* kdebugd submenu function */
static int sec_perfcounters_control(void)
{
	int operation = 0;
	int i = 0, cpu, ret = 1;
	debugd_event_t event;
	int show_menu = 1;

	while (show_menu) {
		show_menu = 0;

		PRINT_KD("\n");
		PRINT_KD("-------------------------------------------"
				"-------------------------\n");
		PRINT_KD("Current Selection Perf Counter: ");
		if (perfcounter_num != -1)
			PRINT_KD("%s\t", perf_types[perfcounter_num].name);
		else
			PRINT_KD("All\t");
		PRINT_KD("CPU: ");
		if (cpu_to_dump == -1)
			PRINT_KD("All\n");
		else
			PRINT_KD("%d\n", cpu_to_dump);
		PRINT_KD("-------------------------------------------"
				"-------------------------\n");
		PRINT_KD("Select Operation....\n");
		PRINT_KD("1. Config: CPU and PMU counter\n");
		PRINT_KD("2. Show PMU Events Summary\n");
		PRINT_KD("3. Show PMU Events in detail\n");
		PRINT_KD("4. Show PMU Events in detail history (%d sec)\n",
				SEC_PMU_EVENTS_NR_ENTRIES);
		PRINT_KD("5. Show PMU Events in detail per processes\n");
		PRINT_KD("> ");

		operation = debugd_get_event_as_numeric(NULL, NULL);

		PRINT_KD("\n\n");

		switch (operation) {
		case 1:
			PRINT_KD("-------------------------\n");
			PRINT_KD("  %c -  ALL CPUs\n", 'a');
			for_each_online_cpu(cpu)
			PRINT_KD("  %c -  CPU %d\n", 'b' + cpu, cpu);
			PRINT_KD("-------------------------\n");
			i = 0; /* corresponds to perfcounter_num = -1 */
			PRINT_KD("  %d -  ALL PERF COUNTERS\n", i);
			for (i = 0; i < NUM_PERF_COUNTERS; i++)
				PRINT_KD("  %d -  %s COUNTER\n",
						i+1 , perf_types[i].name);
			PRINT_KD("-------------------------\n");
			PRINT_KD("> ");
			operation = debugd_get_event_as_numeric(&event, NULL);

			if (event.input_string[0] > 'a' &&
					event.input_string[0] <= 'a' + num_online_cpus())
				cpu_to_dump = event.input_string[0] - 'b';
			else if (event.input_string[0] > ('a' + num_online_cpus())
					|| event.input_string[0] == 'a')
				cpu_to_dump = -1;
			else if (operation >= 0 && operation <= NUM_PERF_COUNTERS)
				perfcounter_num = operation - 1;
			/* after configuration, we want to see the start screen */
			show_menu = 1;
			break;
		case 2:
		case 3:
			if (perfcounters_init_flag)
				sec_perfcounters_destroy();

			perfcounters_type = GLOBAL_COUNTERS;
			display_type = (operation == 2) ?
				SUMMARY_OUTPUT : DETAIL_OUTPUT;

			if (!perfcounters_init_flag)
				sec_perfcounters_init();

			/* Restart gathering statistics if necessary */
			perfcounters_idx = 0;
			perfcounters_is_buffer_full = 0;
			perfcounters_run_state = 1;
			sec_perfcounters_prints_OnOff();
			ret = 0;	/* don't print the menu */
			break;
		case 4:
			perfcounters_type = GLOBAL_COUNTERS;
			display_type = DETAIL_OUTPUT;
			dump_perf_result();
			break;
		case 5:
			if (perfcounters_init_flag &&
					perfcounters_type == GLOBAL_COUNTERS)
				sec_perfcounters_destroy();

			perfcounters_type = PER_THREAD_COUNTERS;
			display_type = DETAIL_OUTPUT;

			if (!perfcounters_init_flag)
				sec_perfcounters_init();

			perfcounters_run_state = 1;
			sec_perfcounters_prints_OnOff();
			ret = 0;
			break;
		default:
			break;
		}
	}
	return ret;
}

void sec_perfcounters_OnOff(void)
{
	if (perfcounters_init_flag)
		sec_perfcounters_destroy();
	else {
		/*BUG: if initialization failed */
		sec_perfcounters_init();
		/* start the mem usage after init*/
		perfcounters_run_state = 1;
	}
}

/* perf counters module init */
int kdbg_perfcounters_init(void)
{

	perfcounters_init_flag = 0;

	perfcounter_num = -1;
#ifdef CONFIG_SEC_PMU_EVENTS_AUTO_START
	perfcounters_type = GLOBAL_COUNTERS;
	perfcounters_idx = 0;
	sec_perfcounters_init();
#endif

	kdbg_register(
		"COUNTER MONITOR: PMU Events (Performance Monitoring Unit)",
		sec_perfcounters_control, turnoff_perfcounters,
		KDBG_MENU_COUNTER_MONITOR_PMU_EVENTS);

	return 0;
}
