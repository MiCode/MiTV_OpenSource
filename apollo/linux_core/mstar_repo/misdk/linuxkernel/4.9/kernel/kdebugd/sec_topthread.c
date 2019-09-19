/*
 *  linux/kernel/sec_topthread.c
 *
 *  CPU Performance Profiling Solution, topthread releated functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-11  Created by gupta.namit.
 *
 *  2012-05-14	topprocess added by vivek.kumar
 *
 */

#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <asm-generic/cputime.h>

#include <linux/sort.h>
#include <linux/delay.h>
#include <linux/ctype.h>

#include <sec_topthread.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include "kdbg_util.h"
#include "sec_workq.h"
#include <linux/atomic.h>

#define JIFFI_TO_PERCENT(x)	(((int)(x)) * 100 / ((int)HZ))

#define TOPTHREAD_PERCENTAGE 100

#define TOTAL_PERCENTAGE (TOPTHREAD_PERCENTAGE * NR_CPUS)

#define THREADINFO_CHAIN_LENGTH 2

#define MENU_ITEM  (NR_CPUS+1)

#define CFS_HEADER "#                            \n"\
"#  TASK NAME        PID PRIORITY CPU USAGE\n"\
"#       |             |   |      |\n"\

/* Flag for showing the header
 * header should be shown once at the start*/
static int g_header_show;

/* Take the lock before writing in buffer*/
static DEFINE_SPINLOCK(sec_topthread_write_lock);

/* Flip buffer for logging top thread usage */
static struct thread_info_table
*g_thread_info_table_chain[THREADINFO_CHAIN_LENGTH] = { NULL };

/* top thread display using sysrq on/off flag */
static int topthread_disp_flag;

/* CFS scheduler print option*/
static int cfs_print_option;

DEFINE_PER_CPU(int, sec_topthread_ctx_cnt);

/* State flag, reason for taking atomic is all the code is
 * running with state transition which is done by veriouse
 * thread in multiprocessor system keep accounting of this
 * variable is necessary */
static atomic_t g_sec_topthread_state = ATOMIC_INIT(E_NONE);

/* 2 funcitons get_state and set_state are used for maintaining the
 * state of system
 * */
static inline sec_counter_mon_state_t sec_topthread_get_state(void);
static int sec_topthread_set_state(sec_counter_mon_state_t new_state, int sync);

/* Destroy the top thread */
static void sec_topthread_destroy_impl(void);

/* The 2 Flip buffer used for read and write the buffer asynchronously */
/* initialy keep g_writer=0 (start cousuming 0th buffer)*/
static int g_writer;	/* Writer */
/* static int g_reader =1;Reader */

typedef enum {
	BY_CPU_TIME = 1,
	BY_PID
} sec_topthread_sort;

/* Spinlocks for accessing global reader and writer buffer
 * Each lock for all flip buffer */
spinlock_t sec_topthread_lock;

/* to store top 5 thread info to store at proc file */
static struct thread_info_result_table *g_ptopthread_proc_buff_;

/* to store top 5 process info at proc file */
static struct thread_info_result_table *g_ptopprocess_proc_buff_;

/* write index of the circular buffer array for top thread*/
static atomic_t g_topthread_proc_entry_idx = ATOMIC_INIT(E_NONE);

/* write index of the circular buffer array for top process*/
static atomic_t g_topprocess_proc_entry_idx = ATOMIC_INIT(E_NONE);

static atomic_t topprocess_buffer_full = ATOMIC_INIT(E_NONE); /* flag which incates buffer full state for top process */
static atomic_t topthread_buffer_full = ATOMIC_INIT(E_NONE); /* flag which incates buffer full state */

/* Print for CFS scheduler */
static void csf_show_entry(int index);

/*
 * whether its in top thread context or top process context
 */
typedef enum {
	TOP_THREAD_CONTEXT = 0,
	TOP_PROCESS_CONTEXT
} sec_counter_mon_top_context;

/* context value sent during top thread store info*/
static atomic_t g_sec_top_context_copy = ATOMIC_INIT(TOP_THREAD_CONTEXT);

/* Gettop context that is sent to top thread store info */
static inline sec_counter_mon_top_context sec_get_top_context_copy(void)
{
	return atomic_read(&g_sec_top_context_copy);
}

/* Sets top context that is sent to top thread store info */
void sec_set_top_context_copy(sec_counter_mon_top_context context)
{
	atomic_set(&g_sec_top_context_copy, context);
}

/*
 * Stores whether counter monitor is in top thread or top process context
 */
static atomic_t g_sec_top_context = ATOMIC_INIT(TOP_THREAD_CONTEXT);

/* Get the current counter monitor top context */
static inline sec_counter_mon_top_context sec_get_top_context(void)
{
	return atomic_read(&g_sec_top_context);
}

/*
 *Sets the counter monitor top context
 */
void sec_set_top_context(sec_counter_mon_top_context context)
{
	atomic_set(&g_sec_top_context, context);
}

/* This function is to show topthread info header */
static void topthread_get_header(int header_row)
{
	int loop;

	/* write cp thread info header */
	switch (header_row) {
	case TOPTHREAD_TITLE_ROW_1:
		PRINT_KD("\n\n");
		PRINT_KD("time   total  ");

		for (loop = 0; loop < TOPTHREAD_MAX_ITEM-1; loop++)
			PRINT_KD("top#%d                ", loop + 1);

		PRINT_KD("top#%d        ", loop + 1);

		PRINT_KD("Etc\n");
		break;

	case TOPTHREAD_TITLE_ROW_2:
		PRINT_KD("              ");

		if (sec_get_top_context() == TOP_THREAD_CONTEXT) {
			for (loop = 0; loop < TOPTHREAD_MAX_ITEM; loop++)
				PRINT_KD("thread               ");
		} else {
			for (loop = 0; loop < TOPTHREAD_MAX_ITEM; loop++)
				PRINT_KD("process              ");
		}

		PRINT_KD("\n");
		break;

	case TOPTHREAD_TITLE_ROW_3:
		PRINT_KD("=====  === ");

		for (loop = 0; loop < TOPTHREAD_MAX_ITEM; loop++)
			PRINT_KD("=================== ");

		PRINT_KD("=====\n");
		break;

	default:
		PRINT_KD("Invalid header row %d\n", header_row);
		break;
	}
}

/* This function is to show topthread info header */
static void topthread_show_header(void)
{
	topthread_get_header(TOPTHREAD_TITLE_ROW_1);
	topthread_get_header(TOPTHREAD_TITLE_ROW_2);
	topthread_get_header(TOPTHREAD_TITLE_ROW_3);
}

static char *convert_taskname(const char *comm, char *out)
{
	int i = 0;
	while (comm[i] && i < TASK_COMM_LEN) {
		if (isprint (comm[i]))
			out[i] = comm[i];
		else
			out[i] = '.';
		i++;
	}

	if (i < TASK_COMM_LEN)
		out[i] = '\0';
	else
		out[TASK_COMM_LEN-1] = '\0';

	return out;
}

/*
 * This function is to show topthread info in the following format
 * 1sec   80.00%   21(60.00%)  22(15.00%)  50(5.00%)   81(0%)     85(0%)  (0%)
 * 2sec  100.00%  21(80.00%)  22(15.00%)  50(5.00%)   81(0%)     85(0%)  (0%)
 * 3sec   50.00%   22(40.00%)  50(5.00%)    21(5.00%)   81(0%)     85(0%)  (0%)
 * 4sec   40.00%   25(20.00%)  21(10.00%)  45(5.00%)   81(5.00%) 85(0%)  (0%)
 */

