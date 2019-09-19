/*
 * kdbg-core.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by lee jung-seung(js07.lee@samsung.com)
 *
 * NOTE:
 *
 */
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include "internal.h"

#include <kdebugd.h>
#include "kdbg-version.h"
#include <mstar/mpatch_macro.h>

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
#include "sec_cpuusage.h"
#include "sec_memusage.h"
#include "sec_netusage.h"
#include "sec_topthread.h"
#include "sec_diskusage.h"
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
#include "sec_perfcounters.h"
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
#endif

#ifdef CONFIG_KDEBUGD_LIFE_TEST
#include "kdbg_key_test_player.h"
#endif

#include <linux/module.h>

/* Kdbg_auto_key_test */
#include <linux/completion.h>
#include <linux/delay.h>
#define AUTO_KET_TEST_TOGGLE_KEY "987"
#ifdef CONFIG_KDEBUGD_BG
#define AUTO_KET_TEST_TOGGLE_KEY_BG_FG "bgtofg"
#define AUTO_KET_TEST_TOGGLE_KEY_BG "bgstart"
#endif
/* Kdbg_auto_key_test */

static DECLARE_WAIT_QUEUE_HEAD(kdebugd_wait);
static DEFINE_SPINLOCK(kdebugd_queue_lock);
struct debugd_queue kdebugd_queue;

struct task_struct *kdebugd_tsk;
unsigned int kdebugd_running;
#ifdef CONFIG_KDEBUGD_BG
unsigned int kdbg_mode;
#endif

struct proc_dir_entry *kdebugd_dir;

/* System event handler call back function */
static int (*g_pKdbgSysEventHandler) (debugd_event_t *event);
/*
 * Debugd event queue management.
 */
static inline int queue_empty(struct debugd_queue *q)
{
	return q->event_head == q->event_tail;
}

static inline void queue_get_event(struct debugd_queue *q,
				   debugd_event_t *event)
{
	BUG_ON(!event);
	q->event_tail = (q->event_tail + 1) % DEBUGD_MAX_EVENTS;
	*event = q->events[q->event_tail];
}

void queue_add_event(struct debugd_queue *q, debugd_event_t *event)
{
	unsigned long flags;

	BUG_ON(!event);
	spin_lock_irqsave(&kdebugd_queue_lock, flags);
	q->event_head = (q->event_head + 1) % DEBUGD_MAX_EVENTS;
	if (q->event_head == q->event_tail) {
		static int notified;

		if (notified == 0) {
			PRINT_KD("\n");
			PRINT_KD("kdebugd: an event queue overflowed\n");
			notified = 1;
		}
		q->event_tail = (q->event_tail + 1) % DEBUGD_MAX_EVENTS;
	}
	q->events[q->event_head] = *event;
	spin_unlock_irqrestore(&kdebugd_queue_lock, flags);
	wake_up_interruptible(&kdebugd_wait);
}

/* Register system event handler */
void kdbg_reg_sys_event_handler(int (*psys_event_handler) (debugd_event_t *))
{
	if (!g_pKdbgSysEventHandler) {
		g_pKdbgSysEventHandler = psys_event_handler;
		PRINT_KD("Registering System Event Handler\n");
	} else if (!psys_event_handler) {
		g_pKdbgSysEventHandler = NULL;
		PRINT_KD("Deregistering System Event Handler\n");
	} else {
		PRINT_KD("ERROR: System Event Handler is Already Enabled\n");
	}
}

/*
 * debugd_get_event_as_numeric
 *
 * Description
 * This API returns the input given by user as long value which can be
 * processed by kdebugd developers.
 * and If user enters a invalid value i.e. which can not be converted to string,
 * is_number is set to zero and entered value is returned as a string
 * i.e. event->input_string.
 *
 * Usage
 * debugd_event_t event;
 * int is_number;
 * int option;
 *
 * option = debugd_get_event_as_numeric(&event, &is_number);
 * if (is_number) {
 *	valid long value, process it i.e. option
 * } else {
 * printk("Invalid Value %s\n", event.input_string);
 * }
 */
long debugd_get_event_as_numeric(debugd_event_t *event, int *is_number)
{
	debugd_event_t temp_event;
	long value = 0;
	char *ptr_end = NULL;
	int is_number_flag = 1;
	int base = 10;

	debugd_get_event(&temp_event);

	/* convert to numeric */
	if (temp_event.input_string[0] == '0'
	    && temp_event.input_string[1] == 'x') {
		base = 16;	/* hex */
	} else {
		base = 10;	/* decimal */
	}
	value = simple_strtol(temp_event.input_string, &ptr_end, base);

	/* check if pure number */
	if (!ptr_end || *ptr_end || ptr_end == temp_event.input_string) {
		value = -1L;
		is_number_flag = 0;
	}

	/* output parameters */
	if (is_number)
		*is_number = is_number_flag;

	if (event)
		*event = temp_event;

	return value;
}

