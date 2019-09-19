/*
 *  linux/kernel/sec_cpuusage.c
 *
 *  CPU Performance Profiling Solution, cpu usage releated functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-11  Created by sk.bansal.
 *
 */
#include <linux/proc_fs.h>
#include <kdebugd.h>
#include "sec_cpuusage.h"
#include <linux/delay.h>
#include "kdbg_util.h"
#include "sec_workq.h"

/*
 * update the buffer with the cpu usage data.
 * It is called every SEC_CPUUSAGE_DELAY_SECS seconds.
 */
static void sec_cpuusage_show_buffer(int index);

/*
 * Create the string of #(for user percentage)
 * and @(for system percentage).
 */
#ifndef CONFIG_SMP
static char *sec_cpuusage_create_string(char *pictStr,
					int cpuusage_user_perc,
					int cpuusage_sys_perc,
					int cpuusage_io_perc);
#endif /*CONFIG_SMP */

/*
 * Turn ON the cpu usage processing.
 */
static int sec_cpuusage_create(void);

/*
 * The buffer that will store cpu usage data.
 */
static struct sec_cpuusage_struct *g_psec_cpuusage_buffer_;

/*
 * when first time run the previouse value is not correct
 * at the time of initilize set the value of first run to 1.
 * when previouse cpu value updated, value of variable will be 0.
 */
static int g_cpuusage_interrupt_first_flag;

/* State flag, reason for taking atomic is all the code is
 * running with state transition which is done by veriouse
 * thread in multiprocessor system keep accounting of this
 * variable is necessary */
static atomic_t g_sec_cpuusage_state = ATOMIC_INIT(E_NONE);

/* Spinlock for accessing global buffer pointer
 * this lock is used for protecting global table
 * it can be destroyed by other thread. */
static DEFINE_SPINLOCK(sec_cpuusage_lock);

/*The index of the buffer array at which the data will be written.*/
/* Lalit: mention "in each cpu usage table" */
static int sec_cpuusage_w_index;

/* Fucntion for setting new state */
/* Lalit: Can we rename it to "change_state" instead of "set_state"? */
static int sec_cpuusage_set_state(sec_counter_mon_state_t new_state, int sync);

/* Fucntion for getting the existing state*/
static inline sec_counter_mon_state_t sec_cpuusage_get_state(void);

static int sec_cpuusage_control(void);
static void sec_cpuusage_destroy_impl(void);
static int sec_cpuusage_create(void);
void sec_cpuusage_dump(void);
void sec_cpuusage_show_buffer(int index);

/* The flag which indicates whether the buffer array is full(value is 1)
 * or is partially full(value is 0).if buffer is full it wiil be rollback.
 * it always be initialized along with the global buffer.
 */
static int sec_cpuusage_is_buffer_full;

#define SPRINT_BUF_LEN 512
/* This function is to show header */
static void cpuusage_show_header(void)
{
	int cpu = 0;
	int offset = 0;
	char buf[SPRINT_BUF_LEN];

	PRINT_KD("\n");
	offset += sprintf(buf, "                  ");
	for_each_online_cpu(cpu) {
		offset += sprintf(buf + offset, "sys i/o wait          ");
	}
	PRINT_KD("%s\n", buf);

	offset = sprintf(buf, "time ");
	for_each_online_cpu(cpu) {
		offset += sprintf(buf + offset, " CPU%d   user  |    |  ", cpu);
	}
	PRINT_KD("%s\n", buf);

	offset = sprintf(buf, " |   ");
	for_each_online_cpu(cpu) {
		offset += sprintf(buf + offset, "  |      |    |    |  ");
	}
	PRINT_KD("%s\n", buf);

}

/* Print header in the interval defin with this preporcessor value*/
#define SEC_CPUUSAGE_HEADER_INTERVAL 20

#ifndef CONFIG_SMP
#define PIC_STR_LEN 21
/* CHANGES: Namit 25-Nov-2010- this function s
   how the string which is excluded in current release  */
/*Create the cpu usage string like this " #####@@@@@@@ "
 *This string make easier to see the cpu usage on serial console.
 */
