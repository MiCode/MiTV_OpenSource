/*
 *  linux/kernel/sec_memusage.c
 *
 *  Memory Performance Profiling Solution, memory usage releated functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-06-05  Created by Choi Young-Ho
 *
 */
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/swap.h>
#include <linux/mmzone.h>
#include <linux/vmstat.h>
#include <linux/nmi.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mmu_context.h>

#include <kdebugd.h>
#include <sec_memusage.h>
#include "kdbg_util.h"

#define SYM_DEBUG_ON  0
#include "kdbg_elf_sym_debug.h"

#include <linux/version.h>
/*
 * Turn ON the mem usage processing.
 */
static int sec_memusage_init(void);

/*
 * The buffer that will store mem usage data.
 */
static struct sec_memusage_struct *g_psec_memusage_buffer_;

/*Init flag */
static int sec_memusage_init_flag;

/* check whether state is running or not..*/
static int sec_memusage_run_state;

/*The index of the buffer array at which the data will be written.*/
static int sec_memusage_index;

/*The virt mem index of the buffer array at which the data will be written.*/
static int sec_virt_memusage_index;

/*The flag which incates whether the buffer array is full(value is 1)
 *or is partially full(value is 0).
 */
static int sec_memusage_is_buffer_full;

/*The flag which will be turned on or off when sysrq feature will
 *turn on or off respectively.
 */
/* Flag is used to turn the sysrq for mem usage on or off */
static int sec_memusage_status;

/* Flag is used to turn the sysrq for virtual mem usage on or off */
static int sec_virt_memusage_status;

/* Flag is used to turn the sysrq for Physical mem usage on or off */
static int sec_phy_memusage_status;

/* Timer for polling */
struct hrtimer memory_usage_timer;

/* Interval for timer */
struct timespec show_memory_interval = {.tv_sec = 1, .tv_nsec = 0};

/* task struct of virtual mem usage */
static pid_t trace_virt_mem_tsk_pid;

#define P2K(x) ((x) << (PAGE_SHIFT - 10))

/* This function is to show header */
static void memusage_show_header(void)
{
	PRINT_KD
	    ("time       total      used       free       buffers    cached    anonpage\n");
	PRINT_KD
	    ("======== ========== ========== ========== ========== ========== ==========\n");
}

/* This function is to show header */
static void phy_memusage_show_header(void)
{
	PRINT_KD
	    ("time    [ pid ]   name     ProcessCur    Code       Data     LibCode     LibData     Stack     Other\n");
	PRINT_KD
	    ("==== ========== ========== ========== ========== ========== ========== ========== ========== ==========\n");
}

/* This function is to show header */
static void virt_memusage_show_header(void)
{
	PRINT_KD("time       VMA Count   VMA Size(kB)\n");
	PRINT_KD("========== ========== ==========\n");
}

/*Dump the bufferd data of mem usage from the buffer.
  This Function is called from the kdebug menu. It prints the data
  same as printed by cat /proc/memusage-gnuplot*/