/*
 * debugd_get_event
 *
 * Description
 * This API returns the input given by user as event and no checks are
 * performed on input.
 * It is returned to user as string in i.e. event->input_string.
 *
 * Usage
 * debugd_event_t event;
 * debugd_get_event(&event);
 * process it event.input_string
 * printk("Value %s\n", event.input_string);
 */
void debugd_get_event(debugd_event_t *event)
{
	debugd_event_t temp_event;

#ifdef CONFIG_KDEBUGD_LIFE_TEST
	int ret = 1;

	while (1) {
#endif
		wait_event_interruptible(kdebugd_wait,
					 !queue_empty(&kdebugd_queue)
					 || kthread_should_stop());
		spin_lock_irq(&kdebugd_queue_lock);
		if (!queue_empty(&kdebugd_queue))
			queue_get_event(&kdebugd_queue, &temp_event);
		else
			temp_event.input_string[0] = '\0';

		spin_unlock_irq(&kdebugd_queue_lock);

#ifdef CONFIG_KDEBUGD_LIFE_TEST
		if (g_pKdbgSysEventHandler) {
			ret = (*g_pKdbgSysEventHandler) (&temp_event);
			if (ret)
				break;
		} else
			break;
	}
#endif

	if (event)
		*event = temp_event;
}

/* command input common code */
/* Note: After getting the task, task lock has been taken inside this function
 * Use put_task_struct for the task if lock get succesfully
 */
struct task_struct *get_task_with_pid(void)
{
	struct task_struct *tsk;

	long event;

	PRINT_KD("\n");
	PRINT_KD("Enter pid of task...\n");
	PRINT_KD("===>  ");
	event = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD("\n");

	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */
	/*Take RCU read lock register can be changed */
	rcu_read_lock();

	tsk = find_task_by_pid_ns(event, &init_pid_ns);
	if (tsk) {
		/*Increment usage count */
		get_task_struct(tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (tsk == NULL)
		return NULL;

	PRINT_KD("\n\n");
	PRINT_KD("Pid: %d, comm: %20s", tsk->pid, tsk->comm);
#ifdef CONFIG_SMP
	PRINT_KD("[%d]", task_cpu(tsk));
#endif
	PRINT_KD("\n");
	return tsk;
}

/* command input common code */
struct task_struct *get_task_with_given_pid(pid_t pid)
{
	struct task_struct *tsk;

	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */

	/*Take RCU read lock register can be changed */
	rcu_read_lock();

	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	if (tsk) {
		/*Increment usage count */
		get_task_struct(tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (tsk == NULL)
		return NULL;

	PRINT_KD("\n\n");
	PRINT_KD("Pid: %d, comm: %20s\n", tsk->pid, tsk->comm);
	return tsk;
}

struct task_struct *find_user_task_with_pid(void)
{
	struct task_struct *tsk;
	long event;

	PRINT_KD("\n");
	PRINT_KD("Enter pid of task...\n");
	PRINT_KD("===>  ");
	event = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD("\n");

	rcu_read_lock();

	tsk = find_task_by_pid_ns(event, &init_pid_ns);

	if (tsk)
		get_task_struct(tsk);
	/*Unlock */
	rcu_read_unlock();

	if (tsk == NULL || !tsk->mm) {
		PRINT_KD("\n[ALERT] %s Thread",
			 (tsk == NULL) ? "No" : "Kernel");
		if (tsk)
			put_task_struct(tsk);
		return NULL;
	}

	PRINT_KD("\n\n");
	PRINT_KD("Pid: %d, comm: %20s", tsk->pid, tsk->comm);
#ifdef CONFIG_SMP
	PRINT_KD("[%d]", task_cpu(tsk));
#endif
	PRINT_KD("\n");

	return tsk;
}

void task_state_help(void)
{
	PRINT_KD("\nRSDTtZXxKW:\n");
	PRINT_KD
	("R : TASK_RUNNING       S:TASK_INTERRUPTIBLE   D:TASK_UNITERRUPTIBLE\n");
	PRINT_KD
	("T : TASK_STOPPED       t:TASK_TRACED          Z:EXIT_ZOMBIE\n");
	PRINT_KD
	("X : EXIT_DEAD          x:TASK_DEAD            K:TASK_WAKEKILL \n");
	PRINT_KD
	("W : TASK_WAKEING \n");

	PRINT_KD("\nSched Policy\n");
	PRINT_KD("SCHED_NORMAL : %d\n", SCHED_NORMAL);
	PRINT_KD("SCHED_FIFO   : %d\n", SCHED_FIFO);
	PRINT_KD("SCHED_RR     : %d\n", SCHED_RR);
	PRINT_KD("SCHED_BATCH  : %d\n", SCHED_BATCH);
}

/*
 *    uptime
 */

unsigned long kdbg_get_uptime(void)
{
	struct timespec uptime;

	do_posix_clock_monotonic_gettime(&uptime);

	return (unsigned long)uptime.tv_sec;
}

/*
 *    kdebugd()
 */

struct kdbg_entry {
	const char *name;
	int (*execute) (void);
	void (*turnoff) (void);
	KDBG_MENU_NUM menu_index;
};

struct kdbg_base {
	unsigned int index;
	struct kdbg_entry entry[KDBG_MENU_MAX];
};

static struct kdbg_base k_base;

int kdbg_register(char *name, int (*func) (void), void (*turnoff) (void),
		  KDBG_MENU_NUM menu_idx)
{
	int idx = k_base.index;
	struct kdbg_entry *cur;

	if (!name || !func) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Invalid params, name %p, func %p !!!\n", name,
			 func);
		return -ENOMEM;
	}

	if (idx >= KDBG_MENU_MAX) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Can not add kdebugd function, menu_idx %d !!!\n",
			 menu_idx);
		return -ENOMEM;
	}