static char *sec_cpuusage_create_string(char *pictStr, int user_perc,
					int sys_perc, int io_perc)
{
	char *ptr = pictStr;
	/*This will print # per 5 percent of user percentage */
	int user_count = user_perc / 5;

	/*This will print @ per 5 percent of system percentage */
	int sys_count = sys_perc / 5;

	/*This will print $ per 5 percent of i/o wait percentage */
	int io_count = io_perc / 5;

	if (user_perc + sys_perc + io_perc > 100) {
		PRINT_KD("Real %%age can not be greater than 100\n");
		strncpy(pictStr, "!!!", sizeof("!!!"));
		pictStr[PIC_STR_LEN - 1] = 0;
		return pictStr;
	}

	/* Memory buffer can not be overflow
	 * size of string =  21
	 * and max size of (user_perc + sys_perc + io_perc=20)  */

	while (sys_count--)
		*ptr++ = '@';

	while (user_count--)
		*ptr++ = '#';

	while (io_count--)
		*ptr++ = '$';

	*ptr = 0;
	return pictStr;
}
#endif

/* (x) is the difference between previous ticks and current ticks.
 * So it can be changed from u64 to int.
 */
#define TIME_TO_PERCENT(x)	(((int)(x)) * 100 / (int)SEC_CPUUSAGE_UPDATE_TICKS)

/*
 * Show the buffer with the cpu usage data on given index.
 * It is called every SEC_CPUUSAGE_DELAY_SECS seconds.
 */
void sec_cpuusage_show_buffer(int index)
{
	int user_perc[NR_CPUS] = { 0};
	int sys_perc[NR_CPUS] = { 0};
	int io_perc[NR_CPUS] = { 0};
	int cpu = 0;
	long sec_count = 0;
	unsigned long flags;
	int buf_offset;
	char buf[SPRINT_BUF_LEN];

#ifndef CONFIG_SMP
	char pict_str[PIC_STR_LEN];
#endif

	if (sec_cpuusage_get_state() != E_RUN_N_PRINT) {
		PRINT_KD("%s: stale message: index= %d\n", __FUNCTION__, index);
		return;
	}

	/*BUG: fucntion will call when cpuusage initialized.
	 *     and buffer is not NULL */
	BUG_ON(!g_psec_cpuusage_buffer_);

	spin_lock_irqsave(&sec_cpuusage_lock, flags);

	sec_count = g_psec_cpuusage_buffer_[index].sec_count;
	for_each_online_cpu(cpu) {
		/* The cpu usage in last SEC_CPUUSAGE_DELAY_SECS second(s). */
		user_perc[cpu] =
		    g_psec_cpuusage_buffer_[index].cpu_data[cpu].user;
		sys_perc[cpu] =
		    g_psec_cpuusage_buffer_[index].cpu_data[cpu].system;
		io_perc[cpu] = g_psec_cpuusage_buffer_[index].cpu_data[cpu].io;
	}

	spin_unlock_irqrestore(&sec_cpuusage_lock, flags);

	/* cpu usage is divided into several values in kernel,
	 * so it should be added to user or system time. */

	buf_offset = sprintf(buf, "%04ld", sec_count);

	for_each_online_cpu(cpu) {

		buf_offset += sprintf(buf + buf_offset, " [%03d%%]", (user_perc[cpu] + sys_perc[cpu]));
		buf_offset += sprintf(buf + buf_offset, " %03d%% %03d%% %03d%%", user_perc[cpu],
			 sys_perc[cpu], io_perc[cpu]);
#ifndef CONFIG_SMP
		/* Since only one CPU print character string also */
		pict_str[0] = pict_str[PIC_STR_LEN - 1] = '\0';
		buf_offset += sprintf(buf + buf_offset, "\t%s", sec_cpuusage_create_string(pict_str,
							    user_perc[cpu],
							    sys_perc[cpu],
							    io_perc[cpu]));
#endif
	}

	PRINT_KD("%s\n", buf);
	if (((index + 1) % SEC_CPUUSAGE_HEADER_INTERVAL) == 0) {
		PRINT_KD("\n");
		cpuusage_show_header();
	}
}

#ifndef CONFIG_SMP
 /* Dump the bufferd data of cpu usage from the buffer.
    This Function is called from the kdebug menu. It prints the data
    same as printed by cat /proc/cpuusage-gnuplot
  */