void sec_memusage_gnuplot_dump(void)
{
	int last_row = 0, saved_row = 0;
	int idx, i;
	int limit_count;

	if (sec_memusage_init_flag) {

		BUG_ON(!g_psec_memusage_buffer_);

		if (sec_memusage_is_buffer_full) {
			limit_count = SEC_MEMUSAGE_BUFFER_ENTRIES;
			last_row = sec_memusage_index;
		} else {
			limit_count = sec_memusage_index;
			last_row = 0;
		}
		saved_row = last_row;

		PRINT_KD("\n\n\n");
		PRINT_KD("{{{#!gnuplot\n");
		PRINT_KD("reset\n");
		PRINT_KD("set title \"Memory Usage\"\n");
		PRINT_KD("set xlabel \"time(sec)\"\n");
		PRINT_KD("set ylabel \"Usage(kB)\"\n");
		PRINT_KD("set yrange [0:%d]\n",
			 g_psec_memusage_buffer_[0].totalram);
		PRINT_KD("set key invert reverse Left outside\n");
		PRINT_KD("set key autotitle columnheader\n");
		PRINT_KD("set auto x\n");
		PRINT_KD("set xtics nomirror rotate by 90\n");
		PRINT_KD("set style data histogram\n");
		PRINT_KD("set style histogram rowstacked\n");
		PRINT_KD("set style fill solid border -1\n");
		PRINT_KD("set grid ytics\n");
		PRINT_KD("set boxwidth 0.7\n");
		PRINT_KD("set lmargin 11\n");
		PRINT_KD("set rmargin 1\n");
		PRINT_KD("#\n");
		PRINT_KD
		    ("plot \"-\" using 2:xtic(1), '' using 3, '' using 4\n");

		/* Becuase of gnuplot grammar, we should print data twice
		 * to draw the two graphic lines on the chart.
		 */
		for (i = 0; i < 3; i++) {
			PRINT_KD
			    ("time    used    buffers  cached    anonpage\n");
			for (idx = 0; idx < limit_count; idx++) {
				int index =
				    last_row % SEC_MEMUSAGE_BUFFER_ENTRIES;
				last_row++;

				PRINT_KD("%04ld  %7lu %7u %7ld %7u\n",
					 g_psec_memusage_buffer_[index].sec,
					 g_psec_memusage_buffer_[index].totalram
					 -
					 g_psec_memusage_buffer_[index].
					 freeram -
					 g_psec_memusage_buffer_[index].
					 bufferram -
					 g_psec_memusage_buffer_[index].cached,
					 g_psec_memusage_buffer_[index].
					 bufferram,
					 g_psec_memusage_buffer_[index].cached,
					 g_psec_memusage_buffer_[index].
					 anonpages_info);
			}
			PRINT_KD("e\n");
			last_row = saved_row;
		}

		PRINT_KD("}}}\n");
	}
}

/*Dump the bufferd data of mem usage from the buffer.
 *This Function is called from the kdebug menu.
 */
void sec_memusage_dump(void)
{
	int i = 0;
	int buffer_count = 0;
	int idx = 0;

	if (sec_memusage_init_flag) {

		BUG_ON(!g_psec_memusage_buffer_);

		if (sec_memusage_is_buffer_full) {
			buffer_count = SEC_MEMUSAGE_BUFFER_ENTRIES;
			idx = sec_memusage_index;
		} else {
			buffer_count = sec_memusage_index;
			idx = 0;
		}

		PRINT_KD("\n\n");
		memusage_show_header();

		for (i = 0; i < buffer_count; ++i, ++idx) {
			idx = idx % SEC_MEMUSAGE_BUFFER_ENTRIES;

			PRINT_KD
			    ("%04ld sec %7u kB %7u kB %7u kB %7u kB %7ld kB %7ukB\n",
			     g_psec_memusage_buffer_[idx].sec,
			     g_psec_memusage_buffer_[idx].totalram,
			     g_psec_memusage_buffer_[idx].totalram -
			     g_psec_memusage_buffer_[idx].freeram,
			     g_psec_memusage_buffer_[idx].freeram,
			     g_psec_memusage_buffer_[idx].bufferram,
			     g_psec_memusage_buffer_[idx].cached,
			     g_psec_memusage_buffer_[idx].anonpages_info);
		}
	}
}

