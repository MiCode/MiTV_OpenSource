/*
 *  linux/kernel/sec_diskusage.c
 *
 *  Disk Performance Profiling Solution, disk usage releated functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-31  Created by Choi Young-Ho, SISC
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
#include <kdebugd.h>
#include <sec_diskusage.h>
#include "kdbg_util.h"

/*
 * The buffer that will store disk usage data.
 */
struct sec_diskstats_struct *g_psec_diskstats_table;	/*[SEC_DISKSTATS_NR_ENTRIES]; */

int sec_diskusage_init_flag;

/* check whether state is running or not..*/
int sec_diskusage_run_state;

/* Buffer Index */
int sec_diskstats_idx;

/*
 * Turn ON the disk usage processing.
 */
static bool sec_diskusage_init(void);

/*The flag which incates whether the buffer array is full(value is 1)
 *or is partially full(value is 0).
 */
int sec_diskusage_is_buffer_full;

/*The flag which will be turned on or off when sysrq feature will
 *turn on or off respectively.
 * 1 = ON
 * 0 = OFF
 */
int sec_diskusage_status;

/* Mutex lock variable to save data structure while printing gnuplot data*/
DEFINE_MUTEX(diskdump_lock);

/* This function is to show header */
void diskusage_show_header(void)
{
	PRINT_KD("Time     Utilization  Throughput    Read     Write\n");
	PRINT_KD("======== ===========  ==========  ========  ========\n");
}

int sec_diskstats_dump_entry(struct gendisk *gd,
			     struct sec_diskdata *pDisk_data)
{
	struct disk_part_iter piter;
	struct hd_struct *hd;
	int cpu;

	if ((NULL == gd) || NULL == pDisk_data)
		return 0;

	disk_part_iter_init(&piter, gd, DISK_PITER_INCL_EMPTY_PART0);
	hd = disk_part_iter_next(&piter);
	if (hd) {
		cpu = part_stat_lock();
		part_round_stats(cpu, hd);
		part_stat_unlock();

		pDisk_data->nsect_read += part_stat_read(hd, sectors[0]);
		pDisk_data->nsect_write += part_stat_read(hd, sectors[1]);
		pDisk_data->io_ticks += part_stat_read(hd, io_ticks);
	} else
		return 0;

	return 1;
}


#define SECT_TO_KBYTE(x)       ((x)*512/1024)



int sec_diskusage_update_flag;

int sec_diskstats_dump(void)
{
	static unsigned long old_rticks;

	unsigned long rticks = jiffies;
	int index = 0;

	static struct sec_diskdata old_disk_data = { 0};
	struct sec_diskdata disk_data = { 0};

	/* TODO: Decide what should be return value.
	 * How caller identifies different scenarios -
	 * 	e.g. init not successful
	 * 	unable to take lock
	 * 	stats read successfully
	 *
	 * 	currently, we return 0 always..
	 */

	if (sec_diskusage_init_flag) {

		/* check whether state is running or not.. */
		if (!sec_diskusage_run_state)
			return 0;

		/* BUG:flag is on but no memory allocated !! */
		BUG_ON(!g_psec_diskstats_table);

		index = (sec_diskstats_idx % SEC_DISKSTATS_NR_ENTRIES);

		sec_diskusage_update(&disk_data);

		if (sec_diskusage_update_flag) {
			/* printing gnuplot grammar need too much time (about 1~2sec)
			 * So don't save the performance data at this time to synchronize the buffer
			 */
			if (!mutex_trylock(&diskdump_lock))
				goto diskdump_out;

			g_psec_diskstats_table[index].sec = kdbg_get_uptime();
			g_psec_diskstats_table[index].nkbyte_read =
			    SECT_TO_KBYTE(disk_data.nsect_read -
					  old_disk_data.nsect_read);
			g_psec_diskstats_table[index].nkbyte_write =
			    SECT_TO_KBYTE(disk_data.nsect_write -
					  old_disk_data.nsect_write);

			BUG_ON(rticks == old_rticks);

			if (rticks != old_rticks)
				g_psec_diskstats_table[index].utilization =
				    (disk_data.io_ticks -
				     old_disk_data.io_ticks) * 100 / (rticks -
								      old_rticks);
			else {
				PRINT_KD
				    ("[KDEBUGD WARNING] disk utilization is reset to 0 !!!!, (rticks:%lu, old_rtics:%lu)\n",
				     rticks, old_rticks);
				g_psec_diskstats_table[index].utilization = 0;
			}
			mutex_unlock(&diskdump_lock);

			/* print turn on */
			if (sec_diskusage_status) {
				PRINT_KD
				    ("%4ld Sec       %3lu %%    %5lu KB  %5lu KB  %5lu KB\n",
				     g_psec_diskstats_table[index].sec,
				     g_psec_diskstats_table[index].utilization,
				     g_psec_diskstats_table[index].nkbyte_read +
				     g_psec_diskstats_table[index].nkbyte_write,
				     g_psec_diskstats_table[index].nkbyte_read,
				     g_psec_diskstats_table[index].
				     nkbyte_write);

				if ((index % 20) == 0)
					diskusage_show_header();
			}

			sec_diskstats_idx++;

			if (sec_diskstats_idx == SEC_DISKSTATS_NR_ENTRIES) {
				sec_diskusage_is_buffer_full = 1;
			}
		} else {
			sec_diskusage_update_flag = 1;
		}

diskdump_out:
		/* save the current data to compare with new data */
		old_disk_data.nsect_read = disk_data.nsect_read;
		old_disk_data.nsect_write = disk_data.nsect_write;
		old_disk_data.io_ticks = disk_data.io_ticks;
		old_rticks = rticks;

	}

	return 0;
}

