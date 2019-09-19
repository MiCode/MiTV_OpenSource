/*
 * kdbg-futex.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by himanshu.maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#include <linux/kernel.h>
#include <kdebugd.h>
#include "kdbg_util.h"
#include <linux/mm.h>
#include <linux/slab.h>
#ifdef CONFIG_ELF_MODULE
#include "elf/kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#define WAITERS_DEFAULT 100
#define MAX_WAITERS_ALLOWED 5000
#define WAITTIME_DEFAULT 10000

#ifdef DBG_FUTEX_LIST
#define dprintk PRINT_KD
#else
#define dprintk(...) do {} while (0)
#endif

enum {
	FUTEX_MENU_CHANGE_CONFIG = 1,	/* To see/edit the configuration */
	FUTEX_MENU_SHOW_LIST,	/* To dump the futex list */
	FUTEX_MENU_SHOW_LIST_WITH_BACKTRACE, /* To dump the futex list and backtrace */
	FUTEX_MENU_PID_DBG_START, /* start pid specific debugging */
	FUTEX_MENU_PID_DBG_STOP_DUMP, /* stop pid specific debugging */
	FUTEX_MENU_EXIT = 99,	/* To exit the menu */
};

/* this is not re-enterant. it is by design to avoid fragmentation */
static void display_futex_lists(const int max_waiters, const int wait_msec,
				const int print_backtrace)
{
	int i = 0, count = 0;
	static struct futex_waiter *waiter_array;
	static int current_array_size;

#if defined(CONFIG_KDEBUGD_TRACE) && defined(CONFIG_ELF_MODULE)
	if (print_backtrace) {
		dprintk("Loading ELF Database....\n");
		/* If the elf is already loaded, it won't load again */
		if (kdbg_elf_load_elf_db_for_all_process())
			PRINT_KD("Error in loading ELF DB !!!!!!\n");
		else
			PRINT_KD("Loading ELF Database completed....\n");
	}
#endif
	dprintk
	    ("max_waiters: %d, min wait time: %d msec, print_backtrace: %d\n",
	     max_waiters, wait_msec, print_backtrace);

	/* avoid fragmentation - save the buffer size, so if its sufficient for
	 * next time, we don't need to allocate */
	dprintk("current_size: %d, max_waiters: %d\n", current_array_size,
		max_waiters);
	if (max_waiters > current_array_size) {
		if (waiter_array) {
			dprintk("Freeing memory....\n");
			kfree(waiter_array);
			waiter_array = NULL;
			current_array_size = 0;
		}
	}

	if (!waiter_array) {
		dprintk("Allocating memory....\n");
		waiter_array =
		    (struct futex_waiter *)
		    kmalloc((sizeof(struct futex_waiter) * max_waiters),
			    GFP_KERNEL);
		if (!waiter_array) {
			PRINT_KD("Insufficient memory");
			return;
		}
		current_array_size = max_waiters;
		memset(waiter_array, 0,
		       sizeof(struct futex_waiter) * max_waiters);
	}

	count = read_hash_queue(waiter_array, max_waiters, wait_msec);
	if ((count < 0) || (count > max_waiters)) {
		PRINT_KD("Error in reading hash queue!!!\n");
		return;
	}

	dprintk("No. of entries filled: %d\n", count);

	PRINT_KD("\n");
	PRINT_KD
	    ("===============================================================================\n");
	PRINT_KD("  * FUTEX LIST *\n");
	PRINT_KD
	    ("Idx   Hash-Key  Thread ID       Thread Name     WaitTime(ms)\n");
	PRINT_KD
	    ("===============================================================================\n");
	for (i = 0; i < count; i++) {
		PRINT_KD("%3d     %5d      %5d     %16s     %10u\n", i + 1,
			 waiter_array[i].hash_key, waiter_array[i].task->pid,
			 waiter_array[i].task->comm, waiter_array[i].wait_msec);
		if (print_backtrace) {
#ifdef CONFIG_KDEBUGD_TRACE
			PRINT_KD
			    ("--backtrace--------------------------------------------------------------------\n");
			show_user_bt(waiter_array[i].task);
#else
			PRINT_KD
			    ("#### Backtrace not available - compile with CONFIG_KDEBUGD_TRACE\n");
#endif
		}
	}
	PRINT_KD
	    ("===============================================================================\n");
}

/*
 * configuratble items for futex:
 *	wait_msec: minimum waiting time
 *	max_waiters: waiting list
 *	kdbg_futex_dbg_pid: PID of thread to be debugged
 */
static int max_waiters = WAITERS_DEFAULT;
static int wait_msec = WAITTIME_DEFAULT;
int kdbg_futex_dbg_pid = -1;

/*
 * flag to trigger debugging
 * (precond: kdbg_futex_dbg_pid should be configured)
 */
int kdbg_futex_dbg_start;

/*
 * the interval in which a thread waits on futex (min,max)
 */
int kdbg_futex_config_min_wait_msec = 50;

static void futex_main_menu(void)
{
	PRINT_KD("\n");
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("1)  Futex Debug: Setup Configuration.\n");
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("2)  Futex Debug: Display Futex List\n");
	PRINT_KD("3)  Futex Debug: Display Futex List with Backtrace\n");
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("4)  Futex Debug: Start Lock Latency Detector");
	PRINT_KD(" (Latency > %d msec)\n", kdbg_futex_config_min_wait_msec);
	PRINT_KD("5)  Futex Debug: Stop Lock Latency Detector\n");
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("99) Futex Debug: Exit Menu\n");
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("\n");
	PRINT_KD("Select Option==>  ");
}