static void kdbg_show_mem_stat(void)
{
	struct sysinfo i;
	long cached;
	unsigned int anon;

	if (!sec_memusage_run_state)
		return;

	if (sec_memusage_init_flag) {

		BUG_ON(!g_psec_memusage_buffer_);

		si_meminfo(&i);
		si_swapinfo(&i);
		cached = global_page_state(NR_FILE_PAGES) -
		    total_swapcache_pages() - i.bufferram;

		if (cached < 0)
			cached = 0;
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,49)
		anon = P2K(global_page_state(NR_ANON_MAPPED));
	#else
		anon = P2K(global_page_state(NR_ANON_PAGES));		
	#endif
		g_psec_memusage_buffer_[sec_memusage_index].sec =
		    kdbg_get_uptime();
		g_psec_memusage_buffer_[sec_memusage_index].totalram =
		    P2K(i.totalram);
		g_psec_memusage_buffer_[sec_memusage_index].freeram =
		    P2K(i.freeram);
		g_psec_memusage_buffer_[sec_memusage_index].bufferram =
		    P2K(i.bufferram);
		g_psec_memusage_buffer_[sec_memusage_index].cached =
		    P2K(cached);
		g_psec_memusage_buffer_[sec_memusage_index].anonpages_info =
		    anon;

		if (sec_memusage_status) {
			PRINT_KD
			    ("%04ld sec %7lu kB %7lu kB %7lu kB %7lu kB %7lu kB %7u kB\n",
			     kdbg_get_uptime(), P2K(i.totalram),
			     P2K(i.totalram) - P2K(i.freeram), P2K(i.freeram),
			     P2K(i.bufferram), P2K(cached), anon);

			if ((sec_memusage_index % 20) == 0)
				memusage_show_header();
		}

		sec_memusage_index++;

		if (sec_memusage_index >= SEC_MEMUSAGE_BUFFER_ENTRIES) {
			sec_memusage_is_buffer_full = 1;
			sec_memusage_index = 0;
		}
	}
}

/* show virtual memory stats */
static void kdbg_show_virt_mem_stat(void)
{
	int vma_count = 0;
	unsigned long vma_size = 0;
	struct task_struct *trace_tsk = 0;
	struct mm_struct *mm;

	if (!sec_virt_memusage_status)
		return;

	if (trace_virt_mem_tsk_pid) {
		/*Take RCU read lock register can be changed */
		rcu_read_lock();
		/* Refer to kernel/pid.c
		 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
		 *  PID-map pages start out as NULL, they get allocated upon
		 *  first use and are never deallocated. This way a low pid_max
		 *  value does not cause lots of bitmaps to be allocated, but
		 *  the scheme scales to up to 4 million PIDs, runtime.
		 */
		trace_tsk =
		    find_task_by_pid_ns(trace_virt_mem_tsk_pid, &init_pid_ns);

		if (trace_tsk)
			get_task_struct(trace_tsk);
		/* Only reference count increment is sufficient */
		rcu_read_unlock();

		if (!trace_tsk) {
			PRINT_KD("\n");
			PRINT_KD("[SP_DEBUG] no task with given pid...\n");
			return;
		}

		mm = get_task_mm(trace_tsk);
		if (!mm) {
			put_task_struct(trace_tsk);
			sym_errk("PID %d mm does not exist\n", trace_virt_mem_tsk_pid);
			return;
		}

		/* cat /proc/<pid>/status */
		vma_count = mm->map_count;
		vma_size =
		    P2K(mm->total_vm);

		mmput(mm);

		/* Decrement usage count */
		put_task_struct(trace_tsk);

		PRINT_KD(" %04ld sec %7d     %7lu\n", kdbg_get_uptime(),
			 vma_count, vma_size);

		if ((++sec_virt_memusage_index % 20) == 0)
			virt_memusage_show_header();
	}
}

#ifdef CONFIG_RSS_INFO
/* get RSS info from fs/proc
 * already incremented mm count before calling this function
 */
static void sec_pid_rss_read(struct mm_struct *mm)
{
	int i;
	unsigned long cur_cnt[VMAG_CNT], max_cnt[VMAG_CNT];
	unsigned long tot_cur_cnt = 0;

	if (!mm) {
		sym_errk("task mm not exist\n");
		return;
	}

	down_read(&mm->mmap_sem);

	for (i = 0; i < VMAG_CNT; i++) {
		get_rss_cnt(mm, i, &cur_cnt[i], &max_cnt[i]);
		tot_cur_cnt += cur_cnt[i];
	}

	up_read(&mm->mmap_sem);

	PRINT_KD("  %7lu K  %7lu K  %7lu K  %7lu K  %7lu K  %7lu K  %7lu K\n",
		 P2K(tot_cur_cnt),
		 P2K(cur_cnt[0]), P2K(cur_cnt[1]),
		 P2K(cur_cnt[2]), P2K(cur_cnt[3]),
		 P2K(cur_cnt[4]), P2K(cur_cnt[5]));

}
#endif