static void sec_cpuusage_gnuplot_dump(void)
{
	int last_row = 0, saved_row = 0;
	int idx, i;
	int limit_count;
	int cpu = 0;
	sec_counter_mon_state_t state = sec_cpuusage_get_state();

	if (state != E_RUNNING && state != E_RUN_N_PRINT) {
		PRINT_KD("%s: state = %d: not running: ignoring\n",
			 __FUNCTION__, state);
		return;
	}

	BUG_ON(!g_psec_cpuusage_buffer_);

	if (sec_cpuusage_is_buffer_full) {
		limit_count = SEC_CPUUSAGE_BUFFER_ENTRIES;
		last_row = sec_cpuusage_w_index;
	} else {
		limit_count = sec_cpuusage_w_index;
		last_row = 0;
	}
	saved_row = last_row;

	PRINT_KD(" \n\n");
	PRINT_KD("{{{#!gnuplot\n");
	PRINT_KD("reset\n");
	PRINT_KD("set title \"CPU Usage\"\n");
	PRINT_KD("set xlabel \"time(sec)\"\n");
	PRINT_KD("set ylabel \"Usage(%%)\"\n");
	PRINT_KD("set yrange [0:100]\n");
	PRINT_KD("set key invert reverse Left outside\n");
	PRINT_KD("set key autotitle columnheader\n");
	PRINT_KD("set auto x\n");
	PRINT_KD("set xtics nomirror rotate by 90\n");
	PRINT_KD("set style data histogram\n");
	PRINT_KD("set style histogram rowstacked\n");
	PRINT_KD("set style fill solid border -1\n");
	PRINT_KD("set grid ytics\n");
	PRINT_KD("set boxwidth 0.7\n");
	PRINT_KD("set lmargin 8\n");
	PRINT_KD("set rmargin 1\n");
	PRINT_KD("#\n");
	PRINT_KD("plot \"-\" using 3:xtic(1), '' using 4\n");

	/* Because of gnuplot grammar, we should print data twice
	 * to draw the two graphic lines on the chart. */
	for (i = 0; i < 2; i++) {
		PRINT_KD("Sec\t\tTotal\tSys\tUsr\n");
		for (idx = 0; idx < limit_count; idx++) {
			int index = last_row % SEC_CPUUSAGE_BUFFER_ENTRIES;
			last_row++;
			PRINT_KD("%04ld\t\t%02u%%\t%02u%%\t%02u%%\n",
				 g_psec_cpuusage_buffer_[index].sec_count,
				 g_psec_cpuusage_buffer_[index].cpu_data[cpu].
				 user +
				 g_psec_cpuusage_buffer_[index].cpu_data[cpu].
				 system,
				 g_psec_cpuusage_buffer_[index].cpu_data[cpu].
				 system,
				 g_psec_cpuusage_buffer_[index].cpu_data[cpu].
				 user);
		}
		PRINT_KD("e\n");
		last_row = saved_row;
	}

	PRINT_KD("}}}\n");
}
#endif /* CONFIG_SMP */

struct sec_cpuusage_data {
	cputime64_t user;	/* User CPU */
	cputime64_t system;	/* CPU spend in system */
	cputime64_t io;		/* I/O wait */
};

/*
 * This function is called at 1 second interval from hrtimer.
 */
static void sec_cpuusage_interrupt(void)
{
	int cpu = 0;
	sec_counter_mon_state_t state;
	long cur_w_index;
	static struct kdbg_work_t sec_cpuusage_work;
	static struct sec_cpuusage_data prev_cpu_data[NR_CPUS];

	state = sec_cpuusage_get_state();
	if (state != E_RUNNING && state != E_RUN_N_PRINT) {
		SEC_CPUUSAGE_DEBUG("%s: cpu-usage state= %d: ignoring\n",
				   __FUNCTION__, state);
		return;
	}

	if (unlikely(g_cpuusage_interrupt_first_flag)) {

		g_cpuusage_interrupt_first_flag = 0;

		for_each_online_cpu(cpu) {

			prev_cpu_data[cpu].user = kcpustat_cpu(cpu).cpustat[CPUTIME_USER] +
			    kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

			prev_cpu_data[cpu].system =
			    kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM] +
			    kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ] +
			    kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];

			prev_cpu_data[cpu].io = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
		}
		return;
	}

	spin_lock(&sec_cpuusage_lock);

	/* Practiclay that condition should never hit */
	BUG_ON(!g_psec_cpuusage_buffer_);

	g_psec_cpuusage_buffer_[sec_cpuusage_w_index].sec_count =
	    kdbg_get_uptime();

	/* calculate usage percentage of each cpu and store in table */
	for_each_online_cpu(cpu) {
		struct sec_cpuusage_data curr_cpu_data;

		curr_cpu_data.user = kcpustat_cpu(cpu).cpustat[CPUTIME_USER] +
		    kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

		curr_cpu_data.system = kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM] +
		    kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ] + kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];

		curr_cpu_data.io = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
		WARN_ON(g_cpuusage_interrupt_first_flag);

		/* Update the percentages in cpu-usage sample table */
		g_psec_cpuusage_buffer_[sec_cpuusage_w_index].cpu_data[cpu].
		    user =
		    (int)TIME_TO_PERCENT(curr_cpu_data.user -
					 prev_cpu_data[cpu].user);

		g_psec_cpuusage_buffer_[sec_cpuusage_w_index].cpu_data[cpu].
		    system =
		    (int)TIME_TO_PERCENT(curr_cpu_data.system -
					 prev_cpu_data[cpu].system);

		g_psec_cpuusage_buffer_[sec_cpuusage_w_index].cpu_data[cpu].io =
		    (int)TIME_TO_PERCENT(curr_cpu_data.io -
					 prev_cpu_data[cpu].io);

		/* store result for next sampling calculation */
		prev_cpu_data[cpu].user = curr_cpu_data.user;
		prev_cpu_data[cpu].system = curr_cpu_data.system;
		prev_cpu_data[cpu].io = curr_cpu_data.io;
	}

	spin_unlock(&sec_cpuusage_lock);
	cur_w_index = sec_cpuusage_w_index;

	if (++sec_cpuusage_w_index == SEC_CPUUSAGE_BUFFER_ENTRIES) {

		if (unlikely(!sec_cpuusage_is_buffer_full)) {
			/* Buffer roll back */
			sec_cpuusage_is_buffer_full = 1;
		}
		/* Reset  write index */
		sec_cpuusage_w_index = 0;
	}

	/* post print job to worker thread */
	if (state == E_RUN_N_PRINT) {
		sec_cpuusage_work.data = (void *)cur_w_index;
		sec_cpuusage_work.pwork = (void *)sec_cpuusage_show_buffer;
		kdbg_workq_add_event(sec_cpuusage_work, NULL);
	} else {
		WARN_ON(state != E_RUNNING);
		WARN_ON(sec_cpuusage_get_state() != E_RUNNING);	/* race condition */
	}
}

