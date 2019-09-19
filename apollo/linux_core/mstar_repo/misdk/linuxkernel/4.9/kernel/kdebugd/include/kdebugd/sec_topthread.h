/*
 *  linux/kernel/sec_topthread.h
 *
 *  CPU Performance Profiling Solution, topthread releated functions definitions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-11  Created by Karunanithy.
 *
 */

#ifndef _LINUX_SEC_TOPTHREAD_H
#define _LINUX_SEC_TOPTHREAD_H

#include <linux/sched.h>
#include <kdebugd.h>

/* consider less then 200 thread will run in a second */
#define TOPTHREAD_MAX_THREAD            100

/* to show cpu usage in 00.00% format */
#define TOPTHREAD_CPUUSAGE_DECIMAL_FORMAT       100

/* threadinfo display duration in sec */
#define TOPTHREAD_DELAY_SECS            1
#define TOPTHREAD_UPDATE_TICKS          (TOPTHREAD_DELAY_SECS * HZ)
#define TOPTHREAD_TITLE_ROW_1           0       /* title row one */
#define TOPTHREAD_TITLE_ROW_2           1       /* title row two */
#define TOPTHREAD_TITLE_ROW_3           2       /* title row thredd */

/* #define TOPTHREAD_DEBUG  */

#ifdef TOPTHREAD_DEBUG
#define SEC_TOPTHREAD_DEBUG(fmt, args...) PRINT_KD(fmt, args)
#else
#define SEC_TOPTHREAD_DEBUG(fmt, args...)
#endif

/*
  * Last 120 topthread info will be buffered in proc
  */
#define TOPTHREAD_MAX_PROC_ENTRY	CONFIG_KDEBUGD_TOPTHREAD_BUFFER_SIZE

#define TOPTHREAD_MAX_ITEM             5 /* we print 5 thread information */

/* TASK_COMM_LEN defined in linux/sched.h, value is 16*/
#define SEC_MAX_NAME_LEN     TASK_COMM_LEN

/* top thread usage storage structure */
struct topthread_info_entry {
	pid_t pid;  /* to store process ID */
	unsigned int tick_cnt; /* to store cpu usage */
};

/* Log of all pid b
 * but keep name of only top cpu taking threads per CPU*/
struct topthread_item_per_cpu {
	int max_thread;
	struct topthread_info_entry cpu_table[TOPTHREAD_MAX_THREAD];
	unsigned char name[TOPTHREAD_MAX_ITEM][SEC_MAX_NAME_LEN];
};


/* Top CPU taking threads in all cpu's collectively */
struct topthread_item_per_pid {
	struct topthread_info_entry th_info;
	unsigned char name[SEC_MAX_NAME_LEN];
};

/* top thread buffer structure */
struct thread_info_result_table {
	unsigned long sec;
	/* Context switch count */
	unsigned long sec_topthread_ctx_cnt[NR_CPUS];
	/* Item for all pid of each CPU */
	struct topthread_item_per_cpu item_per_cpu[NR_CPUS];

#ifdef CONFIG_SMP
	/* Item for top CPU taking pid collectively */
	struct topthread_item_per_pid item_per_pid[TOPTHREAD_MAX_ITEM];
#endif
	/* Total CPU*/
	unsigned int total[NR_CPUS];

};

/* top thread usage buffer */
struct thread_info_table {
	unsigned long sec;
	int available;
	/* Context switch count */
	unsigned long sec_topthread_ctx_cnt[NR_CPUS];
	int max_thread[NR_CPUS];
	struct topthread_info_entry  info[NR_CPUS * TOPTHREAD_MAX_THREAD];
};
extern DEFINE_PER_CPU(int, sec_topthread_ctx_cnt);
/*
  * To enable/disable topthread cpu usage display
  */
void sec_topthread_show_cpu_usage (int option);

/*
  * Function used to show the last n items of the topthread info from topthread queue
  */
void sec_topthread_dump_cpu_usage (int option);

/*
 * get status
 */
void sec_topthread_get_status(void);
/*
 * Destroy all the allocated memory
 */
void sec_topthread_off(void);

void sec_topthread_on_off(void);

void turnoff_cpu_usage(void);

int show_cpu_usage(void);

/*
  * This function is used to handle topthread info storage and validation
  * It is called from timer interrupt handler
  */
void sec_topthread_timer_interrupt_handler(int cpu);

#endif	/* !_LINUX_SEC_TOPTHREAD_H */