	cur = &(k_base.entry[idx]);

	cur->name = name;
	cur->execute = func;
	cur->turnoff = turnoff;
	cur->menu_index = menu_idx;
	k_base.index++;

	return 0;
}

int kdbg_unregister(KDBG_MENU_NUM menu_idx)
{
	struct kdbg_entry *cur;
	int menu_found = 0;
	int i;

	if (menu_idx >= KDBG_MENU_MAX) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Can not remove kdebugd function, menu_idx %d !!!\n",
			 menu_idx);
		return -ENOMEM;
	}

	for (i = 0; i < k_base.index; i++) {
		if (menu_idx == k_base.entry[i].menu_index) {
			cur = &(k_base.entry[i]);
			/* reset execute function pointer */
			cur->execute = NULL;
			menu_found = 1;
			break;
		}
	}

	if (!menu_found) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Can not remove kdebugd function, menu_idx %d !!!\n",
			 menu_idx);
		return -1;
	}

	return 0;
}

static void debugd_menu(void)
{
	int i;
	PRINT_KD("\n");
	PRINT_KD
		(" --- Menu --------------------------------------------------------------\n");
	PRINT_KD(" Select Kernel Debugging Category. - %s\n", KDBG_VERSION);
	for (i = 0; i < k_base.index; i++) {
		if (i % 4 == 0)
			PRINT_KD
			    (" -----------------------------------------------------------------------\n");
		if (k_base.entry[i].execute != NULL) {
			PRINT_KD(" %-2d) %s.\n", k_base.entry[i].menu_index,
				 k_base.entry[i].name);
		}
	}
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");
	PRINT_KD(" 99) exit\n");
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");
	PRINT_KD
	    (" MStar Kernel debugging - DMC - VD Division - S/W Platform Lab1 - Linux Part\n");
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");

}

static int kdebugd(void *arg)
{
	long event;
	unsigned int idx;
	int i = 0;
	int menu_flag = 1;
	int menu_found = 0;

	do {
		if (kthread_should_stop())
			break;

		if (menu_flag) {
			debugd_menu();
			PRINT_KD("%s #> ", sched_serial);
		} else
			menu_flag = 1;

		event = debugd_get_event_as_numeric(NULL, NULL);
		idx = event - 1;
		PRINT_KD("\n");

		/* exit */
		if (event == 99)
			break;

		/* execute operations */
		for (i = 0; i < k_base.index; i++) {
			if (event == k_base.entry[i].menu_index) {
				if (k_base.entry[i].execute != NULL) {
					PRINT_KD("[kdebugd] %d. %s\n",
						 k_base.entry[i].menu_index,
						 k_base.entry[i].name);
					menu_flag =
					    (*k_base.entry[i].execute) ();
				}
				menu_found = 1;
				break;
			}
		}

		if (!menu_found) {
			for (idx = 0; idx < k_base.index; idx++) {
				if (k_base.entry[idx].turnoff != NULL)
					(*k_base.entry[idx].turnoff) ();
			}
		}

		menu_found = 0;
	} while (1);

	PRINT_KD("\n");
	PRINT_KD("[kdebugd] Kdebugd Exit....");
	kdebugd_running = 0;

#ifdef CONFIG_KDEBUGD_LIFE_TEST
	kdbg_stop_key_test_player_thread();
#endif

	return 0;
}