/* Dump the buffered data of disk usage from the buffer.
 * It prints history of disk usage (max 120 sec).
 */
static void dump_disk_result(void)
{
	int i = 0;
	int buffer_count = 0;
	int idx = 0;

	if (sec_diskusage_is_buffer_full) {
		buffer_count = SEC_DISKSTATS_NR_ENTRIES;
		idx = sec_diskstats_idx;
	} else {
		buffer_count = sec_diskstats_idx;
		idx = 0;
	}

	PRINT_KD("Time     Utilization  Throughput    Read     Write\n");
	PRINT_KD("======== ===========  ==========  ========  ========\n");

	for (i = 0; i < buffer_count; ++i, ++idx) {
		idx = idx % SEC_DISKSTATS_NR_ENTRIES;

		PRINT_KD
		    ("%4ld Sec       %3lu %%    %5lu KB  %5lu KB  %5lu KB\n",
		     g_psec_diskstats_table[idx].sec,
		     g_psec_diskstats_table[idx].utilization,
		     g_psec_diskstats_table[idx].nkbyte_read +
		     g_psec_diskstats_table[idx].nkbyte_write,
		     g_psec_diskstats_table[idx].nkbyte_read,
		     g_psec_diskstats_table[idx].nkbyte_write);
	}
}

/* Dump the buffered data of disk usage from the buffer.
 * It prints history of disk usage (max 120 sec).
 */
static void dump_gnuplot_disk_result(void)
{
	int i = 0;
	int buffer_count = 0;
	int idx = 0;

	if (sec_diskusage_is_buffer_full) {
		buffer_count = SEC_DISKSTATS_NR_ENTRIES;
		idx = sec_diskstats_idx;
	} else {
		buffer_count = sec_diskstats_idx;
		idx = 0;
	}

	PRINT_KD("Time     Utilization  Throughput    Read     Write\n");
	PRINT_KD("======== ===========  ==========  ========  ========\n");

	for (i = 0; i < buffer_count; ++i, ++idx) {
		idx = idx % SEC_DISKSTATS_NR_ENTRIES;

		PRINT_KD
		    ("%4ld           %3lu       %5lu     %5lu     %5lu\n",
		     g_psec_diskstats_table[idx].sec,
		     g_psec_diskstats_table[idx].utilization,
		     g_psec_diskstats_table[idx].nkbyte_read +
		     g_psec_diskstats_table[idx].nkbyte_write,
		     g_psec_diskstats_table[idx].nkbyte_read,
		     g_psec_diskstats_table[idx].nkbyte_write);
	}
}

/* Dump the bufferd data of disk usage from the buffer.
 * This Function is called from the kdebug menu.
 * It prints the gnuplot data.
 */