/* Dump the bufferd data of cpu usage from the buffer.
 * This Function is called from the kdebug menu. */
void sec_cpuusage_dump(void)
{
	int i = 0;
	int buffer_count = 0;
	int idx = 0;
	int user_perc = 0;
	int sys_perc = 0;
	int io_perc = 0;
	int cpu = 0;
	int buf_offset = 0;
	char buf[SPRINT_BUF_LEN];

#ifndef CONFIG_SMP
	char pict_str[PIC_STR_LEN];
#endif
	sec_counter_mon_state_t state = sec_cpuusage_get_state();

	if (state == E_NONE || state >= E_DESTROYING) {
		SEC_CPUUSAGE_DEBUG("%s: state = %d: not running: ignoring\n",
				   __FUNCTION__, state);
		return;
	}

	BUG_ON(!g_psec_cpuusage_buffer_);

	if (sec_cpuusage_is_buffer_full) {
		buffer_count = SEC_CPUUSAGE_BUFFER_ENTRIES;
		idx = sec_cpuusage_w_index;
	} else {
		buffer_count = sec_cpuusage_w_index;
		idx = 0;
	}

	cpuusage_show_header();
	for (i = 0; i < buffer_count; ++i, ++idx) {
		idx = idx % SEC_CPUUSAGE_BUFFER_ENTRIES;
		buf_offset = sprintf(buf, "%04ld", g_psec_cpuusage_buffer_[idx].sec_count);
		for_each_online_cpu(cpu) {

			/* Calculate the cpu usage in last SEC_CPUUSAGE_DELAY_SECS second(s). */
			user_perc =
			    g_psec_cpuusage_buffer_[idx].cpu_data[cpu].user;
			sys_perc =
			    g_psec_cpuusage_buffer_[idx].cpu_data[cpu].system;
			io_perc = g_psec_cpuusage_buffer_[idx].cpu_data[cpu].io;

			buf_offset += sprintf(buf + buf_offset, " [%03d%%] %03d%% %03d%% %03d%%",
				(user_perc + sys_perc), user_perc, sys_perc, io_perc);

#ifndef CONFIG_SMP
			pict_str[0] = pict_str[PIC_STR_LEN - 1] = '\0';
			/* Since only one CPU print character string also */
			buf_offset += sprintf(buf + buf_offset, "\t%s",
					sec_cpuusage_create_string(pict_str,
								    user_perc,
								    sys_perc,
								    io_perc));
#endif
		}
		PRINT_KD("%s\n", buf);
	}
}

/* Initialize the cpu usage buffer and global variables.
 * Return 0 on success, negative value as error code */
int sec_cpuusage_create(void)
{
	sec_counter_mon_state_t state = sec_cpuusage_get_state();

	WARN_ON(state != E_NONE && state != E_DESTROYED);

	/* Check for the state first */
	if (state > E_NONE && state < E_DESTROYED) {
		PRINT_KD("CPU USAGE ERROR: Already Initialized\n");
		return -ERR_INVALID;
	}

	WARN_ON(g_psec_cpuusage_buffer_);	/* already cleared */
	g_psec_cpuusage_buffer_ = NULL;
	return 0;
}