#ifdef CONFIG_SMP
static void topthread_show_given_entry(int idx, int disp_option)
{
	unsigned int topthread_cpu_time = 0;
	int loop = 0;
	int cpu = 0;
	unsigned int excess_usage = 0;
	int total_cpu = 0;
	char out[TASK_COMM_LEN] = {0,};
	struct thread_info_result_table *ptop_proc_buff_copy;
	static sec_counter_mon_top_context prev_ctx;

	/* Display the value only after context change */
	if (sec_get_top_context() != prev_ctx) {
		prev_ctx = sec_get_top_context();
		return;
	}

	if ((idx < 0) || (idx > (TOPTHREAD_MAX_PROC_ENTRY - 1))) {
		/* invalid entry */
		PRINT_KD("Invalid Index\n");
		return;
	}

	/* All CPU + collective top thread in all cpu + top thread in each cpu */
	if (disp_option > NR_CPUS + 2) {
		PRINT_KD("Invalid Display Option\n");
		return;
	}

	if (sec_get_top_context() == TOP_THREAD_CONTEXT)
		ptop_proc_buff_copy = g_ptopthread_proc_buff_;

	else
		ptop_proc_buff_copy = g_ptopprocess_proc_buff_;


	switch (disp_option) {
	case MENU_ITEM:	/* Show top thread of all CPU collectively */
	{
		int total_cpu_per = 0;
		PRINT_KD("%5ld  ", ptop_proc_buff_copy[idx].sec);

		for (cpu = 0; cpu < NR_CPUS; cpu++)
			total_cpu += ptop_proc_buff_copy[idx].total[cpu];

		total_cpu_per = JIFFI_TO_PERCENT(total_cpu);
		if (total_cpu_per > (TOTAL_PERCENTAGE))
			total_cpu_per = TOTAL_PERCENTAGE;

		PRINT_KD("%3d ", total_cpu_per);

		while (loop < TOPTHREAD_MAX_ITEM) {

			if (ptop_proc_buff_copy[idx].
					item_per_pid[loop].th_info.pid != 0) {

				PRINT_KD("%-9.9s %4d",
						convert_taskname (ptop_proc_buff_copy
						[idx].item_per_pid[loop].name, out),
						ptop_proc_buff_copy[idx].
						item_per_pid[loop].th_info.
						pid);

				PRINT_KD("(%3d) ",
						JIFFI_TO_PERCENT(
							ptop_proc_buff_copy
							[idx].item_per_pid[loop].
							th_info.tick_cnt));

				topthread_cpu_time +=
					ptop_proc_buff_copy[idx].
					item_per_pid[loop].th_info.tick_cnt;
			} else {
				PRINT_KD("              (  0) ");
			}
			loop++;
		}
		if (total_cpu > topthread_cpu_time) {
			excess_usage = (total_cpu - topthread_cpu_time);
		} else {
			excess_usage = 0;
		}
		PRINT_KD("(%3d)\n", JIFFI_TO_PERCENT (excess_usage));

	}
	break;
	default:
	{
		int tick_cnt = 0;
		loop = 0;
		topthread_cpu_time = 0;
		cpu = (disp_option) - 1;	/* Index starts from 0 */
		if (cpu < 0 || cpu >= NR_CPUS) {
			PRINT_KD("Invalid CPU\n");
			return;
		}
		PRINT_KD("%5ld  ", ptop_proc_buff_copy[idx].sec);

		total_cpu = JIFFI_TO_PERCENT(ptop_proc_buff_copy[idx].total[cpu]);
		if (total_cpu > TOPTHREAD_PERCENTAGE)
			 total_cpu = TOPTHREAD_PERCENTAGE;

		PRINT_KD("%3d ",  total_cpu);

		/* to show pid & cpu usage */
		while (loop < TOPTHREAD_MAX_ITEM) {
			if (ptop_proc_buff_copy[idx].item_per_cpu[cpu].cpu_table[loop].pid !=  0) {
				tick_cnt = JIFFI_TO_PERCENT(ptop_proc_buff_copy[idx].item_per_cpu[cpu].
									  cpu_table[loop].tick_cnt);
				if (tick_cnt > (TOPTHREAD_PERCENTAGE))
					tick_cnt = TOPTHREAD_PERCENTAGE;

				PRINT_KD("%-9.9s %4d",
						convert_taskname (ptop_proc_buff_copy[idx].
						item_per_cpu[cpu].name[loop], out),
						ptop_proc_buff_copy[idx].
						item_per_cpu[cpu].
						cpu_table[loop].pid);
				PRINT_KD("(%3d) ", tick_cnt);
			} else {
				PRINT_KD("              (  0) ");
			}

			topthread_cpu_time +=
				ptop_proc_buff_copy[idx].
				item_per_cpu[cpu].cpu_table[loop].tick_cnt;

			loop++;
		}
		/* to show running thread cpu usage excludes top thread  */
		if (ptop_proc_buff_copy[idx].total[cpu] > topthread_cpu_time) {
			excess_usage =
				(ptop_proc_buff_copy[idx].total[cpu]
				 - topthread_cpu_time);
		} else {
			excess_usage = 0;
		}
		PRINT_KD("(%3d)\n", JIFFI_TO_PERCENT(excess_usage));

	}
	break;
	}
}
#else
/* funciton show index entry in the console .*/
#define UNUSED(x) ((x) = (x))
static void topthread_show_given_entry(int idx, int disp_option)
{
	unsigned int topthread_cpu_time = 0;
	int loop = 0;
	unsigned int excess_usage;
	int cpu = 0;
	char out[TASK_COMM_LEN] = {0,};

	struct thread_info_result_table *ptop_proc_buff_copy;
	static sec_counter_mon_top_context prev_ctx;

	if (sec_get_top_context() != prev_ctx) {
		prev_ctx = sec_get_top_context();
		return;
	}

	/* option is not used.
	 * This is just added to keep the prototype similar to SMP version
	 */
	UNUSED(disp_option);

	if ((idx < 0) || (idx > (TOPTHREAD_MAX_PROC_ENTRY - 1)))
		/* invalid entry */
		return;

	if (sec_get_top_context() == TOP_THREAD_CONTEXT)
		ptop_proc_buff_copy = g_ptopthread_proc_buff_;

	else
		ptop_proc_buff_copy = g_ptopprocess_proc_buff_;

	/* to show sec */
	PRINT_KD("%5ld  ", ptop_proc_buff_copy[idx].sec);

	/* to show total cpu usage */
	PRINT_KD("%3d ",
		 JIFFI_TO_PERCENT(ptop_proc_buff_copy[idx].total[cpu]));

	/* to show pid & cpu usage */
	while (loop < TOPTHREAD_MAX_ITEM) {
		if (ptop_proc_buff_copy[idx].item_per_cpu[cpu].
		    cpu_table[loop].pid != 0) {
			PRINT_KD("%-9.9s %4d",
				 convert_taskname(ptop_proc_buff_copy[idx].item_per_cpu[cpu].
				 name[loop], out),
				 ptop_proc_buff_copy[idx].item_per_cpu[cpu].
				 cpu_table[loop].pid);
			PRINT_KD("(%3d) ",
				 JIFFI_TO_PERCENT(ptop_proc_buff_copy[idx].item_per_cpu[cpu].
				 cpu_table[loop].tick_cnt));
		} else {
			PRINT_KD("              (  0) ");
		}

		topthread_cpu_time +=
		    ptop_proc_buff_copy[idx].item_per_cpu[cpu].
		    cpu_table[loop].tick_cnt;

		loop++;
	}

	/* to show running thread cpu usage excludes top 5 thread  */
	if (ptop_proc_buff_copy[idx].total[cpu] > topthread_cpu_time) {
		excess_usage =
			(ptop_proc_buff_copy[idx].total[cpu] -
			 topthread_cpu_time);
	} else {
		excess_usage = 0;
	}
	PRINT_KD("(%3d)\n", JIFFI_TO_PERCENT(excess_usage));
}

#endif /*CONFIG_SMP */

/* To update the running task info in topthread structure */
static void topthread_store_info(int cpu)
{
	int loop;
	int cpu_index = (cpu * TOPTHREAD_MAX_THREAD);
	int write_index = 0;
	pid_t cur_pid;
	int max_no_of_thread = g_thread_info_table_chain[g_writer]->max_thread[cpu];
	static DEFINE_PER_CPU(int, max_no_exhaust);
	write_index =  cpu_index + max_no_of_thread;

	/* store pid in thread context and tgid in process context */
	if (sec_get_top_context() == TOP_THREAD_CONTEXT)
		cur_pid = current->pid;
	else
		cur_pid = current->tgid;

	/* no need to store idle task time */
	if (cur_pid == 0)
		return;

	/* calculate all task cpu usage */
	for (loop = cpu_index; loop < write_index; loop++) {
		if (cur_pid ==
				g_thread_info_table_chain[g_writer]->info[loop].pid) {
			g_thread_info_table_chain[g_writer]->info[loop].
				tick_cnt += jiffies_to_cputime(1);
			return;
		}
	}

	/*
	 * we expect there is no more than 200 (TOPTHREAD_MAX_THREAD * 2)
	 * 100 for Each CPU
	 * threads running between 1 second, so we don't need to care about
	 * the else part, ie., if (max_no_of_thread > TOPTHREAD_MAX_THREAD)
	 */
	if (max_no_of_thread < TOPTHREAD_MAX_THREAD) {
		g_thread_info_table_chain[g_writer]->
			info[write_index].pid = cur_pid;
		g_thread_info_table_chain[g_writer]->
			info[write_index].tick_cnt =
			jiffies_to_cputime(1);
		g_thread_info_table_chain[g_writer]->max_thread[cpu]++;
	} else {
		if (!per_cpu(max_no_exhaust, cpu)) {
			PRINT_KD(KERN_WARNING
					"Can not add thread info, max cpu_table reached in CPU %d\n",
					cpu);
			per_cpu(max_no_exhaust, cpu)++;
		}
	}
}

/* swap function for topthread_info_entry */
static void swap_topthread_info(void *va, void *vb, int size)
{
	struct topthread_info_entry *a = va, *b = vb;
	unsigned int tcpu_time = a->tick_cnt, tpid = a->pid;
	a->tick_cnt = b->tick_cnt;
	a->pid = b->pid;
	b->tick_cnt = tcpu_time;
	b->pid = tpid;
}

