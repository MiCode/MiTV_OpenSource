/*
 *  linux/kernel/sec_netusage.h
 *
 *  Network Performance Profiling Solution
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-30  Created by Choi Young-Ho, Kim Geon-Ho
 *
 */

#ifndef __LINUX_NETUSAGE_H__
#define __LINUX_NETUSAGE_H__

#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

#define SEC_NETUSAGE_BUFFER_ENTRIES	CONFIG_KDEBUGD_NETUSAGE_BUFFER_SIZE

/*
The time delay after which the data is updated in the buffer.
It is configurable. This is in seconds.
*/
#define SEC_NETUSAGE_DELAY_SECS 1

/* Number of ticks after which the data
  * will be updated in the buffer
  */
#define SEC_NETUSAGE_UPDATE_TICKS    (SEC_NETUSAGE_DELAY_SECS * CONFIG_HZ)

/*
 * It is called at every timer interrupt tick.
 */
void sec_netusage_interrupt(void);

/*Dump the bufferd data of net usage from the buffer.
This Function is called from the kdebug menu. It prints the data
same as printed by cat /proc/netusage-gnuplot*/
void sec_netusage_gnuplot_dump(void);

/*Dump the bufferd data of net usage from the buffer.
This Function is called from the kdebug menu. It prints the data
same as printed by cat /proc/netusage*/
void sec_netusage_dump(void);

/*Enable or disable the internal processing of netusage.
This same as writing the /proc/netusage*/
void sec_netusage_enable_disable(void);

/*
 *Turn the prints of netusage on
 *or off depending on the previous status.
 */
void sec_netusage_prints_OnOff(void);

/*
 * get status
 */
void get_netusage_status(void);
/*
 * Destroy all the allocated memory
 */
void sec_netusage_destroy(void);

void sec_netusage_OnOff(void);

/*
 * Structure that holds data.
 */
struct sec_netusage_struct {
    unsigned long sec;   /*maintains the seconds
				count at which the data is updated.*/
    int rx;   /*packet size taken by profiler
			       while receiving the packet*/
    int tx;   /*packet size taken by profiler
				while sending the packet*/
};
#endif /* __LINUX_NETUSAGE_H__ */