void sec_cpuusage_get_status(void)
{
	switch (sec_cpuusage_get_state()) {
	case E_NONE:		/* Falls Through */
	case E_DESTROYED:
		PRINT_KD("Not Initialized    Not Running\n");
		break;
	case E_DESTROYING:
		PRINT_KD("Not Initialized    <Destroying...>\n");
		break;
	case E_RUN_N_PRINT:
		PRINT_KD("Initialized        Running\n");
		break;
	case E_RUNNING:
		PRINT_KD("Initialized        Background Sampling ON\n");
		break;
	case E_INITIALIZED:
		PRINT_KD("Initialized        Not Running\n");
		break;
	default:
		/* TODO: WARN_ON can be better */
		BUG_ON("ERROR: Invalid state");
	}
}

/* The function call publicaly for destroying all
 * the resource allocation by cpu usage*/
void sec_cpuusage_off(void)
{
	sec_counter_mon_state_t prev_state = sec_cpuusage_get_state();

	if (prev_state > E_NONE && prev_state < E_DESTROYING) {
		/* Now destroy */
		/* Before Destroying make sure state is E_INITIALIZED */
		while (prev_state > E_INITIALIZED)
			sec_cpuusage_set_state(--prev_state, 1);	/* Send with sync */

		/* System is in E_INITIALIZED State now destroyed it
		 * and free all the resources*/
		sec_cpuusage_set_state(E_DESTROYED, 1);	/* Send with sync */
	} else {

		PRINT_KD("State is either destroyed or Invalid %d\n",
			 prev_state);
	}
}

/* Destroy implementation funciton used internaly */
void sec_cpuusage_destroy_impl(void)
{
	unsigned long flags;
	/* Buffer should not be NULL
	 * Print warning but avoid crash */
	WARN_ON(!g_psec_cpuusage_buffer_);

	spin_lock_irqsave(&sec_cpuusage_lock, flags);
	if (g_psec_cpuusage_buffer_) {
		KDBG_MEM_DBG_KFREE(g_psec_cpuusage_buffer_);
		g_psec_cpuusage_buffer_ = NULL;
	}
	spin_unlock_irqrestore(&sec_cpuusage_lock, flags);

	sec_cpuusage_w_index = 0;
	/* Reset buffer buffer index */
	sec_cpuusage_is_buffer_full = 0;
	/* Do not take first value for comparison */
	g_cpuusage_interrupt_first_flag = 1;

}

/*
 *Turn off the prints of cpuusage on
 */
void turnoff_sec_cpuusage(void)
{
	if (sec_cpuusage_get_state() == E_RUN_N_PRINT) {
		sec_cpuusage_set_state(E_RUNNING, 1);	/* Send with sync */
		PRINT_KD("CPU USAGE Dump OFF\n");
	}
}

/*
 *Turn the prints of cpuusage on
 *or off depending on the previous status.
 */
void sec_cpuusage_prints_on_off(void)
{
	int ret = 0;
	sec_counter_mon_state_t state = sec_cpuusage_get_state();

	if (state == E_RUNNING) {
		cpuusage_show_header();
		sec_cpuusage_set_state(E_RUN_N_PRINT, 1);	/* Send with sync */
	} else if (state == E_RUN_N_PRINT) {
		ret = sec_cpuusage_set_state(E_RUNNING, 1);	/* Send with sync */
	} else {
		ret = -ERR_INVALID;
		PRINT_KD("Invalid cpuusate state= %d\n", state);
		/* TODO: WARN_ON can be better */
		BUG_ON("Invalid cpuusage state");
	}

	if (ret)
		PRINT_KD
		    ("ERROR: cpuusage prints on / off: operation failed with"
		     " error code= %d\n", ret);
}