/* compare function for topthread_info_entry using tick_cnt */
static int cmp_topthread_info_cputime(const void *va, const void *vb)
{
	struct topthread_info_entry *a = (struct topthread_info_entry *)va, *b =
	    (struct topthread_info_entry *)vb;
	return b->tick_cnt - a->tick_cnt;
}

/* compare function for topthread_info_entry using tick_cnt */
static int cmp_topthread_info_pid(const void *va, const void *vb)
{
	struct topthread_info_entry *a = (struct topthread_info_entry *)va, *b =
	    (struct topthread_info_entry *)vb;
	return b->pid - a->pid;
}

/* heap sort the list.
 * example
 *	Before: 3 2 1 6 1 1 33 10 1 40
 *	After : 40 33 10 6 3 2 1 1 1 1
 */
static int topthread_heapSort(struct topthread_info_entry *buffer,
			      int array_size, sec_topthread_sort sort_type)
{
	int ret = 0;
	if (array_size) {
		if (sort_type == BY_CPU_TIME) {
			sort(buffer, array_size, sizeof(buffer[0]),
			     cmp_topthread_info_cputime, swap_topthread_info);
			ret = 1;
		} else if (sort_type == BY_PID) {
			sort(buffer, array_size, sizeof(buffer[0]),
			     cmp_topthread_info_pid, swap_topthread_info);
			ret = 1;
		} else {
			PRINT_KD("Sort type not exist\n");
		}
	}
	return ret;
}

#ifdef CONFIG_SMP
/* In this fucntion all the PID who are taking maximum CPU collectively
 * in all CPU will be copy in the display buffer
 * */
static int sec_topthread_assign_top_pid(struct thread_info_result_table *wr_ptr,
					struct thread_info_table *rd_ptr)
{

	int max_entry = NR_CPUS * TOPTHREAD_MAX_THREAD;
	int loop = 0, curr = 0, next = 1, adv = 0;

	/* sort the whole array with pid*/
	topthread_heapSort(rd_ptr->info, max_entry, BY_PID);

	/* Remove Duplicate Entries and add CPU of ALL dupliacte entries */
	for (loop = 0; loop < max_entry-1; loop++) {

		/* Check whethe next pointer exists ?? */
		if (!rd_ptr->info[next].pid)
			break;
		/* Both values are different do nothing advance both pointer by 1*/
		if (rd_ptr->info[curr].pid != rd_ptr->info[next].pid) {
			curr += (adv+1);
			next++;
			adv = 0;
		} else {
			/* Both values are equal, add cpu time and make next entry 0*/
			rd_ptr->info[curr].tick_cnt += rd_ptr->info[next].tick_cnt;
			rd_ptr->info[next].pid = 0;
			rd_ptr->info[next].tick_cnt = 0;
			/* Advance next pointer by 1 */
			next++;
			/* Advance current pointer by 1 */
			adv++;
		}
	}

	topthread_heapSort(rd_ptr->info, max_entry, BY_CPU_TIME);

	loop = 0;

	while (loop < TOPTHREAD_MAX_ITEM) {
		wr_ptr->item_per_pid[loop].th_info.pid = rd_ptr->info[loop].pid;
		wr_ptr->item_per_pid[loop].th_info.tick_cnt = rd_ptr->info[loop].tick_cnt;
		loop++;
	}

	return 0;
}
#endif /*CONFIG_SMP */

static int sec_topthread_assign_all_pid(struct thread_info_result_table *wr_ptr,
					struct thread_info_table *rd_ptr)
{
	int cpu_idx = 0;
	int loop = 0;

	int cpu_buf_idx = 0;
	int max_read_idx = 0;
	int i = 0;

	for (cpu_idx = 0; cpu_idx < NR_CPUS; cpu_idx++) {

		SEC_TOPTHREAD_DEBUG
			("No of threads in CPU BUFF-[%d] ---> [%d]\n", cpu_idx,
			 rd_ptr->max_thread[cpu_idx]);

		if (TOPTHREAD_MAX_THREAD > rd_ptr->max_thread[cpu_idx]) {
			wr_ptr->item_per_cpu[cpu_idx].max_thread =
				rd_ptr->max_thread[cpu_idx];
		} else {
			WARN_ON(rd_ptr->max_thread[cpu_idx] > TOPTHREAD_MAX_THREAD);
			wr_ptr->item_per_cpu[cpu_idx].max_thread =
				TOPTHREAD_MAX_THREAD;
		}
		SEC_TOPTHREAD_DEBUG("Assigned threads in CPU BUFF ---> [%d]\n",
				wr_ptr->item_per_cpu[cpu_idx].max_thread);

		wr_ptr->sec_topthread_ctx_cnt[cpu_idx] =
			rd_ptr->sec_topthread_ctx_cnt[cpu_idx];

		/* get the data from rearranged array of thread cpu usage */
		cpu_buf_idx = cpu_idx * TOPTHREAD_MAX_THREAD;
		max_read_idx = cpu_buf_idx + wr_ptr->item_per_cpu[cpu_idx].max_thread;

		loop = cpu_buf_idx;
		for (i = 0; i < wr_ptr->item_per_cpu[cpu_idx].max_thread; i++) {

			if (loop == max_read_idx)
				break;

			wr_ptr->item_per_cpu[cpu_idx].cpu_table[i].pid =
				rd_ptr->info[loop].pid;

			wr_ptr->item_per_cpu[cpu_idx].cpu_table[i].tick_cnt =
				rd_ptr->info[loop].tick_cnt;

			wr_ptr->total[cpu_idx] +=
				rd_ptr->info[loop].tick_cnt;
			loop++;

		}

	}
	return 0;
}

/*
 *  these 3 funciton are used for maintaing display buffer index for top thread
 *  1- reset funciton reset the buffer index.
 *  2- get_index provide the index
 *  3- incr_index increment and maintain rollback of the function.
 *  at the time of rollback index will be reset and buffer full
 *  option will be set.
 *  */

static void topthread_reset_proc_index(void)
{
	atomic_set(&g_topthread_proc_entry_idx, 0);
}

static int topthread_get_proc_index(void)
{
	return atomic_read(&g_topthread_proc_entry_idx);
}

static void topthread_incr_proc_index(void)
{
	atomic_inc(&g_topthread_proc_entry_idx);

	if (topthread_get_proc_index() >= TOPTHREAD_MAX_PROC_ENTRY) {
		atomic_set (&topthread_buffer_full, 1);
		topthread_reset_proc_index();
	}
}

/*
 *  these 3 funciton are used for maintaing display buffer index for top process
 *  1- reset funciton reset the buffer index.
 *  2- get_index provide the index
 *  3- incr_index increment and maintain rollback of the function.
 *  at the time of rollback index will be reset and buffer full
 *  option will be set.
 *  */

static void topprocess_reset_proc_index(void)
{
	atomic_set(&g_topprocess_proc_entry_idx, 0);
}

static int topprocess_get_proc_index(void)
{
	return atomic_read(&g_topprocess_proc_entry_idx);
}

static void topprocess_incr_proc_index(void)
{
	atomic_inc(&g_topprocess_proc_entry_idx);

	if (topprocess_get_proc_index() >= TOPTHREAD_MAX_PROC_ENTRY) {
		atomic_set (&topprocess_buffer_full, 1);
		topprocess_reset_proc_index();
	}
}


/* Find top threads info and display
 * this fucntion is a core parsing function.
 * the data which have been collected at the time of timer interrupt,
 * is parsed here
 * */
