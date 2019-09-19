/*
 *  linux/kernel/sec_diskusage.h
 *
 *  Disk Performance Profiling Solution
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-31  Created by Choi Young-Ho, SISC
 *
 */

#ifndef __LINUX_DISKUSAGE_H__
#define __LINUX_DISKUSAGE_H__

#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <linux/genhd.h>

#define SEC_DISKSTATS_NR_ENTRIES	CONFIG_KDEBUGD_DISKUSAGE_BUFFER_SIZE


/*Dump the bufferd data of disk usage from the buffer.
This Function is called from the kdebug menu. It prints the data
same as printed by cat /proc/diskusage-gnuplot*/
void sec_diskusage_gnuplot_dump(void);

/*
 *Turn the prints of diskusage on
 *or off depending on the previous status.
 */
void sec_diskusage_prints_OnOff(void);

/*
 * Collect disk usage data from block/genhd.c file
 */
int sec_diskstats_dump(void);

/*
 * Structure that holds data.
 */
struct sec_diskstats_struct {
	unsigned long sec;
	long nkbyte_read;
	long nkbyte_write;
	unsigned long utilization;
};

struct sec_diskdata {
	long nsect_read;
	long nsect_write;
	unsigned long io_ticks;
};

int sec_diskstats_dump_entry (struct gendisk *gd,
		struct sec_diskdata *pDisk_data);

void sec_diskusage_update(struct sec_diskdata *pDisk_data);

/*
 * The buffer that will store disk usage data.
 */
extern struct sec_diskstats_struct *g_psec_diskstats_table; /*[SEC_DISKSTATS_NR_ENTRIES]; */


extern int sec_diskusage_init_flag ;

/* check whether state is running or not..*/
extern int sec_diskusage_run_state;

/* Buffer Index */
extern int sec_diskstats_idx;

/*The flag which incates whether the buffer array is full(value is 1)
 *or is partially full(value is 0).
 */
extern int sec_diskusage_is_buffer_full;

/*The flag which will be turned on or off when sysrq feature will
 *turn on or off respectively.
 * 0 = ON
 * 1 = OFF
 */
extern int sec_diskusage_status;

/*
 * get status
 */
void get_diskusage_status(void);
/*
 * Destroy all the allocated memory
 */
void sec_diskusage_destroy(void);

void sec_diskusage_OnOff(void);
/* Mutex lock variable to save data structure while printing gnuplot data*/
extern struct mutex diskdump_lock;

#endif /* __LINUX_DISKUSAGE_H__ */