/*cpuusage hrtimer for 1 sec interval*/
static struct hrtimer cpuusage_timer;
static struct timespec tv = {.tv_sec = 1, .tv_nsec = 0};
/*Function handler for cpuusage_hrtimer*/
static enum hrtimer_restart cpuusage_hrtimer_handler(struct hrtimer *ht)
{
	/*sampling at every second and forward timer*/
	sec_cpuusage_interrupt();
	hrtimer_forward(&cpuusage_timer, ktime_get(), timespec_to_ktime(tv));
	return HRTIMER_RESTART;
}
/* Set the new state of cpuusage */
static int sec_cpuusage_set_state_impl(sec_counter_mon_state_t new_state)
{
	int ret = 0;
	sec_counter_mon_state_t prev_state = sec_cpuusage_get_state();

	SEC_CPUUSAGE_DEBUG("Changing state:%d --> %d\n", prev_state, new_state);

	switch (new_state) {
	case E_INITIALIZED:
		if (prev_state == E_NONE || prev_state == E_DESTROYED) {
			/* Initialize the cpuusage buffer */
			BUG_ON(g_psec_cpuusage_buffer_);
			/*Using in timer context, use with GFP_ATOMIC flag */
			g_psec_cpuusage_buffer_ =
			    (struct sec_cpuusage_struct *)
			    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
						 SEC_CPUUSAGE_BUFFER_ENTRIES *
						 sizeof(struct
							sec_cpuusage_struct),
						 GFP_ATOMIC);
			if (!g_psec_cpuusage_buffer_) {
				PRINT_KD
				    ("ERROR: CPU USAGE: Insufficient memory\n");
				ret = -ERR_NO_MEM;
				break;
			}
			memset(g_psec_cpuusage_buffer_, 0,
			       SEC_CPUUSAGE_BUFFER_ENTRIES *
			       sizeof(struct sec_cpuusage_struct));

			/* Reset write Index */
			sec_cpuusage_w_index = 0;

			/* Reset buffer buffer index */
			sec_cpuusage_is_buffer_full = 0;

			/* Do not take first value for comparison */
			g_cpuusage_interrupt_first_flag = 1;

			/*ON init flag.. */
			atomic_set(&g_sec_cpuusage_state, E_INITIALIZED);
			PRINT_KD("Initialization done ..\n");

		} else if (prev_state == E_RUNNING
			   || prev_state == E_RUN_N_PRINT) {
			atomic_set(&g_sec_cpuusage_state, E_INITIALIZED);
			/*cancel hrtimer*/
			hrtimer_cancel(&cpuusage_timer);
		} else if (prev_state == E_INITIALIZED) {
			PRINT_KD("Already Initialized\n");
			ret = -ERR_DUPLICATE;
		} else {
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);	/* internal transition state */
			BUG_ON("Invalid cpuusage state");
		}
		break;
	case E_RUNNING:
		if (prev_state == E_INITIALIZED) {
			g_cpuusage_interrupt_first_flag = 1;
			atomic_set(&g_sec_cpuusage_state, E_RUNNING);
			/*start hrtimer*/
			hrtimer_init(&cpuusage_timer, CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
			cpuusage_timer._softexpires =
				timespec_to_ktime(tv);
			cpuusage_timer.function =
				cpuusage_hrtimer_handler;
			hrtimer_start(&cpuusage_timer,
				cpuusage_timer._softexpires,
				HRTIMER_MODE_REL);
			PRINT_KD("Running status done ..\n");
		} else if (prev_state == E_RUN_N_PRINT) {
			atomic_set(&g_sec_cpuusage_state, E_RUNNING);
		} else if (prev_state == E_RUNNING) {
			PRINT_KD
			    ("Already Running: Background Sampling is ON\n");
			ret = -ERR_DUPLICATE;
		} else if (prev_state == E_NONE || prev_state == E_DESTROYED) {
			PRINT_KD("ERROR: %s: only one transition supported\n",
				 __FUNCTION__);
			ret = -ERR_NOT_SUPPORTED;
		} else {
			ret = -ERR_INVALID;
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);	/* internal transition state */
			BUG_ON("Invalid cpuusage state");
		}
		break;
	case E_RUN_N_PRINT:
		if (prev_state == E_RUNNING) {
			atomic_set(&g_sec_cpuusage_state, E_RUN_N_PRINT);
		} else if (prev_state == E_RUN_N_PRINT) {
			PRINT_KD("Already Running state\n");
			ret = -ERR_DUPLICATE;
		} else if (prev_state == E_NONE || prev_state == E_DESTROYED) {
			PRINT_KD("ERROR: %s: only one transition supported\n",
				 __FUNCTION__);
			ret = -ERR_NOT_SUPPORTED;
		} else {
			ret = -ERR_INVALID;
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);	/* internal transition state */
			BUG_ON("Invalid cpuusage state");
		}
		break;
	case E_DESTROYED:

		if (prev_state == E_INITIALIZED) {

			/* First set the state so that interrupt stop all the work */
			atomic_set(&g_sec_cpuusage_state, E_DESTROYING);
			sec_cpuusage_destroy_impl();
			atomic_set(&g_sec_cpuusage_state, E_DESTROYED);

		} else if (prev_state == E_DESTROYING) {
			PRINT_KD("Already Destroying state\n");
			ret = -ERR_DUPLICATE;
		} else {
			ret = -ERR_INVALID;
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);	/*internal transition state */
			BUG_ON("Invalid cpuusage state");
		}

		break;
	default:
		PRINT_KD("ERROR: Invalid State argument\n");
		BUG_ON(prev_state == E_DESTROYING);
		ret = -ERR_INVALID_ARG;
		break;
	}
	return ret;
}