int kdebugd_start(void)
{
	int ret = 0;

	kdebugd_tsk = kthread_create(kdebugd, NULL, "kdebugd");
	if (IS_ERR(kdebugd_tsk)) {
		ret = PTR_ERR(kdebugd_tsk);
		kdebugd_tsk = NULL;
		return ret;
	}

	kdebugd_tsk->flags |= PF_NOFREEZE;
	wake_up_process(kdebugd_tsk);

	return ret;
}

int kdebugd_status(void)
{
	int key;

	while (1) {

		PRINT_KD
		    ("------------------ COUNTER MONITOR STATUS -------------------\n");
		PRINT_KD
		    ("NUM   MONITOR               INIT STATE         RUN STATE\n");
		PRINT_KD
		    ("=== ===================== ===============    ============\n");

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
		PRINT_KD("1   CPU USAGE             ");
		sec_cpuusage_get_status();
		PRINT_KD("2   TOP THREAD            ");
		sec_topthread_get_status();
		PRINT_KD("3   MEM USAGE             ");
		get_memusage_status();
		PRINT_KD("4   NET USAGE             ");
		get_netusage_status();
		PRINT_KD("5   DISK USAGE            ");
		get_diskusage_status();
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS
		PRINT_KD("6   PMU_EVENTS  ");
		get_perfcounters_status();
#endif /* CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS */
#endif

#ifdef CONFIG_SCHED_HISTORY
#if defined(CONFIG_CACHE_ANALYZER) && (MP_DEBUG_TOOL_OPROFILE == 1)
		PRINT_KD("7   SCHED HISTORY LOGGER  ");
#else
		PRINT_KD("6   SCHED HISTORY LOGGER  ");
#endif
		status_sched_history();
#endif
		PRINT_KD("99- Exit\n");
		PRINT_KD
		    ("------------------------- STATUS END ------------------------\n");
		PRINT_KD("*HELP*\n");
		PRINT_KD
		    ("  A) To Enable print and dump the respective Counter Monitor go to the kdebugd menu "
		     "and give the corresponding options\n");
		PRINT_KD
		    ("  B) [TurnOn] -->Initialize the feature and run in the background\n");
		PRINT_KD
		    ("  C) [TurnOff] -->Free the resources occoupied by the functionality\n\n");
		PRINT_KD("To Turn On/off feature enter corresponding number\n");
		PRINT_KD("-->");
		key = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (key) {

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
		case 1:
			{
				sec_cpuusage_on_off();
			}
			break;
		case 2:
			{
				sec_topthread_on_off();
			}
			break;

		case 3:
			{
				sec_memusage_OnOff();
			}
			break;
		case 4:
			{
				sec_netusage_OnOff();
			}
			break;
		case 5:
			{
				sec_diskusage_OnOff();
			}
			break;
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS
		case 6:
			{
				sec_perfcounters_OnOff();
			}
			break;
#endif /* CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS */
#endif
#ifdef CONFIG_SCHED_HISTORY
#if defined(CONFIG_CACHE_ANALYZER) && (MP_DEBUG_TOOL_OPROFILE == 1)
		case 7:
#else
		case 6:
#endif
			{
				sched_history_OnOff();
			}
			break;
#endif
		case 99:
			{
				PRINT_KD("Exiting...\n");
				return 1;
			}
		default:
			{
				PRINT_KD("Invalid Choice\n");
				break;
			}
		}
	}
	return 1;
}

/* Kdbg_auto_key_test */

/**********************************************************************
 *                                                                    *
 *              proc related functions                       *
 *                                                                    *
 **********************************************************************/

#define KDBG_AUTO_KEY_TEST_NAME	"kdbgd/auto_key_test"

/**
 * The buffer used to store character for this module
 *
 */
static char kdbg_auto_key_test_buffer[DEBUGD_MAX_CHARS];

/**
 * The size of the buffer
 *
 */
static ssize_t kdbg_auto_key_test_buffer_size;

/*auto key test activate*/
static int kdbg_auto_key_test_activate(void);

/* for /proc/kdbeugd/auto_key_test writing */
static ssize_t kdbg_auto_key_test_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);

