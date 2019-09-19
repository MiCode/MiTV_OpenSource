/*
 *  kernel/kdebugd/include/kdebugd/sec_perfcounters.h
 *
 *  Performance Counters Solution
 *
 *  Copyright (C) 2011  Samsung
 *
 *  2009-05-31  Created by Vladimir Podyapolskiy
 *
 */

#ifndef __LINUX_PERFCOUNTERS_H__
#define __LINUX_PERFCOUNTERS_H__

#include <linux/kernel_stat.h>

#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/timex.h>
#include <linux/io.h>

/*
 *Turn the prints of perfcounters on
 *or off depending on the previous status.
 */
void sec_perfcounters_prints_OnOff(void);

/* Collect perf usage data */
extern int sec_perfcounters_dump(void);

/*The flag which incates whether the buffer array is full(value is 1)
 *or is partially full(value is 0).
 */
extern int sec_perfcounters_is_buffer_full;

/*The flag which will be turned on or off when sysrq feature will
 *turn on or off respectively.
 * 0 = ON
 * 1 = OFF
 */
extern int sec_perfcounters_status;

/*
 * get status
 */
void get_perfcounters_status(void);
/*
 * Destroy all the allocated memory
 */
void sec_perfcounters_destroy(void);

void sec_perfcounters_OnOff(void);

int kdbg_perfcounters_init(void);

#endif /* __LINUX_PERF_COUNTERS_H__ */