static int sec_cpuusage_set_state(sec_counter_mon_state_t new_state, int sync)
{

	struct kdbg_work_t sec_cpuusage_destroy_work;
	struct completion done;
	int ret = 0;

	if (sync) {
		init_completion(&done);

		sec_cpuusage_destroy_work.data = (void *)new_state;
		sec_cpuusage_destroy_work.pwork =
		    (void *)sec_cpuusage_set_state_impl;
		kdbg_workq_add_event(sec_cpuusage_destroy_work, &done);

		wait_for_completion(&done);

		/* Check the failed case */
		if (sec_cpuusage_get_state() != new_state) {
			PRINT_KD("SEC CPUUSAGE ERROR: State change Failed\n");
			ret = -1;
		}

	} else {		/* In some cases, wait is cannot permitted.
				   In that case set state immediately . */
		sec_cpuusage_set_state_impl(new_state);
	}

	return ret;

}

/* Get the current state of cpuusage */
static inline sec_counter_mon_state_t sec_cpuusage_get_state(void)
{
	return atomic_read(&g_sec_cpuusage_state);
}

/*
 *Turn the backgroung running of cpuusage on
 *or off depending on the previous status.
 */
static int sec_cpuusage_control(void)
{
	int operation = 0;
	int ret = 1;
	sec_counter_mon_state_t state = sec_cpuusage_get_state();

	/* start background sampling, if stopped state */
	if (state == E_NONE || state == E_DESTROYED) {

		if (sec_cpuusage_set_state(E_INITIALIZED, 1) < 0) {	/* Send with sync */
			PRINT_KD
			    ("SEC CPUUSAGE ERROR: Error in Initialization\n");
			return 1;
		}
	}

	PRINT_KD("\n");
	PRINT_KD("Select Operation....\n");
	PRINT_KD("1. Turn On/Off the CPU Usage prints\n");
	PRINT_KD("2. Dump CPU Usage history(%d sec)\n",
		 SEC_CPUUSAGE_BUFFER_ENTRIES);
#ifndef CONFIG_SMP
	PRINT_KD("3. Dump CPU Usage gnuplot history(%d sec)\n",
		 SEC_CPUUSAGE_BUFFER_ENTRIES);
#endif
	PRINT_KD("99. For Exit\n");

	PRINT_KD("==>  ");

	operation = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n\n");

	switch (operation) {
	case 1:
		state = sec_cpuusage_get_state();
		if (state == E_INITIALIZED)
			sec_cpuusage_set_state(E_RUNNING, 1);	/* Send with sync */
		sec_cpuusage_prints_on_off();
		ret = 0;	/* don't print the menu */
		break;
	case 2:
		sec_cpuusage_dump();
		break;
#ifndef CONFIG_SMP
	case 3:
		sec_cpuusage_gnuplot_dump();
		break;
#endif /* CONFIG_SMP */
	case 99:
		ret = 1;
		break;
	default:
		PRINT_KD("Invalid Input ...\n");
		break;
	}
	return ret;
}

void sec_cpuusage_on_off(void)
{

	sec_counter_mon_state_t state = sec_cpuusage_get_state();
	/* Destroying state is intermediate state of destroy
	 * and it should not be stale, avoid this*/
	BUG_ON(state == E_DESTROYING);

	if (state == E_NONE || state == E_DESTROYED) {

		if (sec_cpuusage_set_state(E_INITIALIZED, 1) < 0) {	/* Send with sync */

			PRINT_KD("Initialization Failed\n");
			return;
		}
		/* background sampling ON */
		sec_cpuusage_set_state(E_RUNNING, 1);	/* Send with sync */
	} else {

		WARN_ON(state < E_NONE || state > E_DESTROYED);

		/* Before Destroying make sure state is E_INITIALIZED */
		while (state > E_INITIALIZED)
			sec_cpuusage_set_state(--state, 1);	/* Send with sync */

		/* System is in E_INITIALIZED State now destroyed it
		 * and free all the resources*/
		sec_cpuusage_set_state(E_DESTROYED, 1);	/* Send with sync */
	}
}