void topthread_find_top5thread_n_display(int buf_index)
{
	int loop;
	static int time_count;	/* to keep the seconds display */
	struct task_struct *tsk = NULL;
	int cpu_idx = 0;
	int max_no_of_thread = 0;

	struct thread_info_result_table *ptop_proc_buff_copy;
	int proc_buff_index;

	sec_counter_mon_state_t state = sec_topthread_get_state();

	if (state != E_RUNNING && state != E_RUN_N_PRINT) {
		/*PRINT_KD("%s: topthread state= %d: ignoring\n", __FUNCTION__, state); */
		return;
	}

	/* Buffer is destroyed or not allocated !! */
	if (!g_thread_info_table_chain[buf_index]) {
		PRINT_KD("Reader Buffer not allocated returning ..\n");
		return;
	}

	if (sec_get_top_context() == TOP_THREAD_CONTEXT) {
		ptop_proc_buff_copy = g_ptopthread_proc_buff_;
		proc_buff_index = topthread_get_proc_index();
	}

	else {
		ptop_proc_buff_copy = g_ptopprocess_proc_buff_;
		proc_buff_index = topprocess_get_proc_index();
	}

	/* BUG:flag is on but no memory allocated !! */
	WARN_ON(!ptop_proc_buff_copy);

	if (!ptop_proc_buff_copy) {
		PRINT_KD("Display Buffer no allocated returning ..\n");
		return;
	}

	/* initialize to zero */
	memset(&ptop_proc_buff_copy[proc_buff_index], 0,
			sizeof(struct thread_info_result_table));

	/* Sort the array of thread cpu usage
	 * to reduce the rearrange time use the quick sort algorithm
	 */
	ptop_proc_buff_copy[proc_buff_index].sec =
		g_thread_info_table_chain[buf_index]->sec;

	for (cpu_idx = 0; cpu_idx < NR_CPUS; cpu_idx++) {
		max_no_of_thread =
			g_thread_info_table_chain[buf_index]->max_thread[cpu_idx];

		if (max_no_of_thread > 0) {
			topthread_heapSort(&g_thread_info_table_chain[buf_index]->info[cpu_idx * TOPTHREAD_MAX_THREAD],
					max_no_of_thread, BY_CPU_TIME);
		}
	}

	sec_topthread_assign_all_pid(&ptop_proc_buff_copy[proc_buff_index],
			g_thread_info_table_chain[buf_index]);

#ifdef CONFIG_SMP
	sec_topthread_assign_top_pid(&ptop_proc_buff_copy[proc_buff_index],
			g_thread_info_table_chain[buf_index]);
#endif /* CONFIG_SMP */

	for (cpu_idx = 0; cpu_idx < NR_CPUS; cpu_idx++) {
		g_thread_info_table_chain[buf_index]->max_thread[cpu_idx] = 0;
	}

	memset(g_thread_info_table_chain[buf_index]->info, 0,
			NR_CPUS * TOPTHREAD_MAX_THREAD *
			sizeof(struct topthread_info_entry));

	/* lock the assigned avaible */
	spin_lock(&sec_topthread_lock);

	g_thread_info_table_chain[buf_index]->available = 1;

	/* Release the lock */
	spin_unlock(&sec_topthread_lock);

	/* to reset tothread buffer just reset the below variable */
	max_no_of_thread = 0;

	if (sec_topthread_get_state() == E_RUN_N_PRINT && cfs_print_option)
		csf_show_entry(proc_buff_index);

	loop = 0;
	while (loop < TOPTHREAD_MAX_ITEM) {
		/* Refer to kernel/pid.c
		 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
		 *  PID-map pages start out as NULL, they get allocated upon
		 *  first use and are never deallocated. This way a low pid_max
		 *  value does not cause lots of bitmaps to be allocated, but
		 *  the scheme scales to up to 4 million PIDs, runtime.
		 */
		for (cpu_idx = 0; cpu_idx < NR_CPUS; cpu_idx++) {
			if (ptop_proc_buff_copy[proc_buff_index].
					item_per_cpu[cpu_idx].cpu_table[loop].pid) {

				rcu_read_lock();
				/* get the data from rearranged array of thread cpu usage */
				tsk =
					find_task_by_pid_ns(ptop_proc_buff_copy
							[proc_buff_index].item_per_cpu
							[cpu_idx].
							cpu_table[loop].pid,
							&init_pid_ns);
				/*Unlock */
				rcu_read_unlock();

				if (tsk) {
					get_task_comm(ptop_proc_buff_copy
							[proc_buff_index].
							item_per_cpu[cpu_idx].
							name[loop], tsk);
				} else {
					strncpy(ptop_proc_buff_copy
							[proc_buff_index].item_per_cpu
							[cpu_idx].name[loop], "<th_dead>",
							SEC_MAX_NAME_LEN - 1);
				}

				ptop_proc_buff_copy[proc_buff_index].
					item_per_cpu[cpu_idx].name[loop]
					[SEC_MAX_NAME_LEN - 1] = '\0';
			}

		}

#ifdef CONFIG_SMP

		if (ptop_proc_buff_copy[proc_buff_index].item_per_pid[loop].
				th_info.pid) {

			/*Take RCU read lock register can be changed */
			rcu_read_lock();

			tsk =
				find_task_by_pid_ns(ptop_proc_buff_copy
						[proc_buff_index].item_per_pid
						[loop].th_info.pid,
						&init_pid_ns);
			/*Unlock */
			rcu_read_unlock();

			if (tsk) {
				get_task_comm(ptop_proc_buff_copy
						[proc_buff_index].
						item_per_pid[loop].name, tsk);
			} else {
				strncpy(ptop_proc_buff_copy
						[proc_buff_index].item_per_pid[loop].
						name, "<th_dead>", SEC_MAX_NAME_LEN - 1);
			}

			ptop_proc_buff_copy[proc_buff_index].
				item_per_pid[loop].name[SEC_MAX_NAME_LEN - 1] =
				'\0';
		}
#endif /*CONFIG_SMP */

		loop++;
	}

	/* if show flag is on, then show it to user */
	if (sec_topthread_get_state() == E_RUN_N_PRINT) {

		if (topthread_disp_flag) {
			time_count++;
			if ((time_count % 20) == 0)
				topthread_show_header();
			topthread_show_given_entry(proc_buff_index,
					topthread_disp_flag);
		}
	}

	if (sec_get_top_context() == TOP_THREAD_CONTEXT)
		topthread_incr_proc_index();

	else
		topprocess_incr_proc_index();

}

/* Turn off the prints of topthread
 */
void turnoff_topthread(void)
{
	if (sec_topthread_get_state() == E_RUN_N_PRINT) {
		/* Send with sync */
		sec_topthread_set_state(E_RUNNING, 1);
		topthread_disp_flag = 0;
		PRINT_KD("\nTOPTHREAD> Off\n");

	}


	turnoff_cpu_usage();
}

/*
 * This function is called at every topthread info sysrq request
 * It is used to enable/disable the topthread info display
 * Once it is enabled, it will the topthread header text and reset necessary flags
 */
void sec_topthread_show_cpu_usage(int option)
{
	int ret = 0;
	sec_counter_mon_state_t state = sec_topthread_get_state();

	if (state == E_RUNNING) {
		topthread_show_header();
		/* Send with sync */
		sec_topthread_set_state(E_RUN_N_PRINT, 1);
		topthread_disp_flag = option;
	} else if (state == E_RUN_N_PRINT) {
		/* If CFS is Running toggle only
		 * top thread display flag */
		if (!cfs_print_option) {
			/* Send with sync */
			ret = sec_topthread_set_state(E_RUNNING, 1);
			topthread_disp_flag = 0;
		} else {
			topthread_disp_flag = topthread_disp_flag ?
			    !topthread_disp_flag : option;
		}
	} else {
		ret = -ERR_INVALID;
		PRINT_KD("Invalid cpuusate state= %d\n", state);
		/* TODO: WARN_ON can be better */
		BUG_ON("Invalid topthread state");
	}

	if (ret)
		PRINT_KD
		    ("ERROR: topthread prints on / off: operation failed with"
		     " error code= %d\n", ret);
}

/*
 * This will dump the bufferd data of topthread cpu usage from the buffer.
 * This Function is called from the kdebug menu.
 */
void sec_topthread_dump_cpu_usage(int option)
{
	int i = 0;
	int buffer_count = 0;
	int idx = 0;

	sec_counter_mon_state_t state = sec_topthread_get_state();

	if (state == E_NONE || state >= E_DESTROYING)
		return;

	if (sec_get_top_context() == TOP_THREAD_CONTEXT) {
		if (atomic_read(&topthread_buffer_full)) {
			buffer_count = TOPTHREAD_MAX_PROC_ENTRY;
			idx = topthread_get_proc_index();
		} else {
			buffer_count = topthread_get_proc_index();
			idx = 0;
		}
	}

	else {
		if (atomic_read (&topprocess_buffer_full)) {
			buffer_count = TOPTHREAD_MAX_PROC_ENTRY;
			idx = topprocess_get_proc_index();
		} else {
			buffer_count = topprocess_get_proc_index();
			idx = 0;
		}
	}

	topthread_show_header();

	for (i = 0; i < buffer_count; i++, idx++) {
		idx = idx % TOPTHREAD_MAX_PROC_ENTRY;

		/*  There are 8 menu out of this 4 for periodic print
		*  submenu and 4 dump menu,
		*  sending only 4 menu will solve our purpose*/
		topthread_show_given_entry(idx, option);
	}
}

/* The function checks the available buffer
 * return first available buffer index after
 * write index.
 * if no buffers are free return -1
 * */
static int sec_get_available_table_idx(void)
{
	int idx = (g_writer + 1) % THREADINFO_CHAIN_LENGTH;
	int i = 0;

	SEC_TOPTHREAD_DEBUG("Write Index:[%d] start search idx[%d]\n", g_writer,
			idx);
	/* Check all index next to write index. */
	spin_lock(&sec_topthread_lock);
	for (i = 0; i < THREADINFO_CHAIN_LENGTH - 1; i++) {
		if (g_thread_info_table_chain[idx]->available) {
			SEC_TOPTHREAD_DEBUG("Got the Index: %d\n", idx);
			break;
		}
		idx = (idx + 1) % THREADINFO_CHAIN_LENGTH;
	}
	spin_unlock(&sec_topthread_lock);

	return (i == THREADINFO_CHAIN_LENGTH - 1 ? -1 : idx);
}