void sec_diskusage_gnuplot_dump(void)
{
	PRINT_KD("\n\n");

	PRINT_KD("{{{#!gnuplot\n");
	PRINT_KD("reset\n");
	PRINT_KD("set title \"Disk Usage\"\n");
	PRINT_KD("set ylabel \"Throughput(KBytes)\"\n");
	PRINT_KD("set key autotitle columnheader\n");
	PRINT_KD("set xtics rotate by 90 offset character 0, -9, 0\n");
	PRINT_KD("set grid %s\n",
		 (sec_diskusage_is_buffer_full
		  || (sec_diskstats_idx > 20)) ? "ytics" : "");
	PRINT_KD("set lmargin 10\n");
	PRINT_KD("set rmargin 1\n");
	PRINT_KD("set multiplot\n");
	PRINT_KD("set size 1, 0.6\n");
	PRINT_KD("set origin 0, 0.4\n");
	PRINT_KD("set bmargin 0\n");
	PRINT_KD("#\n");
	PRINT_KD
	    ("plot \"-\" using 3:xtic(1) with lines lt 1, '' using 4 with lines, '' using 5 with lines\n");

	/* lock to save the buffer while printing gnuplot data */
	mutex_lock(&diskdump_lock);

	dump_gnuplot_disk_result();
	PRINT_KD("e\n");

	/* Since gnuplot grammar, print data several tims */
	dump_gnuplot_disk_result();
	PRINT_KD("e\n");

	/* Since gnuplot grammar, print data several tims */
	dump_gnuplot_disk_result();
	PRINT_KD("e\n");

	PRINT_KD("reset\n");
	PRINT_KD("set ylabel \"Utilization(%%)\"\n");
	PRINT_KD("set xlabel \"time(sec)\"\n");
	PRINT_KD("set key autotitle columnheader\n");
	PRINT_KD("set auto x\n");
	PRINT_KD("set xtics rotate by 90\n");
	PRINT_KD("set yrange[0:100]\n");
	PRINT_KD("set grid %s\n",
		 (sec_diskusage_is_buffer_full
		  || (sec_diskstats_idx > 20)) ? "ytics" : "");
	PRINT_KD("set lmargin 10\n");
	PRINT_KD("set rmargin 1\n");
	PRINT_KD("set bmargin\n");
	PRINT_KD("set format x\n");
	PRINT_KD("set size 1.0, 0.4\n");
	PRINT_KD("set origin 0.0, 0.0\n");
	PRINT_KD("set tmargin 0\n");
	PRINT_KD("plot \"-\" using 2:xtic(1) with filledcurve x1 lt 3\n");

	/* Since gnuplot grammar, print data several tims */
	dump_gnuplot_disk_result();

	/* unlock */
	mutex_unlock(&diskdump_lock);

	PRINT_KD("e\n");
	PRINT_KD("unset multiplot\n");
	PRINT_KD("}}}\n\n");

}

/* initialize disk usage buffer and variable */

bool sec_diskusage_init()
{
	static int func_register;
	/* Turn ON the processing of dumping the disk usage data. */
	g_psec_diskstats_table =
	    (struct sec_diskstats_struct *)
	    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				 SEC_DISKSTATS_NR_ENTRIES *
				 sizeof(struct sec_diskstats_struct),
				 GFP_ATOMIC);
	if (!g_psec_diskstats_table) {
		PRINT_KD("DISK USAGE ERROR: Insuffisient memory\n");
		sec_diskusage_init_flag = 0;
		return false;
	}

	memset(g_psec_diskstats_table, 0, SEC_DISKSTATS_NR_ENTRIES *
	       sizeof(struct sec_diskstats_struct));
	sec_diskusage_is_buffer_full = 0;
	sec_diskusage_init_flag = 1;
	sec_diskusage_update_flag = 0;

	if (!func_register) {
		register_counter_monitor_func(sec_diskstats_dump);
		func_register = 1;
	}
	return true;
}

void get_diskusage_status(void)
{
	if (sec_diskusage_init_flag) {

		PRINT_KD("Initialized        ");
		if (sec_diskusage_run_state)
			PRINT_KD("Running\n");
		else
			PRINT_KD("Not Running\n");
	} else {
		PRINT_KD("Not Initialized    Not Running\n");
	}
}

void sec_diskusage_destroy(void)
{
	mutex_lock(&diskdump_lock);

	sec_diskusage_update_flag = 0;
	sec_diskusage_status = 0;
	sec_diskusage_run_state = 0;

	if (g_psec_diskstats_table) {
		KDBG_MEM_DBG_KFREE(g_psec_diskstats_table);
		g_psec_diskstats_table = NULL;
		sec_diskusage_init_flag = 0;
		PRINT_KD("DISKUSAGE Destroyed Successfuly\n");
	} else {
		PRINT_KD("Already Not Initialized\n");
	}
	mutex_unlock(&diskdump_lock);
}

/*
 *Turn off the prints of diskusage
 */
void turnoff_diskusage(void)
{
	if (sec_diskusage_status) {
		sec_diskusage_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Disk Usage Dump OFF\n");
	}
}