/* to open/setup futex list configuration */
static void futex_config_menu(void)
{
	int wait_time = 0;
	int list_size = 0;
	int dbg_pid = 0;
	int option = 0;

	do {
		PRINT_KD("--------------------------------------------------------------\n");
		PRINT_KD("1)  Futex Config: Show Configuration.\n");
		PRINT_KD("--------------------------------------------------------------\n");
		PRINT_KD("2)  Futex Config: Minimum waiting time in msec.\n");
		PRINT_KD("3)  Futex Config: Maximum List Size (<= 5000) to be displayed.\n");
		PRINT_KD("4)  Futex Config: Filter PID for Lock Latency Detector.\n");
		PRINT_KD("5)  Futex Config: Latency Time for Lock Latency Detector.\n");
		PRINT_KD("--------------------------------------------------------------\n");
		PRINT_KD("99) Futex Config: Exit Menu.\n");
		PRINT_KD("--------------------------------------------------------------\n");
		PRINT_KD("Select Option==>  ");
		option = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (option) {
		case 1:
			PRINT_KD("Current Configuration\n");
			PRINT_KD("Minimum waiting time in msec                : %d msec\n",
					wait_msec);
			PRINT_KD("Maximum List Size (<= 5000) to be displayed : %d\n",
					max_waiters);
			if (kdbg_futex_dbg_pid == -1)
				PRINT_KD("Filter PID for Lock Latency Detector        : No PID\n");
			else
				PRINT_KD("Filter PID for Lock Latency Detector        : %d\n",
						kdbg_futex_dbg_pid);
			PRINT_KD("Latency Time for Lock Latency Detector      : %d\n",
					kdbg_futex_config_min_wait_msec);
			PRINT_KD("\n");
			break;

		case 2:
			PRINT_KD("\n");
			PRINT_KD
				("Minimum waiting time in msec (current: %d) ==> ",
				 wait_msec);
			wait_time = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			/* for empty, debugd_get_event_as_numeric() returns 0,
			 * we want to make that case invalid */
			if (wait_time <= 0) {
				PRINT_KD("Error in config. Keeping default - %d...\n",
					 wait_msec);
			} else {
				wait_msec = wait_time;
			}

			break;

		case 3:
			PRINT_KD
				("Maximum List Size (<= %d) to be displayed (current: %d)==> ",
				 MAX_WAITERS_ALLOWED, max_waiters);
			list_size =
				debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			if (list_size <= 0
					|| list_size > MAX_WAITERS_ALLOWED) {
				PRINT_KD("Error in config. Keeping default - %d...\n",
					 max_waiters);
			} else {
				max_waiters = list_size;
			}

			break;

		case 4:
			if (kdbg_futex_dbg_pid == -1) {
				PRINT_KD("Enter the PID (current: No PID)==> ");
			} else {
				PRINT_KD("Enter the PID (current: %d)==> ",
						kdbg_futex_dbg_pid);
			}

			dbg_pid = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			/* for empty, debugd_get_event_as_numeric() returns 0,
			 * we want to make that case invalid */
			if (dbg_pid <= 0) {
				PRINT_KD("Error in config. Keeping default - ");
				if (kdbg_futex_dbg_pid == -1)
					PRINT_KD("No PID\n");
				else
					PRINT_KD("%d\n", kdbg_futex_dbg_pid);
			} else {
				kdbg_futex_dbg_pid = dbg_pid;
			}

			break;

		case 5:
			PRINT_KD("\n");
			PRINT_KD("Minimum latency for futex (current: %d) ==> ",
						kdbg_futex_config_min_wait_msec);
			wait_time = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			if (wait_time <= 0) {
				PRINT_KD("Error in config. Keeping default - %d...\n",
					 kdbg_futex_config_min_wait_msec);
			} else {
				kdbg_futex_config_min_wait_msec = wait_time;
			}

			break;

		case 99:
			break;

		default:
			PRINT_KD("\n");
			PRINT_KD("\nInvalid Option....\n");
			break;

		}
	} while (option != 99);
}

static int futex_list_menu(void)
{
	int operation = 0;

	do {
		futex_main_menu();
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (operation) {
		case FUTEX_MENU_CHANGE_CONFIG:
			futex_config_menu();
			break;

		case FUTEX_MENU_SHOW_LIST:
			display_futex_lists(max_waiters, wait_msec, 0);
			break;

		case FUTEX_MENU_SHOW_LIST_WITH_BACKTRACE:
			display_futex_lists(max_waiters, wait_msec, 1);
			break;

		case FUTEX_MENU_PID_DBG_START:
			/* check kdbg_futex_dbg_pid configured */
			if ((kdbg_futex_dbg_pid < 1)
					|| (kdbg_futex_dbg_pid > 65535)) {
				PRINT_KD("ERR: PID not configured..\n");
				break;
			}
			/* check if it is valid */
			kdbg_futex_dbg_start = 1;
			PRINT_KD("Lock latency detection started...\n");
			PRINT_KD("   PID: %d, Min latency: %d msec\n",
					kdbg_futex_dbg_pid,
					kdbg_futex_config_min_wait_msec);
			break;

		case FUTEX_MENU_PID_DBG_STOP_DUMP:
			/* remove pid filter in futex */
			kdbg_futex_dbg_start = 0;
			/* print any additional info:
			 *	e.g. list of the locks with waiting time
			 */
			break;

		case FUTEX_MENU_EXIT:
			break;

		default:
			PRINT_KD("\n");
			PRINT_KD("\nInvalid Option....\n  ");
			break;
		}
	} while (operation != FUTEX_MENU_EXIT);
	PRINT_KD("Futex Debug menu exit....\n");

	/* return value is true - to show the kdebugd menu options */
	return 1;
}

int kdbg_futex_init(void)
{
	return kdbg_register("DEBUG: Show Futex List", futex_list_menu, NULL,
			     KDBG_MENU_FUTEX);
}