/* The function change the buffer and notify worker
 * thread for process the buffer */
static int sec_topthread_notify_reader(int idx_writer)
{
	struct kdbg_work_t top_thread_work;
	int ret = 0;
	int ret_idx = sec_get_available_table_idx();
	if (ret_idx  >= 0) {
		SEC_TOPTHREAD_DEBUG("Return Table Index: %d\n", ret_idx);
		/* Post work */
		top_thread_work.data = (void *)(long)idx_writer;
		top_thread_work.pwork =
			(void *)topthread_find_top5thread_n_display;

		/* Add the work to the queue .. */
		kdbg_workq_add_event(top_thread_work, NULL);

		/* Change the reader and writer */
		/*              g_reader = g_writer; */
		g_writer = ret_idx;
		SEC_TOPTHREAD_DEBUG("Reader Index: %d  Writer Index: %d\n",
				idx_writer, g_writer);
		ret = 1;
	}
	return ret;
}
/*This function called by tothread_hrtimer at one second interval*/
static void sec_topthread_process_data(void)
{
	spin_lock(&sec_topthread_write_lock);
	/* update context every one second */
	if (sec_get_top_context_copy() == TOP_THREAD_CONTEXT)
		sec_set_top_context(TOP_THREAD_CONTEXT);
	else
		sec_set_top_context(TOP_PROCESS_CONTEXT);

	if (g_thread_info_table_chain[g_writer]) {
		int temp_cpu = 0;
		g_thread_info_table_chain[g_writer]->sec =
			kdbg_get_uptime();
		g_thread_info_table_chain[g_writer]->available =
			0;

		for_each_online_cpu(temp_cpu) {

			g_thread_info_table_chain[g_writer]->
				sec_topthread_ctx_cnt[temp_cpu] =
				per_cpu(sec_topthread_ctx_cnt,
						temp_cpu);
			/* Each time reset the context switch variable */
			per_cpu(sec_topthread_ctx_cnt,
					temp_cpu) = 0;
		}
		/*TODO:sec_topthread_notify_reader fail*/
		/* send event and flip currently used buffer */
		sec_topthread_notify_reader(g_writer);
	}
	spin_unlock(&sec_topthread_write_lock);
}
/* This function is called at every timer interrupt tick.
 * update topthread buffer
 */
void sec_topthread_timer_interrupt_handler(int cpu)
{
	sec_counter_mon_state_t state = E_NONE;

	state = sec_topthread_get_state();

	if (state != E_RUNNING && state != E_RUN_N_PRINT) {
		/*              PRINT_KD("%s: topthread state= %d: ignoring\n", __FUNCTION__, state); */
		return;
	}

	spin_lock(&sec_topthread_write_lock);
	if (g_thread_info_table_chain[g_writer]->available) {

		/* add and update task info into topthreadinfo structure
		 * check for run state and then update */
		topthread_store_info(cpu);

	}
	spin_unlock(&sec_topthread_write_lock);
}

/* Initialize the cpu usage buffer and global variables.
 * Return 0 on success, negative value as error code
 * */
int sec_topthread_create(void)
{
	sec_counter_mon_state_t state = sec_topthread_get_state();

	WARN_ON(state != E_NONE && state != E_DESTROYED);

	/* Check for the state first */
	if (state > E_NONE && state < E_DESTROYED) {
		PRINT_KD("CPU USAGE ERROR: Already Initialized\n");
		return -ERR_INVALID;
	}
	return 0;
}

/* Initialization of sec_topthread,
 * all the buffer has been  initialized and allocated here
 */
int sec_topthread_init(void)
{
	int i = 0;

	for (i = 0; i < THREADINFO_CHAIN_LENGTH; i++) {
		/* Ensure the Buufer should be NULL */
		BUG_ON(g_thread_info_table_chain[i]);
		/*Using in timer context, use with GFP_ATOMIC flag */
		g_thread_info_table_chain[i] = (struct thread_info_table *)
			KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
					sizeof(struct thread_info_table),
					GFP_ATOMIC);

		if (!g_thread_info_table_chain[i]) {
			PRINT_KD("TOP THREAD ERROR: Insuffisient memory\n");
			goto ERR;
		}
		memset(g_thread_info_table_chain[i], 0,
				sizeof(struct thread_info_table));
		g_thread_info_table_chain[i]->available = 1;
	}
	sec_topthread_lock = __SPIN_LOCK_UNLOCKED(sec_topthread_lock);

	/*Using in timer context, use with GFP_ATOMIC flag */
	g_ptopthread_proc_buff_ = (struct thread_info_result_table *)
		KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				TOPTHREAD_MAX_PROC_ENTRY *
				sizeof(struct thread_info_result_table),
				GFP_ATOMIC);

	if (!g_ptopthread_proc_buff_) {
		PRINT_KD("TOP THREAD ERROR: Insuffisient memory\n");
		goto ERR;
	}

	memset(g_ptopthread_proc_buff_, 0, TOPTHREAD_MAX_PROC_ENTRY *
			sizeof(struct thread_info_result_table));

	g_ptopprocess_proc_buff_ = (struct thread_info_result_table *)
		KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				TOPTHREAD_MAX_PROC_ENTRY *
				sizeof(struct thread_info_result_table),
				GFP_ATOMIC);

	if (!g_ptopprocess_proc_buff_) {
		PRINT_KD("TOP THREAD ERROR: Insuffisient memory\n");
		goto ERR;
	}

	memset(g_ptopprocess_proc_buff_, 0, TOPTHREAD_MAX_PROC_ENTRY *
			sizeof(struct thread_info_result_table));

	g_writer = 0;		/* Writer */

	topthread_reset_proc_index();
	topprocess_reset_proc_index();

	return true;

ERR:
	/* On Err deallocate all the allocation */
	for (i = 0; i < THREADINFO_CHAIN_LENGTH; i++) {
		if (g_thread_info_table_chain[i]) {
			KDBG_MEM_DBG_KFREE(g_thread_info_table_chain[i]);
			g_thread_info_table_chain[i] = NULL;
		}
	}

	if (g_ptopthread_proc_buff_) {
		KDBG_MEM_DBG_KFREE(g_ptopthread_proc_buff_);
		g_ptopthread_proc_buff_ = NULL;
	}

	if (g_ptopprocess_proc_buff_) {
		KDBG_MEM_DBG_KFREE(g_ptopprocess_proc_buff_);
		g_ptopprocess_proc_buff_ = NULL;
	}

	return false;
}

/* Get the current state of topthread */
static inline sec_counter_mon_state_t sec_topthread_get_state(void)
{
	return atomic_read(&g_sec_topthread_state);
}

/*
 *Turn off the prints of topthread on
 */
void turnoff_sec_topthread(void)
{
	if (sec_topthread_get_state() == E_RUN_N_PRINT) {
		/* Send with sync */
		sec_topthread_set_state(E_RUNNING, 1);
		topthread_disp_flag = 0;
	}
	PRINT_KD("TOP THREAD > OFF\n");
}

/*topthread hrtimer to process data at 1 sec interval*/
static struct hrtimer topthread_timer;
static struct timespec tv = {.tv_sec = 1, .tv_nsec = 0};
/*Function handler for topthread_timer*/
static enum hrtimer_restart topthread_hrtimer_handler(struct hrtimer *ht)
{
	/*process data at every second and forward timer*/
	sec_topthread_process_data();
	hrtimer_forward(&topthread_timer, ktime_get(), timespec_to_ktime(tv));
	return HRTIMER_RESTART;
}
/* this is internal function for set state,
 * this function has been sent to worker thread for setting state.
 * For Strict checking of all the state BUG_ON has been used.
 * */