/*
 *Turn the prints of diskusage on
 *or off depending on the previous status.
 */
void sec_diskusage_prints_OnOff(void)
{
	sec_diskusage_status = (sec_diskusage_status) ? 0 : 1;

	if (sec_diskusage_status) {
		PRINT_KD("Disk Usage Dump ON\n");

		diskusage_show_header();
	} else {
		PRINT_KD("Disk Usage Dump OFF\n");
	}
}

/*
 *Turn the backgroung running of diskusage on
 *or off depending on the previous status.
 */
void sec_diskusage_background_OnOff(void)
{
	sec_diskusage_run_state = (sec_diskusage_run_state) ? 0 : 1;

	if (sec_diskusage_run_state)
		PRINT_KD("CPU USAGE Backgound Run ON\n");
	else
		PRINT_KD("CPU USAGE Backgound Run OFF\n");
}

/* kdebugd submenu function */
static int sec_diskusage_control(void)
{
	int operation = 0;
	int ret = 1;

	if (!sec_diskusage_init_flag)
		sec_diskusage_init();

	PRINT_KD("\n");
	PRINT_KD("Select Operation....\n");
	PRINT_KD("1. Turn On/Off the Disk Usage prints\n");
	PRINT_KD("2. Dump Disk Usage history(%d sec)\n",
		 SEC_DISKSTATS_NR_ENTRIES);
	PRINT_KD("3. Dump Disk Usage gnuplot history(%d sec)\n",
		 SEC_DISKSTATS_NR_ENTRIES);
	PRINT_KD("==>  ");

	operation = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n\n");

	switch (operation) {
	case 1:
		sec_diskusage_run_state = 1;
		sec_diskusage_prints_OnOff();
		ret = 0;	/* don't print the menu */
		break;
	case 2:
		dump_disk_result();
		break;
	case 3:
		sec_diskusage_gnuplot_dump();
		break;
	default:
		break;
	}
	return ret;
}

void sec_diskusage_OnOff(void)
{
	if (sec_diskusage_init_flag) {
		sec_diskusage_destroy();
	} else {
		/*BUG: if initialization failed */
		sec_diskusage_init();
		/* start the mem usage after init */
		sec_diskusage_run_state = 1;
	}
}

#if	defined(CONFIG_SEC_DISKUSAGE_AUTO_START) && defined(CONFIG_COUNTER_MON_AUTO_START_PERIOD)
/* The feature is for auto start cpu usage for taking the log
 * of the secified time */
static struct hrtimer diskusage_auto_timer;
static struct timespec tv = {.tv_sec = CONFIG_COUNTER_MON_START_SEC, .tv_nsec =
	    0};

/* Auto start loging the cpu usage data */
static enum hrtimer_restart diskusage_auto_start(struct hrtimer *ht)
{
	static int started;
	enum hrtimer_restart h_ret = HRTIMER_NORESTART;

	BUG_ON(started != 0 && started != 1);

	if (!sec_diskusage_init_flag) {
		PRINT_KD("Error: DiskUsage Not Initialized\n");
		return HRTIMER_NORESTART;
	}

	/* Make the status running */
	if (!started) {

		sec_diskusage_run_state = 1;

		/* restart timer at finished seconds. */
		tv.tv_sec = CONFIG_COUNTER_MON_FINISHED_SEC;
		tv.tv_nsec = 0;

		hrtimer_forward(&diskusage_auto_timer, ktime_get(),
				timespec_to_ktime(tv));
		started = 1;
		h_ret = HRTIMER_RESTART;

	} else {

		if (!sec_diskusage_status)
			sec_diskusage_run_state = 0;

		h_ret = HRTIMER_NORESTART;
	}
	return h_ret;
}
#endif

/* diskuage module init */
int kdbg_diskusage_init(void)
{

	sec_diskusage_init_flag = 0;

#ifdef CONFIG_SEC_DISKUSAGE_AUTO_START
	sec_diskusage_init();

#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	hrtimer_init(&diskusage_auto_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	diskusage_auto_timer._softexpires = timespec_to_ktime(tv);
	diskusage_auto_timer.function = diskusage_auto_start;
	hrtimer_start(&diskusage_auto_timer, diskusage_auto_timer._softexpires,
		      HRTIMER_MODE_REL);
#else
	sec_diskusage_run_state = 1;
#endif

#endif

	kdbg_register("COUNTER MONITOR: Disk Usage", sec_diskusage_control,
		      turnoff_diskusage, KDBG_MENU_COUNTER_MONITOR_DISK_USAGE);

	return 0;
}
