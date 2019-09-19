/*
 *  linux/kernel/sec_memusage.h
 *
 *  Memory Performance Profiling Solution
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-06-05  Created by Choi Young-Ho
 *
 */

#ifndef __LINUX_MEMUSAGE_H__
#define __LINUX_MEMUSAGE_H__

#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

#define SEC_MEMUSAGE_BUFFER_ENTRIES	CONFIG_KDEBUGD_MEMUSAGE_BUFFER_SIZE

/*
The time delay after which the data is updated in the buffer.
It is configurable. This is in seconds.
*/
#define SEC_MEMUSAGE_DELAY_SECS 1

/* Number of ticks after which the data
 * will be updated in the buffer
 */
#define SEC_MEMUSAGE_UPDATE_TICKS    (SEC_MEMUSAGE_DELAY_SECS * CONFIG_HZ)

/*
 * It is called at every timer interrupt tick.
 */
void sec_memusage_interrupt(void);

/*Dump the bufferd data of mem usage from the buffer.
This Function is called from the kdebug menu. It prints the data
same as printed by cat /proc/memusage-gnuplot*/
void sec_memusage_gnuplot_dump(void);

/*Dump the bufferd data of mem usage from the buffer.
This Function is called from the kdebug menu. It prints the data
same as printed by cat /proc/memusage*/
void sec_memusage_dump(void);

/*Enable or disable the internal processing of memusage.
This same as writing the /proc/memusage*/
void sec_memusage_enable_disable(void);

/*
 *Turn the prints of memusage on
 *or off depending on the previous status.
 */
void sec_memusage_prints_OnOff(void);

/*
 * get status
 */
void get_memusage_status(void);
/*
 * Destroy all the allocated memory
 */
void sec_memusage_destroy(void);

void sec_memusage_OnOff(void);

/*
 * Structure that holds data.
 */

struct sec_memusage_struct {
	unsigned long sec;
	unsigned int totalram;
	unsigned int freeram;
	unsigned int bufferram;
	long cached;
	unsigned int anonpages_info;
};

#endif /* __LINUX_MEMUSAGE_H__ */