int sec_topthread_set_state_impl(sec_counter_mon_state_t new_state)
{
	int ret = 0;
	sec_counter_mon_state_t prev_state = sec_topthread_get_state();

	SEC_TOPTHREAD_DEBUG("old state %d --> new state %d\n", prev_state,
			new_state);

		switch (new_state) {
		case E_INITIALIZED:
			if (prev_state == E_NONE || prev_state == E_DESTROYED) {
				/* Initialize the topthread buffer */
				int cpu = 0;
				if (!sec_topthread_init())
					return -1;

				for_each_online_cpu(cpu)
					per_cpu(sec_topthread_ctx_cnt, cpu) = 0;

				for_each_online_cpu(cpu)
					per_cpu(sec_topthread_ctx_cnt, cpu) = 0;

				/*ON init flag.. */
				atomic_set(&g_sec_topthread_state, E_INITIALIZED);
				PRINT_KD("Initialization done ..\n");

			} else if (prev_state == E_RUNNING
					|| prev_state == E_RUN_N_PRINT) {
				atomic_set(&g_sec_topthread_state, E_INITIALIZED);
				atomic_set (&topprocess_buffer_full, 0);
				atomic_set (&topthread_buffer_full, 0);
				/*cancel hrtimer*/
				hrtimer_cancel(&topthread_timer);
			} else if (prev_state == E_INITIALIZED) {
				PRINT_KD("Already Initialized\n");
				ret = -ERR_DUPLICATE;
			} else {
				/* TODO: WARN_ON can be better */
				BUG_ON(prev_state == E_DESTROYING);	/* internal transition state */
				BUG_ON("Invalid topthread state");
			}
			break;
		case E_RUNNING:
			if (prev_state == E_INITIALIZED) {
				atomic_set(&g_sec_topthread_state, E_RUNNING);
				/*create hrtimer*/
				hrtimer_init(&topthread_timer, CLOCK_MONOTONIC,
						HRTIMER_MODE_REL);
				topthread_timer._softexpires =
					timespec_to_ktime(tv);
				topthread_timer.function =
					topthread_hrtimer_handler;
				hrtimer_start(&topthread_timer,
						topthread_timer._softexpires,
						HRTIMER_MODE_REL);
				PRINT_KD("Running status done ..\n");
			} else if (prev_state == E_RUN_N_PRINT) {
				atomic_set(&g_sec_topthread_state, E_RUNNING);
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
				BUG_ON("Invalid topthread state");
			}
			break;
		case E_RUN_N_PRINT:
			if (prev_state == E_RUNNING) {
				atomic_set(&g_sec_topthread_state, E_RUN_N_PRINT);
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
				BUG_ON("Invalid topthread state");
			}
			break;
		case E_DESTROYED:

			if (prev_state == E_INITIALIZED) {

				/*                              init_completion(&done); */
				/* First set the state so that interrupt stop all the work */
				atomic_set(&g_sec_topthread_state, E_DESTROYING);
				sec_topthread_destroy_impl();
				/*                              sec_topthread_destroy_work.pwork =  */
				/*                              kdbg_workq_add_event(sec_topthread_destroy_work, &done);   */
				/*                              wait_for_completion(&done); */
				/*                              msleep(500);     */
				atomic_set(&g_sec_topthread_state, E_DESTROYED);

			} else if (prev_state == E_DESTROYING) {
				PRINT_KD("Already Destroying state\n");
				ret = -ERR_DUPLICATE;
			} else {
				ret = -ERR_INVALID;
				BUG_ON(prev_state == E_DESTROYING);	/*internal transition state */
				BUG_ON("Invalid topthread state");
			}

			break;
		default:
			PRINT_KD("ERROR: Invalid State argument\n");
			ret = -ERR_INVALID_ARG;
			break;
		}
	return ret;
}

/* From this function state change message is send to worker threed.
 * worker thread work in
 * 2 mode--
 * send with sync and send without sync.
 * */
static int sec_topthread_set_state(sec_counter_mon_state_t new_state, int sync)
{
	struct kdbg_work_t sec_topthread_destroy_work;
	struct completion done;
	int ret = 0;

	if (sync) {
		init_completion(&done);

		sec_topthread_destroy_work.data = (void *)new_state;
		sec_topthread_destroy_work.pwork =
		    (void *)sec_topthread_set_state_impl;
		kdbg_workq_add_event(sec_topthread_destroy_work, &done);

		wait_for_completion(&done);

		/* Check the failed case */
		if (sec_topthread_get_state() != new_state) {
			PRINT_KD("SEC TOPTHREAD ERROR: State change Failed\n");
			ret = -1;
		}
	} else {
		sec_topthread_set_state_impl(new_state);
	}

	return ret;

}