/* Find task struct and increments its usage */
static struct task_struct *sec_get_task_struct(pid_t pid)
{
	struct task_struct *task;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task)
		return ERR_PTR(-ESRCH);
	return task;
}

static void sec_show_phy_memusage_info(pid_t pid)
{
	struct task_struct *p;
	struct mm_struct *mm;
#ifdef CONFIG_RSS_INFO
	struct vm_area_struct *vma = NULL;
#endif

	p = sec_get_task_struct(pid);
	if (IS_ERR(p)) {
		PRINT_KD("[ERROR] No Thread\n");
		return;
	}
	mm = get_task_mm(p);
	if (!mm) {
		put_task_struct(p);
		return;
	}
	PRINT_KD("%04ld  [%7d] %-10.10s", kdbg_get_uptime(), task_pid_nr(p), p->comm);

#ifdef CONFIG_RSS_INFO
	vma = p->mm->mmap;
	if (vma)
		sec_pid_rss_read(mm);
#else
	PRINT_KD("Please define  CONFIG_RSS_INFO\n");
#endif

	mmput(mm);
	put_task_struct(p);
}

/* show physical memory stats */
static int kdbg_show_phy_mem_stat(void)
{
	const int MAX_EXTRA_PROCESSES = 50;	/* allow for variation of 50 threads */
	pid_t *pid_arr = NULL;
	struct task_struct *p;
	const int MAX_PROCESSES = nr_processes() + MAX_EXTRA_PROCESSES;
	int i, count = 0;

	if (!sec_phy_memusage_status)
		return -1;

	phy_memusage_show_header();

	pid_arr = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				       MAX_PROCESSES * sizeof(pid_t),
				       GFP_KERNEL);
	if (!pid_arr) {
		sym_errk("[ERROR] Insufficient Memory!!!!\n");
		return -1;
	}

	read_lock(&tasklist_lock);
	for_each_process(p) {
		if (p && p->mm) {
			pid_arr[count++] = p->pid;
			if (count == MAX_PROCESSES) {
				PRINT_KD
				    ("User threads more than %d, increase MAX_PROCESSES\n",
				     MAX_PROCESSES);
				break;
			}
		}
	}
	read_unlock(&tasklist_lock);

	for (i = 0; i < count; i++)
		sec_show_phy_memusage_info(pid_arr[i]);

	PRINT_KD("\n");

	KDBG_MEM_DBG_KFREE(pid_arr);
	pid_arr = NULL;

	return 0;
}

enum hrtimer_restart mem_show_func(struct hrtimer *ht)
{
	ktime_t now;
	if (sec_memusage_run_state)
		kdbg_show_mem_stat();
	if (sec_virt_memusage_status)
		kdbg_show_virt_mem_stat();

	now = ktime_get();

	hrtimer_forward(&memory_usage_timer, now,
			timespec_to_ktime(show_memory_interval));
	return HRTIMER_RESTART;
}