#if defined (CONFIG_SEC_CPU_AUTO_START) &&  defined(CONFIG_COUNTER_MON_AUTO_START_PERIOD)
/* The feature is for auto start cpu usage for taking the log
 * of the secified time */
static struct hrtimer cpuusage_auto_timer;
static struct timespec tv_autostart = {.tv_sec = CONFIG_COUNTER_MON_START_SEC, .tv_nsec =
	    0};
static enum hrtimer_restart cpuusage_auto_start(struct hrtimer *ht);

static void sec_cpuusage_hrtimer_start(void)
{
	int state_ret = 0;
	/* Make the status running */
	sec_counter_mon_state_t state = sec_cpuusage_get_state();

	/* Check whether its in Initialized state */
	if (state == E_NONE || state == E_DESTROYED) {
		/* If not Initialize it First , send no sync */
		state_ret = sec_cpuusage_set_state_impl(E_INITIALIZED);
		if (state_ret < 0) {	/* Error in Initialization */
			PRINT_KD
			    ("SEC CPUUSAGE ERROR: Error In Initialization\n");
			return;
		}
	}
	if (sec_cpuusage_get_state() != E_RUNNING &&
	    sec_cpuusage_get_state() != E_RUN_N_PRINT) {
		state_ret = sec_cpuusage_set_state_impl(E_RUNNING);
	}
}

static void sec_cpuusage_hrtimer_stop(void)
{
	int state_ret = 0;
	/* Make the status running */

	sec_counter_mon_state_t state = sec_cpuusage_get_state();
	if (state == E_RUNNING) {
		state_ret = sec_cpuusage_set_state(E_INITIALIZED, 0);
		WARN_ON(state_ret);
	}
}

/* Auto start loging the cpu usage data */
static enum hrtimer_restart cpuusage_auto_start(struct hrtimer *ht)
{
	static int started;
	enum hrtimer_restart h_ret = HRTIMER_NORESTART;
	int ret = 0;
	struct kdbg_work_t sec_cpuusage_start_work;
	sec_counter_mon_state_t state = sec_cpuusage_get_state();
	BUG_ON(started != 0 && started != 1);

	/* If state is destroying dont take a risk,
	 * return immediatly*/
	if (state == E_DESTROYING) {
		PRINT_KD
		    ("INFO --> State is destroying cpusauge cannot be started\n");
		return ret;
	}

	/* Make the status running */
	if (!started) {

		/* Post the work */
		sec_cpuusage_start_work.data = (void *)NULL;
		sec_cpuusage_start_work.pwork =
		    (void *)sec_cpuusage_hrtimer_start;

		kdbg_workq_add_event(sec_cpuusage_start_work, NULL);

		started = 1;
		/* restart timer at finished seconds. */
		tv_autostart.tv_sec = CONFIG_COUNTER_MON_FINISHED_SEC;
		tv_autostart.tv_nsec = 0;

		hrtimer_forward(&cpuusage_auto_timer, ktime_get(),
				timespec_to_ktime(tv_autostart));
		h_ret = HRTIMER_RESTART;

	} else {
		/* Finished second expired stop it */
		/* If user enabled periodic printing, then don't stop it */
		BUG_ON(h_ret != HRTIMER_NORESTART);

		sec_cpuusage_start_work.data = (void *)NULL;
		sec_cpuusage_start_work.pwork =
		    (void *)sec_cpuusage_hrtimer_stop;

		kdbg_workq_add_event(sec_cpuusage_start_work, NULL);

		++started;
	}

	return h_ret;
}
#endif

int kdbg_cpuusage_init(void)
{
	if (sec_cpuusage_create()) {
		WARN_ON("Error in Initialization");
		return 0;
	}
#ifdef CONFIG_SEC_CPU_AUTO_START

#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	hrtimer_init(&cpuusage_auto_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cpuusage_auto_timer._softexpires = timespec_to_ktime(tv_autostart);
	cpuusage_auto_timer.function = cpuusage_auto_start;
	hrtimer_start(&cpuusage_auto_timer, cpuusage_auto_timer._softexpires,
		      HRTIMER_MODE_REL);
#else
	if (sec_cpuusage_set_state(E_INITIALIZED, 1) < 0) {	/* Send with sync */
		PRINT_KD("SEC CPUUSAGE ERROR: Error in Initialization\n");
	} else {
		sec_cpuusage_set_state(E_RUNNING, 1);	/* Send with sync */
	}
#endif

#endif

	kdbg_register("COUNTER MONITOR: CPU Usage", sec_cpuusage_control,
		      turnoff_sec_cpuusage,
		      KDBG_MENU_COUNTER_MONITOR_CPU_USAGE);

	return 0;
}