/*Get the state */
void sec_topthread_get_status(void)
{
	switch (sec_topthread_get_state()) {
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
 * the resource allocation by top thread*/
void sec_topthread_off(void)
{
	sec_counter_mon_state_t prev_state = sec_topthread_get_state();

	if (prev_state > E_NONE && prev_state < E_DESTROYING) {
		/* Now destroy */
		while (prev_state > E_INITIALIZED) {
			/* Send with sync */
			sec_topthread_set_state(--prev_state, 1);
		}
		/* System is in E_INITIALIZED State now destroyed it
		 * and free all the resources*/
		/* Send with sync */
		sec_topthread_set_state(E_DESTROYED, 1);
	} else {
		PRINT_KD("State is either destroyed or Invalid %d\n",
			 prev_state);
	}
}

/* Destroy all the allocations and deinitialize stuffs */
void sec_topthread_destroy_impl(void)
{

	int i = 0;

	topthread_disp_flag = 0;

	/* First Stop the writer */
	for (i = 0; i < THREADINFO_CHAIN_LENGTH; i++) {
		if (g_thread_info_table_chain[i]) {
			KDBG_MEM_DBG_KFREE(g_thread_info_table_chain[i]);
			g_thread_info_table_chain[i] = NULL;
		}
	}

	g_writer = 0;		/* Writer */

	if (g_ptopthread_proc_buff_)
		KDBG_MEM_DBG_KFREE(g_ptopthread_proc_buff_);
	g_ptopthread_proc_buff_ = NULL;

	topthread_reset_proc_index();

	PRINT_KD("TOPTHREAD Destroyed Successfully\n");

	if (g_ptopprocess_proc_buff_)
		KDBG_MEM_DBG_KFREE(g_ptopprocess_proc_buff_);
	g_ptopprocess_proc_buff_ = NULL;

	topprocess_reset_proc_index();

	PRINT_KD("TOPPROCESS Destroyed Successfuly\n");
}

static int get_index(void)
{

#ifdef CONFIG_SMP
	int operation = 0;
	int cpu = 0;
	int index = 0;
	int ret_val = 0;

	do {
		PRINT_KD("\n");
		PRINT_KD("%d.  ALL\n", ++index);
		for (cpu = 0; cpu < NR_CPUS; cpu++)
			PRINT_KD("%d.  CPU [%d]\n", ++index, cpu);

		PRINT_KD("==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		if (operation == 1) {
			ret_val = index;
			break;
		} else if (operation > 0 && operation <= index) {
			ret_val = operation - 1;
			break;
		} else {
			PRINT_KD("Invalid choice\n");
			index = 0;
			continue;
		}

	} while (!ret_val);

	return ret_val;
#else
	PRINT_KD("MSG:System is having only 1 CPU\n");
	return 1;
#endif

}

static void toggle_context(void)
{
	if (sec_get_top_context_copy() == TOP_THREAD_CONTEXT) {
		sec_set_top_context_copy(TOP_PROCESS_CONTEXT);
		PRINT_KD("Task context changed to Process\n");
	} else {
		sec_set_top_context_copy(TOP_THREAD_CONTEXT);
		PRINT_KD("Task context changed to Thread\n");
	}
}

/* Control function will called by kdebugd thread.*/
static int sec_topthread_control(void)
{
	int operation = 0;
	int sub_operation = 0;
	int ret = 1;
	int cpu_index = 0;

	sec_counter_mon_state_t state = sec_topthread_get_state();

#ifdef CONFIG_SMP
	cpu_index = MENU_ITEM;
#else
	cpu_index = 1;
#endif

	/* start background sampling, if stopped state */
	if (state == E_NONE || state == E_DESTROYED) {
		/* Send with sync */
		if (sec_topthread_set_state(E_INITIALIZED, 1) < 0) {	/* Send with sync */
			PRINT_KD
			    ("SEC TOPTHREAD ERROR: Error in Initialization\n");
			return ret;
		}
	}

	do {
		sub_operation = 0;

		PRINT_KD("\n");
		if (sec_get_top_context_copy() == TOP_THREAD_CONTEXT) {
			PRINT_KD("------Turn On/Off top thread print------\n");
			if (cpu_index == MENU_ITEM)
				PRINT_KD("1.  Config: CPU (Current: All), Task (Current: Thread)\n");
			else
				PRINT_KD("1.  Config: CPU (Current: CPU[%d]), Task (Current: Thread)\n", (cpu_index-1));
			PRINT_KD("2.  Show Total CPU Usage for All Threads\n");
			PRINT_KD("3.  Show CPU Usage for Top 5 Threads\n");
			PRINT_KD("4.  Show Total CPU Usage for Top 5 Threads (120 sec)\n");
		} else {
			PRINT_KD("------Turn On/Off top process print------\n");
			if (cpu_index == MENU_ITEM)
				PRINT_KD("1.  Config: CPU (Current: All), Task (Current: Process)\n");
			else
				PRINT_KD("1.  Config: CPU (Current: CPU[%d]), Task (Current: Process)\n", (cpu_index-1));
			PRINT_KD("2.  Show Total CPU Usage for All Processes\n");
			PRINT_KD("3.  Show CPU Usage for Top 5 Processes\n");
			PRINT_KD("4.  Show Total CPU Usage for Top 5 Processes (120 sec)\n");

		}

		PRINT_KD("------------------------------------------\n");
		PRINT_KD("99. For Exit\n");
		PRINT_KD("------------------------------------------\n");
		PRINT_KD("==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		if (operation == 1) {
			do {
				PRINT_KD("\n");
				if (cpu_index == MENU_ITEM)
					PRINT_KD("1.  Config: CPU (Current: All)\n");
				else
					PRINT_KD("1.  Config: CPU (Current: CPU[%d])\n", (cpu_index-1));

				if (sec_get_top_context_copy() == TOP_THREAD_CONTEXT)
					PRINT_KD("2.  Toggle: Task (Current: Thread)\n");
				else
					PRINT_KD("2.  Toggle: Task (Current: Process)\n");

				PRINT_KD("99. For Exit\n");
				PRINT_KD("==> ");
				sub_operation = debugd_get_event_as_numeric(NULL, NULL);
				PRINT_KD("\n");

				if ((sub_operation != 1) && (sub_operation != 2) && (sub_operation != 99)) {
					PRINT_KD("Invalid choice\n");
					continue;
				} else
					break;

			} while (1);

			switch (sub_operation) {
			case 1:
				cpu_index = get_index();
				continue;

			case 2:
				toggle_context();
				continue;
			}
		}

		if (sub_operation == 99)
			continue;

		if (operation != 99 && (operation <= 0 || operation > 4)) {
			PRINT_KD("Invalid Input ...\n");
			continue;
		}
		break;

	} while (1);

	switch (operation) {
	case 2:

		state = sec_topthread_get_state();

		if (state == E_INITIALIZED) {
			sec_topthread_set_state(E_RUNNING,
						1) /* Send with sync */ ;
			sec_topthread_set_state(E_RUN_N_PRINT,
						1) /* Send with sync */ ;

		} else if (state == E_RUNNING) {
			sec_topthread_set_state(E_RUN_N_PRINT,
						1) /* Send with sync */ ;
		}

		if (!cfs_print_option) {
			g_header_show = 1;
			cfs_print_option = cpu_index;
		} else
			turnoff_cpu_usage();
		ret = 0;
		break;
	case 3:
		state = sec_topthread_get_state();
		if (state == E_INITIALIZED)
			sec_topthread_set_state(E_RUNNING, 1);	/* Send with sync */
		sec_topthread_show_cpu_usage(cpu_index);
		ret = 0;
		break;
	case 4:
		sec_topthread_dump_cpu_usage(cpu_index);
		break;
	case 99:
		ret = 1;
		break;
	}

	return ret;
}

void sec_topthread_on_off(void)
{
	sec_counter_mon_state_t state = sec_topthread_get_state();
	/* Destroying state is intermediate state of destroy
	 * and it should not be stale, avoid this*/
	BUG_ON(state == E_DESTROYING);

	if (state == E_NONE || state == E_DESTROYED) {

		if (sec_topthread_set_state(E_INITIALIZED, 1) < 0) {	/* Send with sync */

			PRINT_KD("Initialization Failed\n");
			return;
		}
		/* background sampling ON */
		sec_topthread_set_state(E_RUNNING, 1);	/* Send with sync */
	} else {

		WARN_ON(state < E_NONE || state > E_DESTROYED);

		/* Before Destroying make sure state is E_INITIALIZED */
		while (state != E_INITIALIZED)
			sec_topthread_set_state(--state, 1);	/* Send with sync */

		/* System is in E_INITIALIZED State now destroyed it
		 * and free all the resources*/
		sec_topthread_set_state(E_DESTROYED, 1);	/* Send with sync */
	}
}

#if defined(CONFIG_SEC_TOPTHREAD_AUTO_START) &&  defined(CONFIG_COUNTER_MON_AUTO_START_PERIOD)

static struct hrtimer topthread_auto_timer;
static struct timespec tv_autostart = {.tv_sec = CONFIG_COUNTER_MON_START_SEC, .tv_nsec =
	    0};
static void sec_topthread_hrtimer_start(void)
{
	int state_ret = 0;
	/* Make the status running */
	sec_counter_mon_state_t state = sec_topthread_get_state();

	/* Check whether its in Initialized state */
	if (state == E_NONE || state == E_DESTROYED) {
		/* If not Initialize it First , send no sync */
		state_ret = sec_topthread_set_state_impl(E_INITIALIZED);
		if (state_ret < 0) {	/* Error in Initialization */
			PRINT_KD
			    ("SEC TOPTHREAD ERROR: Error In Initialization\n");
			return;
		}
	}
	if (sec_topthread_get_state() != E_RUNNING &&
	    sec_topthread_get_state() != E_RUN_N_PRINT) {
		state_ret = sec_topthread_set_state_impl(E_RUNNING);
	}
}

static void sec_topthread_hrtimer_stop(void)
{
	int state_ret = 0;
	/* Make the status running */
	sec_counter_mon_state_t state = sec_topthread_get_state();
	if (state == E_RUNNING) {
		state_ret = sec_topthread_set_state(E_INITIALIZED, 0);
		WARN_ON(state_ret);
	}
}

static enum hrtimer_restart topthread_auto_start(struct hrtimer *ht)
{
	static int started;
	enum hrtimer_restart ret = HRTIMER_NORESTART;
	sec_counter_mon_state_t state = sec_topthread_get_state();
	struct kdbg_work_t sec_topthread_start_work;

	BUG_ON(started != 0 && started != 1);

	/* If state is destroying dont take a risk,
	 * return immediatly*/
	if (state == E_DESTROYING) {
		PRINT_KD
		    ("INFO --> State is destroying cpusauge cannot be started\n");
		return ret;
	}

	if (!started) {

		started = 1;

		sec_topthread_start_work.data = (void *)NULL;
		sec_topthread_start_work.pwork =
		    (void *)sec_topthread_hrtimer_start;
		kdbg_workq_add_event(sec_topthread_start_work, NULL);

		/* Restart if after finshed seconds. */
		tv_autostart.tv_sec = CONFIG_COUNTER_MON_FINISHED_SEC;
		tv_autostart.tv_nsec = 0;

		hrtimer_forward(&topthread_auto_timer, ktime_get(),
				timespec_to_ktime(tv_autostart));

		ret = HRTIMER_RESTART;

	} else {		/* Finished second expired stop it */
		BUG_ON(ret != HRTIMER_NORESTART);
		/* If Display flag is off stop collecting data */
		started = 0;

		sec_topthread_start_work.data = (void *)NULL;
		sec_topthread_start_work.pwork =
		    (void *)sec_topthread_hrtimer_stop;
		kdbg_workq_add_event(sec_topthread_start_work, NULL);

		ret = HRTIMER_NORESTART;
	}
	return ret;
}
#endif

/* ------------------ CFS SCHEDULER START ---------------*/

/* CFS scheduler prints  will show all the
 * running in timer tick
 * the Menu for SMP processor contains 2 SubMenu
 * 1- CPU 0 statistics
 * 2- CPU 1 statistics
 * 3- Consolidated
 * */

#define TOPTHREAD_MAX_PRINT  1024

#define FULL_ROW_LEN 80
#define HALF_ROW_LEN 40



#ifdef CONFIG_SMP
static int get_max_index(struct topthread_item_per_cpu item[], int start_idx, int arr_count)
{
	int cpu = start_idx;
	int max_index = item[start_idx].max_thread;

	for (cpu = start_idx; cpu < arr_count; cpu++) {
		if (max_index < item[cpu].max_thread)
			max_index = item[cpu].max_thread;
	}

	return max_index;
}
#endif

/* To solve printk issue, snprintf is removed
 * and is replaced by PRINT_KD */
#define WITHOUT_BUFF_APPEND

#ifdef WITHOUT_BUFF_APPEND
#define BUFF_APPEND	PRINT_KD
#else
#define BUFF_APPEND(x, ...)   do { int pos = TOPTHREAD_MAX_PRINT - ret;\
									if (pos > 0) {\
										ret += snprintf(print_buff + ret, pos, x, ##__VA_ARGS__);\
										print_buff[ret] = 0;\
				} \
							if (row_len > (pos)) {\
									print_buff[ret] = 0;\
									printk("%s", print_buff);\
									ret = 0;\
							} \
				} while (0)
#endif

static void csf_show_entry(int index)
{
	struct task_struct *tsk = NULL;
	int cpu = 0;
	int print_option = cfs_print_option - 1;

#ifndef WITHOUT_BUFF_APPEND
	int ret = 0;
#endif
	int row_len = 0;
	char out[TASK_COMM_LEN] = {0,};
	int cpu_prcn = 0;

	static char print_buff[TOPTHREAD_MAX_PRINT];
	struct thread_info_result_table *ptop_proc_buff_copy;
	static sec_counter_mon_top_context prev_ctx;

	if (sec_get_top_context() != prev_ctx) {
		prev_ctx = sec_get_top_context();
		return;
	}

	if (sec_get_top_context() == TOP_THREAD_CONTEXT)
		ptop_proc_buff_copy = g_ptopthread_proc_buff_;

	else
		ptop_proc_buff_copy = g_ptopprocess_proc_buff_;


#ifdef CONFIG_SMP

	if (print_option == NR_CPUS) {	/* Print Usage of all CPU's */
		int max_loop;
		int total_cpu_idx = 0;
		unsigned long total_ctx = 0;
		int i = 0, j = 0;

		for_each_online_cpu(cpu) {
			total_cpu_idx +=
				ptop_proc_buff_copy[index].item_per_cpu[cpu].
				max_thread;
			cpu_prcn +=
				JIFFI_TO_PERCENT(ptop_proc_buff_copy[index].
						total[cpu]);
			total_ctx +=
				ptop_proc_buff_copy[index].
				sec_topthread_ctx_cnt[cpu];
		}

		row_len = FULL_ROW_LEN;

		if (cpu_prcn > (TOTAL_PERCENTAGE))
			cpu_prcn = TOTAL_PERCENTAGE;

		BUFF_APPEND("\n");
		BUFF_APPEND
			("+---------------------------------------------------------------------+");
		BUFF_APPEND("\n");
		BUFF_APPEND
			("|[%5ld sec] CPU USAGE:%3d%%(MAXIMUM %3d%%) # task:%3d. # ctx:%7lu.  |",
			 ptop_proc_buff_copy[index].sec, cpu_prcn, TOTAL_PERCENTAGE, total_cpu_idx, total_ctx);

		BUFF_APPEND("\n");
		BUFF_APPEND
			("+---------------------------------------------------------------------+\n");

		/* Don't print for CPUs in pairs,
		* instead print for all CPUs */
		for (j = 0; j < NR_CPUS; j += 2) {
			max_loop =
				get_max_index(ptop_proc_buff_copy[index].item_per_cpu, j, (j + 2));

			/* If there are no threads running on any CPU,
			 * print minimum one blank line. */
			if (!max_loop)
				max_loop = 1;

			if (g_header_show) {
				BUFF_APPEND ("%s", CFS_HEADER);
				g_header_show = 0;
			}

			for (cpu = j; (cpu < j+2 && cpu < NR_CPUS); cpu++) {
				cpu_prcn = JIFFI_TO_PERCENT(ptop_proc_buff_copy[index].total[cpu]);
				if (cpu_prcn > (TOPTHREAD_PERCENTAGE))
					cpu_prcn = TOPTHREAD_PERCENTAGE;

				BUFF_APPEND ("+-CPU%d TOTAL (MAXIMUM 100%%)----%3d%% ", cpu,
						cpu_prcn);
			}
			BUFF_APPEND("\n");

			for (i = 0; i < max_loop; i++) {
				/*Take RCU read lock register can be changed */
				for (cpu = j; (cpu < j+2 && cpu < NR_CPUS); cpu++) {
					if (ptop_proc_buff_copy[index].
							item_per_cpu[cpu].max_thread > i) {

						rcu_read_lock();
						/* get the data from rearranged array of thread cpu usage */
						tsk =
							find_task_by_pid_ns
							(ptop_proc_buff_copy
							 [index].item_per_cpu[cpu].
							 cpu_table[i].pid, &init_pid_ns);
						if (tsk)
							get_task_struct(tsk);

						/*Unlock */
						rcu_read_unlock();

						cpu_prcn = JIFFI_TO_PERCENT (ptop_proc_buff_copy
									 [index].item_per_cpu[cpu].
									 cpu_table[i].tick_cnt);
						if (cpu_prcn > (TOPTHREAD_PERCENTAGE))
							cpu_prcn = TOPTHREAD_PERCENTAGE;

						if (tsk) {

							BUFF_APPEND
								("| %-16s- %4d P[%3d] %2d%% ",
								convert_taskname(tsk->comm, out),
								 ptop_proc_buff_copy
								 [index].item_per_cpu[cpu].
								 cpu_table[i].pid,
								 tsk->prio,
								 cpu_prcn);
							put_task_struct(tsk);
						} else {

							BUFF_APPEND
								("| %-16s- %4d P[%3d] %2d%% ",
								 "<th_dead>",
								 ptop_proc_buff_copy
								 [index].item_per_cpu[cpu].
								 cpu_table[i].pid, 0,
								 cpu_prcn);
						}
					} else {
						BUFF_APPEND
							("|                                   ");
					}

				}

				BUFF_APPEND("\n");
			}
		}

		PRINT_KD("%s", print_buff);
	} else			/* Show for each CPU */
#endif /*CONFIG_SMP */
	{
		int i = 0;
		row_len = HALF_ROW_LEN;
		cpu = print_option;
		WARN_ON(cpu < 0 || cpu >= NR_CPUS);

		if (cpu < 0 || cpu >= NR_CPUS) {
			PRINT_KD("Incorrect CPU %d\n", cpu);
			return;
		}

		cpu_prcn = JIFFI_TO_PERCENT (ptop_proc_buff_copy[index].total[cpu]);
		if (cpu_prcn  > TOPTHREAD_PERCENTAGE)
			cpu_prcn = TOPTHREAD_PERCENTAGE;

		PRINT_KD("\n+--------------------------------------+");
		PRINT_KD("\n|[ %5ld sec ] CPU[%1d] USAGE: %3d%%      |",
				ptop_proc_buff_copy[index].sec, cpu,
				cpu_prcn);
		PRINT_KD("\n| # of task:%4d.  # of ctx:%7lu.#  |",
				ptop_proc_buff_copy[index].item_per_cpu[cpu].
				max_thread,
				ptop_proc_buff_copy[index].
				sec_topthread_ctx_cnt[cpu]);

		BUFF_APPEND("\n+--------------------------------------+\n");

		if (g_header_show) {
			BUFF_APPEND("%s", CFS_HEADER);
			BUFF_APPEND
				("+--------------------------------------+\n");
			g_header_show = 0;
		}

		for (i = 0;
				i <
				ptop_proc_buff_copy[index].item_per_cpu[cpu].
				max_thread; i++) {
			/*Take RCU read lock register can be changed */
			rcu_read_lock();
			/* get the data from rearranged array of thread cpu usage */
			tsk =
				find_task_by_pid_ns(ptop_proc_buff_copy
						[index].item_per_cpu[cpu].
						cpu_table[i].pid, &init_pid_ns);
			/*Unlock */
			if (tsk)
				get_task_struct(tsk);

			rcu_read_unlock();

			cpu_prcn = JIFFI_TO_PERCENT (ptop_proc_buff_copy[index].
						 item_per_cpu[cpu].cpu_table[i].tick_cnt);
			if (cpu_prcn  > TOPTHREAD_PERCENTAGE)
				cpu_prcn = TOPTHREAD_PERCENTAGE;

			if (tsk) {

				BUFF_APPEND("| %-16s- %4d C[%d] P[%3d] %2d%% ",
						convert_taskname(tsk->comm, out),
						ptop_proc_buff_copy[index].
						item_per_cpu[cpu].cpu_table[i].pid,
						cpu, tsk->prio,
						cpu_prcn);
				put_task_struct(tsk);
			} else {

				BUFF_APPEND
					("| %-16s- %4d  C[%d] P[%3d] %2d%%     ",
					 "<th_dead>",
					 ptop_proc_buff_copy[index].
					 item_per_cpu[cpu].cpu_table[i].pid, cpu, 0,
					 cpu_prcn);
			}

			BUFF_APPEND("\n");
		}
		PRINT_KD("%s", print_buff);
	}
}

void turnoff_cpu_usage(void)
{
	if (cfs_print_option != 0) {
		PRINT_KD(KERN_EMERG "\nCPU Usage Check Off\n");
		cfs_print_option = 0;
		if (!topthread_disp_flag)
			sec_topthread_set_state(E_RUNNING, 1);	/* Send with sync */
	}
}

/* ------------------ CFS SCHEDULER END ---------------*/

/*
 * topthread proc init function
 */
int kdbg_topthread_init(void)
{
	if (sec_topthread_create()) {
		WARN_ON("Error in Initialization");
		return 0;
	}
#ifdef CONFIG_SEC_TOPTHREAD_AUTO_START

#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	hrtimer_init(&topthread_auto_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	topthread_auto_timer._softexpires = timespec_to_ktime(tv_autostart);
	topthread_auto_timer.function = topthread_auto_start;
	hrtimer_start(&topthread_auto_timer, topthread_auto_timer._softexpires,
			HRTIMER_MODE_REL);
#else
	if (sec_topthread_set_state(E_INITIALIZED, 1) < 0) {	/* Send with sync */

		PRINT_KD("SEC TOPTHREAD ERROR: Error in Initialization\n");
	} else {
		sec_topthread_set_state(E_RUNNING, 1) /* Send with sync */ ;
	}
#endif

#endif
	kdbg_register("COUNTER MONITOR: CPU Usage for each task",
			sec_topthread_control, turnoff_topthread,
			KDBG_MENU_COUNTER_MONITOR_EACH_THREAD);

	return 0;
}