/* for /proc/kdbeugd/auto_key_test  reading */
static ssize_t kdbg_auto_key_test_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);

/* for /proc/kdbeugd/auto_key_test  file operation(wrap the read & write) */
struct file_operations kdbg_auto_key_test_operation;

/*auto key test activate*/
static int kdbg_auto_key_test_activate(void)
{
	static struct proc_dir_entry *kdbg_auto_key_test_proc_dentry;
	int err = 0;

	if (!kdbg_auto_key_test_proc_dentry) {
		kdbg_auto_key_test_proc_dentry =
		    proc_create(KDBG_AUTO_KEY_TEST_NAME, 0, NULL, &kdbg_auto_key_test_operation);

		if (!kdbg_auto_key_test_proc_dentry) {
			printk(KERN_WARNING
			       "/proc/kdebugd/auto_key_test creation failed\n");
			err = -ENOMEM;
		}
	}

	return err;
}

/**
 * This function is called with the /proc file is read
 *
 */
static ssize_t kdbg_auto_key_test_read(struct file *filp, char __user *buffer, size_t count, loff_t *offset)
{
	int ret;

	if (offset > 0) {
		/* we have finished to read, return 0 */
		ret = 0;
	} else {
		/* fill the buffer, return the buffer size */
		memcpy(buffer, kdbg_auto_key_test_buffer,
		       kdbg_auto_key_test_buffer_size);
		ret = kdbg_auto_key_test_buffer_size;
	}

	return ret;

}

/**
 * This function is called with the /proc file is written
 *
 */
static ssize_t kdbg_auto_key_test_write(struct file *filp, const char __user *buffer, size_t count, loff_t *ppos)
{
	debugd_event_t event;
	char *ptr = NULL;

	/* get buffer size */
	if (count > DEBUGD_MAX_CHARS)
		kdbg_auto_key_test_buffer_size = DEBUGD_MAX_CHARS;
	else
		kdbg_auto_key_test_buffer_size = count;

	/* write data to the buffer */
	if (copy_from_user
	    (kdbg_auto_key_test_buffer, buffer,
	     kdbg_auto_key_test_buffer_size)) {
		return -EFAULT;
	}
	kdbg_auto_key_test_buffer[kdbg_auto_key_test_buffer_size - 1] = '\0';

	/* remove leading and trailing whitespaces */
	ptr = strstrip(kdbg_auto_key_test_buffer);

	BUG_ON(!ptr);

	if (!strncmp(ptr, "NO_WAIT", sizeof("NO_WAIT") - 1)) {
		ptr += sizeof("NO_WAIT") - 1;
	} else {
		/* wait till queue is empty */
		while (!queue_empty(&kdebugd_queue))
			msleep(200);
	}

	/* create a event */
	strncpy(event.input_string, ptr, sizeof(event.input_string) - 1);
	event.input_string[sizeof(event.input_string) - 1] = '\0';
#ifdef CONFIG_KDEBUGD_BG
	if (!strcmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY_BG_FG))
		kdbg_mode = 0;
#endif

	/* Magic key to start/stop the thread */
#ifdef CONFIG_KDEBUGD_BG
	if (!strcmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY) || !strcmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY_BG)) {
#else
	if (!strcmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY)) {
#endif
		if (!kdebugd_running) {
			kdebugd_start();
			kdebugd_running = 1;
#ifdef CONFIG_KDEBUGD_BG
			if (!strcmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY_BG))
				kdbg_mode = 1;
#endif
		} else {
			debugd_event_t temp_event = { "99"};
			do {
				/* kdebugd menu exit */
				queue_add_event(&kdebugd_queue, &temp_event);
				msleep(2000);
			} while (kdebugd_running);
		}
	} else if (kdebugd_running)
		queue_add_event(&kdebugd_queue, &event);

	return kdbg_auto_key_test_buffer_size;
}

struct file_operations kdbg_auto_key_test_operation = {
         .read = kdbg_auto_key_test_read,
         .write = kdbg_auto_key_test_write,
};

/* Kdbg_auto_key_test */

static int __init kdebugd_init(void)
{
	int rv = 0;
	kdebugd_dir = proc_mkdir("kdbgd", NULL);
	if (kdebugd_dir == NULL)
		rv = -ENOMEM;

/* Kdbg_auto_key_test */
	rv = kdbg_auto_key_test_activate();
/* Kdbg_auto_key_test */

	return rv;
}

static void __exit kdebugd_cleanup(void)
{
	remove_proc_entry("kdbgd", NULL);
}

module_init(kdebugd_init);
module_exit(kdebugd_cleanup);