static void start_memory_usage(void)
{
	hrtimer_init(&memory_usage_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	memory_usage_timer._softexpires = timespec_to_ktime(show_memory_interval);
	memory_usage_timer.function = mem_show_func;
	hrtimer_start(&memory_usage_timer, memory_usage_timer._softexpires,
		      HRTIMER_MODE_REL);
}

int sec_memusage_init()
{
	static int func_register;

	/* Turn ON the processing of dumping the mem usage data. */
	g_psec_memusage_buffer_ =
	    (struct sec_memusage_struct *)KDBG_MEM_DBG_KMALLOC
	    (KDBG_MEM_KDEBUGD_MODULE,
	     SEC_MEMUSAGE_BUFFER_ENTRIES * sizeof(struct sec_memusage_struct),
	     GFP_ATOMIC);

	if (!g_psec_memusage_buffer_) {
		PRINT_KD("MEMUSAGE USAGE ERROR: Insuffisient memory\n");
		sec_memusage_init_flag = 0;
		return false;
	}

	memset(g_psec_memusage_buffer_, 0, SEC_MEMUSAGE_BUFFER_ENTRIES *
	       sizeof(struct sec_memusage_struct));
	sec_memusage_index = 0;
	sec_virt_memusage_index = 0;
	sec_memusage_is_buffer_full = 0;
	sec_memusage_init_flag = 1;
	start_memory_usage();

	if (!func_register) {
		register_counter_monitor_func(kdbg_show_phy_mem_stat);
		func_register = 1;
	}

	return true;
}

void get_memusage_status(void)
{
	if (sec_memusage_init_flag) {

		PRINT_KD("Initialized        ");
		if (sec_memusage_run_state)
			PRINT_KD("Running\n");
		else
			PRINT_KD("Not Running\n");
	} else {
		PRINT_KD("Not Initialized    Not Running\n");
	}
}

void sec_memusage_destroy(void)
{

	sec_memusage_status = 0;
	sec_memusage_run_state = 0;
	if (g_psec_memusage_buffer_) {
		hrtimer_cancel(&memory_usage_timer);
		KDBG_MEM_DBG_KFREE(g_psec_memusage_buffer_);
		g_psec_memusage_buffer_ = NULL;
		sec_memusage_init_flag = 0;

		PRINT_KD("MemUsage Destroyed Successfuly\n");
	} else {
		PRINT_KD("Already Not Initialized\n");
	}

}

/*
 *Turn off the prints of memusage
 */
void turnoff_memusage(void)
{
	if (sec_memusage_status) {
		sec_memusage_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Memory USAGE Dump OFF\n");
	}

	if (sec_virt_memusage_status) {
		sec_virt_memusage_status = 0;
		trace_virt_mem_tsk_pid = 0;
		PRINT_KD("\n");
		PRINT_KD("Virtual Memory USAGE Dump OFF\n");
	}

	if (sec_phy_memusage_status) {
		sec_phy_memusage_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Physical Memory USAGE Dump OFF\n");
	}
}

/*
 *Turn the prints of memusage on
 *or off depending on the previous status.
 */
void sec_memusage_prints_OnOff(void)
{
	sec_memusage_status = (sec_memusage_status) ? 0 : 1;

	if (sec_memusage_status) {
		PRINT_KD("Memory USAGE Dump ON\n");

		memusage_show_header();
	} else {
		PRINT_KD("Memory USAGE Dump OFF\n");
	}
}

/*
 *Turn the prints of virtual memusage on
 *or off depending on the previous status.
 */
void sec_virt_memusage_prints_OnOff(void)
{
	sec_virt_memusage_status = (sec_virt_memusage_status) ? 0 : 1;

	if (sec_virt_memusage_status) {
		PRINT_KD("Virtual Memory USAGE Dump ON\n");
		virt_memusage_show_header();
	} else {
		trace_virt_mem_tsk_pid = 0;
		PRINT_KD("Virtual Memory USAGE Dump OFF\n");
	}
}

/*
 *Turn the prints of Physical memusage on
 *or off depending on the previous status.
 */
void sec_phy_memusage_prints_OnOff(void)
{
	sec_phy_memusage_status = (sec_phy_memusage_status) ? 0 : 1;

	if (sec_phy_memusage_status)
		PRINT_KD("Physical Memory USAGE Dump ON\n");
	else
		PRINT_KD("Physical Memory USAGE Dump OFF\n");
}

/*
 *Turn the backgroung running of memusage on
 *or off depending on the previous status.
 */
void sec_memusage_background_OnOff(void)
{
	sec_memusage_run_state = (sec_memusage_run_state) ? 0 : 1;

	if (sec_memusage_run_state)
		PRINT_KD("Memory USAGE Backgound Run ON\n");
	else
		PRINT_KD("Memory USAGE Backgound Run OFF\n");
}

static int sec_memusage_control(void)
{
	int operation = 0;
	int ret = 1;
	struct task_struct *trace_tsk = NULL;

	if (!sec_memusage_init_flag)
		sec_memusage_init();

	PRINT_KD("\n");
	PRINT_KD("Select Operation....\n");
	PRINT_KD("1. Turn On/Off the Memory Usage prints\n");
	PRINT_KD("2. Dump Memory Usage history(%d sec)\n",
		 SEC_MEMUSAGE_BUFFER_ENTRIES);
	PRINT_KD("3. Dump Memory Usage gnuplot history(%d sec)\n",
		 SEC_MEMUSAGE_BUFFER_ENTRIES);
	PRINT_KD("4. Virtual Mem Usage Info\n");
	PRINT_KD("5. Physical Mem Usage Info\n");
	PRINT_KD("==>  ");

	operation = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n\n");

	switch (operation) {
	case 1:
		sec_memusage_run_state = 1;
		sec_memusage_prints_OnOff();
		ret = 0;	/* don't print the menu */
		break;
	case 2:
		sec_memusage_dump();
		break;
	case 3:
		sec_memusage_gnuplot_dump();
		break;
	case 4:
		sec_virt_memusage_index = 0;
		trace_tsk = get_task_with_pid();

		if (trace_tsk == NULL || !trace_tsk->mm) {
			PRINT_KD("[ALERT] %s Thread\n",
				 trace_tsk == NULL ? "No" : "Kernel");
			return ret;
		}
		trace_virt_mem_tsk_pid = trace_tsk->pid;
		sec_virt_memusage_prints_OnOff();
		ret = 0;
		break;
	case 5:
		sec_phy_memusage_prints_OnOff();
		ret = 0;
		break;
	default:
		break;
	}
	return ret;
}

void sec_memusage_OnOff(void)
{
	if (sec_memusage_init_flag) {
		sec_memusage_destroy();
	} else {
		/*BUG: if initialization failed */
		sec_memusage_init();
		/* start the mem usage after init */
		sec_memusage_run_state = 1;
	}
}

#if	defined(CONFIG_SEC_MEMUSAGE_AUTO_START) &&  defined(CONFIG_COUNTER_MON_AUTO_START_PERIOD)
/* The feature is for auto start cpu usage for taking the log
 * of the secified time */
static struct hrtimer memusage_auto_timer;
static struct timespec tv = {.tv_sec = CONFIG_COUNTER_MON_START_SEC, .tv_nsec =
	    0};

/* Auto start loging the cpu usage data */
static enum hrtimer_restart memusage_auto_start(struct hrtimer *ht)
{
	static int started;
	enum hrtimer_restart h_ret = HRTIMER_NORESTART;

	BUG_ON(started != 0 && started != 1);

	if (!sec_memusage_init_flag) {
		PRINT_KD("Error: MemUsage Not Initialized\n");
		return HRTIMER_NORESTART;
	}
	/* Make the status running */
	if (!started) {

		sec_memusage_run_state = 1;
		/* restart timer at finished seconds. */
		tv.tv_sec = CONFIG_COUNTER_MON_FINISHED_SEC;
		tv.tv_nsec = 0;

		hrtimer_forward(&memusage_auto_timer, ktime_get(),
				timespec_to_ktime(tv));
		started = 1;
		h_ret = HRTIMER_RESTART;

	} else {

		if (!sec_memusage_status)
			sec_memusage_run_state = 0;

		h_ret = HRTIMER_NORESTART;
	}
	return h_ret;
}
#endif

int kdbg_memusage_init(void)
{
	sec_memusage_init_flag = 0;

#ifdef CONFIG_SEC_MEMUSAGE_AUTO_START
	sec_memusage_init();
#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	hrtimer_init(&memusage_auto_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	memusage_auto_timer._softexpires = timespec_to_ktime(tv);
	memusage_auto_timer.function = memusage_auto_start;
	hrtimer_start(&memusage_auto_timer, memusage_auto_timer._softexpires,
		      HRTIMER_MODE_REL);
#else
	sec_memusage_run_state = 1;
#endif
#endif

	kdbg_register("COUNTER MONITOR: Memory Usage", sec_memusage_control,
		      turnoff_memusage, KDBG_MENU_COUNTER_MONITOR_MEMORY_USAGE);

	return 0;
}
