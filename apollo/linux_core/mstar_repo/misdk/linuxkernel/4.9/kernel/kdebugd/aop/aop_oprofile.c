
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <kdebugd.h>
#include "aop_oprofile.h"
#include "aop_report.h"
#include "aop_kernel.h"
#include "aop_report_symbol.h"
#include <mstar/mpatch_macro.h>

#ifdef	CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#include "aop_debug.h"

#include "../sec_workq.h"

/* default event buffer sampling rate for 100 samples delay*/
#define AOP_CONFIG_DEFAULT_SAMPLING_MS	((1000 * 100)/(HZ))

/* default event buffer size*/
#define AOP_CONFIG_DEFAULT_EVENT_BUFF		(256*AOP_KB)
#define AOP_CONFIG_MIN_EVENT_BUFF			(30*AOP_KB)
#define AOP_CONFIG_MAX_EVENT_BUFF			(8*AOP_KB*AOP_KB)

/* task state related defines */
#define AOP_TSK_NOT_CREATED		0
#define AOP_TSK_ALREADY_CREATED	1

/* enum for adv oprofile submenu */
enum {
	AOP_MENU_CHANGE_CONFIG = 1,	/* to set oprofile configuration */
	AOP_MENU_SHOW_CONFIG,	/* to show oprofile configuration */
	AOP_MENU_START_PROFILE,	/* to start oprofile and aop buff task  */
	AOP_MENU_STOP_PROFILE,	/* to stop oprofile and aop buff task  */
	AOP_MENU_SHOW_CUR_STATE,	/* to show oprofile running state */
	AOP_MENU_REPORT_APPL_SAMPLE,	/* generate report for application Samples */
	AOP_MENU_REPORT_LIB_SAMPLE,	/* generate report for library Samples */
	AOP_MENU_REPORT_KERNEL_SAMPLE,	/* generate report for kernel Samples */
	AOP_MENU_REPORT_TGID,	/* generate report --- ProcessID wise */
	AOP_MENU_REPORT_TID,	/* generate report --- ThreadID wise */
	AOP_MENU_REPORT_ALL_SAMPLE,	/* generate report for all Samples */
	AOP_MENU_REPORT_SYMBOL_SYSTEM_WIDE,	/*system wide symbol info including
						   application & library name */
	AOP_MENU_REPORT_SYMBOL_APPL_LIB_SAMPLE,	/* application wise symbol info
						   including library name */
	AOP_MENU_REPORT_SYMBOL_ALL,	/* report all symbols */
#ifdef CONFIG_SMP
	AOP_MENU_REPORT_CPU_WISE,
#endif
	AOP_DUMP_ALL_SAMPLES_CALLGRAPH, /* callgraph */
#if AOP_DEBUG_ON
	AOP_DUMP_ALL_SAMPLES
#endif /* AOP_DEBUG_ON */
};

/* enum for adv oprofile configuration submenu */
enum {
	AOP_CONFIG_BUFF_SIZE = 1,	/* to configure event cache size */
	AOP_CONFIG_SORT_OPTION,	/* to configure sort option for reports */
	AOP_CONFIG_DEMANGLE_SYM_NAME,	/* to configure symbol name demangle option */
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
	AOP_CONFIG_SAMPLES_SOURCE,	/* to configure source of the
						sampling interrupts option */
	AOP_SHOW_SAMPLES_SOURCE,	/* to show source of the sampling
							interrupts option */
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
};

/* enum for adv oprofile configuration submenu */
enum {
	AOP_REPORT_TID_CPU_ALL = 0,	/* to show report for ALL CPU */
	AOP_REPORT_TID_CPU_WISE,	/* to show report CPU wise */
};

/* string for adv oprofile sort option */
static const char *aop_config_sort_string[3] = {
	"Default",		/* sort option none */
	"Sort by vma address",	/* sort by vma address */
	"sort by samples"	/* sort report by samples */
};

/* this buffer size is defined based on the memory requirement experiment
  * done for 30 min. For 30mins of profiling, we got 1.2MB data.
  * It may be reduced by setup filter setting
  */
static unsigned long aop_event_buffer_size = AOP_CONFIG_DEFAULT_EVENT_BUFF;

/* aop report sort option, by default it is none.
It may be changed using setup configuration option */
int aop_config_sort_option = AOP_SORT_BY_SAMPLES;

struct task_struct *aop_buff_tsk;

/* aop report symbol demangle option, by default it is none.
It may be changed using setup configuration option.
By default demangling function name is ON */
int aop_config_demangle = 1;

/* global structure for a buffer used to store the event buffer data*/
struct aop_cache_type aop_cache;

/* to update task create status */
static unsigned int aop_buff_tsk_created;

/* to know oprofile running status */
static unsigned int aop_profile_running;

/* copy the dcookie pointer, when register a decookie. this will later used
to unregister the dcookie if adv oprofile is implemented as kernel module
right now it is not kernel module */
static void *aop_dcookie_user_data;

static int aop_stop_oprofile(void);

int aop_kdebug_init(void);

/* For initialization*/
static int do_init(void);

struct aop_register *kdebugd_aop_oprofile;

/* To wakeup buffer waiter from adv oprofile module */
static int aop_oprofile_wake_up_buffer(void)
{
	int retval = 1;
	if (1 /*aop_profile_running */) {
		/* wake up the daemon to read what remains */
		kdebugd_aop_oprofile->aop_wake_up_buffer_waiter();
		retval = 0;
	}
	return retval;
}

/* deallocate/free memory*/
static void aop_free_event_cache(void)
{
	/* allocat memory for given buffer size */
	if (aop_cache.buffer) {
		KDBG_MEM_DBG_KFREE(aop_cache.buffer);
		aop_cache.buffer = NULL;
	}

	/* initialize the wr/rd offset values */
	aop_cache.wr_offset = 0;
	aop_cache.rd_offset = 0;
}

/* allocate memory to copy event data */
static int aop_alloc_event_cache(void)
{
	/* allocat memory for given buffer size */
	aop_cache.buffer = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_SAMPLING_BUFFER_MODULE,
						aop_event_buffer_size,
						GFP_KERNEL);

	/* BUG ON when failed to allocate memory */
	WARN_ON(!aop_cache.buffer);
	if (!aop_cache.buffer) {
		/*return -ENOMEM;*/	/* no memory!! */
		aop_errk ("Failed memory alloc aop_alloc_event_cache %lu\n", aop_event_buffer_size);
		PRINT_KD("Allocating 1MB memory\n");
		aop_event_buffer_size = 1000*AOP_KB;
		/* allocat memory for given buffer size */
		aop_cache.buffer = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_SAMPLING_BUFFER_MODULE,
						aop_event_buffer_size,
						GFP_KERNEL)	;
		if (!aop_cache.buffer)
			return -ENOMEM;
	}

	/* initialize the wr/rd offset values */
	aop_cache.wr_offset = 0;
	aop_cache.rd_offset = 0;

	/* return success */
	return 0;
}

/* clear event cache buffer */
static void aop_clear_event_cache(void)
{
	/* clear cached event data */
	if (aop_cache.buffer)
		memset(aop_cache.buffer, 0, aop_event_buffer_size);

	/* initialize the wr/rd offset values */
	aop_cache.rd_offset = 0;
	aop_cache.wr_offset = 0;
}

/* Wakeup event buffer and cache the event data buffer.
 before read the data, must wakeup the event waiter
 and read data from event buffer */
static int aop_put_data(void)
{
	unsigned long remain_buff_size;
	/* check whether enough buffer is there at event cache buffer ot not */
	remain_buff_size = aop_event_buffer_size - aop_cache.wr_offset;
	if (!remain_buff_size) {
		aop_printk("Sec_op_cache FULL w =%d\n", aop_cache.wr_offset);
		PRINT_KD("[%s]Buffer Full:: Event Cache Full\n ", __FUNCTION__);
		return -1;
	}

	/* to wakeup event waiter */
	if (aop_oprofile_wake_up_buffer() == 0) {
		ssize_t retval = -1;
		unsigned long data_size;

		data_size =
		    kdebugd_aop_oprofile->aop_oprofile_buffer_size *
		    AOP_ULONG_SIZE;
		if (data_size > remain_buff_size)
			data_size = remain_buff_size;

		/* event cache buffer is enough to store event buffer then read data from event buff */
		retval =
		    kdebugd_aop_oprofile->aop_read_event_buffer(aop_cache.
								buffer +
								aop_cache.
								wr_offset /
								AOP_ULONG_SIZE,
								&data_size);
		aop_printk("Data read %lu\n", data_size);

		if (aop_profile_running == 0)
			aop_printk("wr_offset = %d :: retval = %d\n",
				   aop_cache.wr_offset, retval);
		if (aop_profile_running == 0 && retval == 0)
			return -1;

		/* update the cache wr offeset, when data read is succeed */
		if (retval > 0)
			aop_cache.wr_offset += retval;

	}
	if (aop_profile_running == 0)
		aop_printk("--------------->: Returning Zero\n");
	return 0;		/* upon success */
}

/* op buffer task function, which can wakeup event 1000 milliseconds and
verify oprofile is stopped or not, if not stopped read data from event buffer
and put it in op cache buffer */
static int aop_buff_task_sync(void *arg)
{
	int buff_full = 0;
	long sample_count = 0;
	unsigned long remain_buff_size;

	PRINT_KD("\n");

	/* AOP sampling In progress PC Count  */
	kdebugd_aop_oprofile->aop_reset_pc_sample_count();

	while (!kthread_should_stop()) {

		sample_count = kdebugd_aop_oprofile->aop_get_pc_sample_count();

		/* when oprofile is stopped don't process
		   aop_buff_tsk_created variable is also used
		   to keep track of task running status */
		if (!aop_buff_tsk_created)
			break;

		/* copy event_data to aop_cache */
		if (aop_put_data() == -1) {
			/* mark buffer full state */
			buff_full = 1;
			break;
		}
		/* check whether enough buffer is there at event cache buffer ot not */
		remain_buff_size = aop_event_buffer_size - aop_cache.wr_offset;

		if (remain_buff_size > AOP_KB) {
			/* extra space at end for clearning previous output of same line. */
			PRINT_KD
			    ("\t\t [buffer_size: %d KB]  [pc_sample: %ld]\n",
			     (aop_cache.wr_offset >> 10), sample_count);
		}

		/* profile sampling rate */
		msleep(AOP_CONFIG_DEFAULT_SAMPLING_MS);
	}

	aop_profile_running = 0;
	PRINT_KD("\n");

	/* when buffer is full stop the adv oprofile */
	if (buff_full) {
		PRINT_KD("Buffer Full = %d KB: Stopping profiling...\n",
			 (aop_cache.wr_offset >> 10));
		aop_stop_oprofile();
	} else {
		/* read any remaining buffer from event buffer */
		while (aop_put_data() != -1) {
			aop_printk("Finishing the sample collection...\n");
			/* extra space at end for clearning previous output of same line. */
			aop_printk
			    ("=======> [buffer_size: %d KB]  [pc_sample: %d]\n",
			     (aop_cache.wr_offset >> 10),
			     kdebugd_aop_oprofile->aop_get_pc_sample_count());
		}
	}

	/* set the oprofile running status */
	kdebugd_aop_oprofile->aop_reset_pc_sample_count();
	return 0;
}

/* aop buffer task create function, which is called when oprofile start is request from user.
when this function is already created and not running, then task running status is set,
the op buffer task read this task running status flag and cache the event data for
further report generation. if the task is not created, then create & run aop_tsk */
static int aop_buff_tsk_create(void)
{
	int ret = AOP_TSK_NOT_CREATED;
	/*struct task_struct *aop_buff_tsk; */

	/* if task is already created return error code */
	if (aop_buff_tsk_created) {
		/* task already created */
		return AOP_TSK_ALREADY_CREATED;
	}

	/* reset the aop buffer and task state */
	aop_clear_event_cache();

	/* set the tsk create flag */
	aop_buff_tsk_created = 1;

	/* create aop buffer task */
	aop_buff_tsk = kthread_create(aop_buff_task_sync, NULL, "aop_tsk");
	if (IS_ERR(aop_buff_tsk)) {
		/* ret = PTR_ERR(aop_buff_tsk); */
		aop_errk
		    ("---------- Aop Buff Tsk thread Creation Failed --------\n");
		aop_buff_tsk = NULL;
		aop_buff_tsk_created = 0;
		return ret;
	}

	/* update task flag and wakeup process */
	aop_buff_tsk->flags |= PF_NOFREEZE;
	wake_up_process(aop_buff_tsk);

	return ret;
}

/* stop the buff task, stop means, just clear the tsk running state */
static int aop_buff_tsk_stop(void)
{
	/* stop the buff task */
	if (aop_buff_tsk) {

		/* Stop the symbol resolution thread */
		if (current != aop_buff_tsk) {
			aop_printk("Other thread stopping AOP task\n");
			kthread_stop(aop_buff_tsk);
		} else
			aop_printk("AOP task stopping itself\n");

		/* cleanup would be done by thread itself by calling do_exit(); */
		aop_buff_tsk = NULL;

		/* set the task create flag to zero */
		aop_buff_tsk_created = 0;

		/* return success */
		return 0;
	} else {
		WARN_ON(aop_buff_tsk_created);
	}

	return 1;		/* return error */
}

/* Callgraph */
extern aop_caller_head_list *aop_caller_list_head;
#define KDBG_CALLGRAPH_DEPTH	2

/*
  * This function is to start oprofile and aop buff task
  * It is called from kdebug menu, when user choose start oprofile option
  */
static int aop_start_oprofile(void)
{
	int ret = 0;

	if (aop_buff_tsk_created) {
		aop_printk("Task %s!!!\n",
			   (ret ==
			    AOP_TSK_ALREADY_CREATED) ? "exists" :
			   "create failed");
		PRINT_KD("Adv OProfile already running!!!\n");
		return 1;
	}

	if (!aop_cache.buffer) {
		aop_errk ("aop_cache.buffer in NULL ... Please allocate\n");
		return -1;
	}

	/*reset all the resiurces taken for processing */
	aop_free_resources();
	aop_chk_resources();

	/* callpath mem release */
	aop_cp_free_mem();

	/* Clear the even buffer and set the writing position to zero.
	   This is not done in aop_oprofile_stop() */
	kdebugd_aop_oprofile->aop_event_buffer_clear();

	/* clear the cpu buffer after stop the oprofile,
	   as we don't shutdown the oprofile while stop, we must clear the cpu buffer */
	kdebugd_aop_oprofile->aop_clear_cpu_buffers();

	/* Callgraph */
	oprofile_set_backtrace(KDBG_CALLGRAPH_DEPTH);
	aop_caller_list_head = NULL;

	/* start oprofile */
	kdebugd_aop_oprofile->aop_oprofile_start();

	PRINT_KD("Adv OProfile running...\n");

	/* set the oprofile running status */
	aop_profile_running = 1;

	/* create aop buff task, if it is already created and stopped by oprofile,
	   then, the task running status will be set to wake the thread to cache event data */
	ret = aop_buff_tsk_create();
	if (ret != AOP_TSK_NOT_CREATED) {
		aop_printk("AOP Task %s!!!\n",
			   (ret ==
			    AOP_TSK_ALREADY_CREATED) ? "exists" :
			   "create failed");
		aop_errk("Adv OProfile task creation failed!!!\n");

		/* We already handled already created case above. */
		BUG_ON(ret == AOP_TSK_ALREADY_CREATED);

		aop_profile_running = 0;
		/* stop oprofile */
		kdebugd_aop_oprofile->aop_oprofile_stop();

		return 1;
	}

	/* as this return value is mean to show or not show the kdebugd menu options */
	return 0;
}

/* Function is to stop oprofile and stop the task which cache the event buffer
Once stop the called by kdebugd, oprofile is stopped and collected samples
are processed and ready for report */
static int aop_stop_oprofile(void)
{
	aop_printk("enter\n");
	/* stop aop buff task, it means set the task running status 0, to stop cache event data */
	if (aop_buff_tsk_stop()) {
		/* oprofile not started or already stopped so return */
		PRINT_KD("Adv OProfile stopped already!!!\n");
		return 1;
	}
	aop_printk("after stoppig thread\n");

	/* set the oprofile running status */
	aop_profile_running = 0;

	/* stop oprofile, which stop buffering the event data */
	kdebugd_aop_oprofile->aop_oprofile_stop();

	/* once, oprofile is stopped, all the collected samples are processed and stored
	   in seperate buffer as per filter option & oprofile configuration */
	aop_process_all_samples();
	PRINT_KD("Profiling Stopped!!!(Total Samples : %lu)\n",
		 aop_nr_total_samples);

	/* Loading ELF Database Automatically.... */
	aop_load_elf_db_for_all_samples();

	AOP_PRINT_TID_LIST(__FUNCTION__);
	AOP_PRINT_TGID_LIST(__FUNCTION__);

	return 0;
}

#if AOP_DEBUG_ON
static int aop_dump_all_raw_samples(void)
{
	/*Read from the zeroth entry to maximum filled. cache the write offset. */
	int tmp_buf_write_pos = aop_cache.wr_offset / AOP_ULONG_SIZE;
	int count = 0;
	PRINT_KD("\n%s: Dump raw data %d\n", __FUNCTION__, tmp_buf_write_pos);
	for (count = 0; count < aop_cache.wr_offset / AOP_ULONG_SIZE; count++)
		PRINT_KD("0x%04x 0x%08lx\n", count, aop_cache.buffer[count]);

	return 0;
}
#endif /* AOP_DEBUG_ON */

/* to show oprofile current state */
static int aop_profile_current_state(void)
{
	/* show user the current oprofile state */
	PRINT_KD("Profile State: ");
	if (aop_profile_running)
		PRINT_KD("Running\n");
	else
		PRINT_KD("Not Running\n");

	return 1;		/* to show kdebugd menu */
}

#if AOP_DEBUG_ON
/* Function is used to view the aop cache wr pointer and total size.
It is used for debugging purpose */
static int aop_print_current_wr_offer(void)
{
	PRINT_KD("aop cache status [%d/%ld]\n",
		 aop_cache.wr_offset, aop_event_buffer_size);
	return 0;
}
#endif /* AOP_DEBUG_ON */

/*
  * This function sets the size of buffer in which the raw data is collected.
  * By default, the size is 2*1024*1024 which is capable of storing data
  * for 30 Mins.
  */
static int aop_config_set_buffer_size(void)
{
	int buffer_size = 0;
	int cur_buff_size = aop_event_buffer_size / AOP_KB;

	PRINT_KD("\nCurrent Buffer size = %d KB\n", cur_buff_size);

	if (!aop_profile_running) {
		PRINT_KD("Default Buffer size of %d KB is sufficient for "
			 "collecting oprofile data of approx %d mins.\n",
			 cur_buff_size,
			 ((cur_buff_size * AOP_CONFIG_DEFAULT_SAMPLING_MS) /
			  (60 * 1000)));
		PRINT_KD("Enter Buffersize(in KB)-> ");
		buffer_size = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");
		buffer_size = buffer_size * AOP_KB;	/*Converting the value into Bytes */

		if (buffer_size == aop_event_buffer_size) {
			return 0;	/*if same as previous, then return */
		} else if (buffer_size <= 0) {
			PRINT_KD("Invalid value\n");
			return -1;	/*if it is zero and -ve, then return */
		} else if (buffer_size < AOP_CONFIG_MIN_EVENT_BUFF) {
			/*If it is less then the minimum value, then return */
			PRINT_KD("\nMinimum buffer size of  %dKB is required "
				 "for oprofiling of %d Secs. Please enter again...\n",
				 (AOP_CONFIG_MIN_EVENT_BUFF / AOP_KB),
				 ((AOP_CONFIG_MIN_EVENT_BUFF *
				   AOP_CONFIG_DEFAULT_SAMPLING_MS) / (AOP_KB *
								      1000)));
			return -1;
		} else if (buffer_size > AOP_CONFIG_MAX_EVENT_BUFF) {
			/*If it is more then the maximum value, then return */
			PRINT_KD
			    ("\nBuffer size exceeds the maximum buffer size of %dKB "
			     "for oprofiling of %d Mins Please enter again...\n",
			     (AOP_CONFIG_MAX_EVENT_BUFF / AOP_KB),
			     (((AOP_CONFIG_MAX_EVENT_BUFF / (AOP_KB)) *
			       (AOP_CONFIG_DEFAULT_SAMPLING_MS)) / (1000 *
								    60)));
			return -1;
		} else {
			aop_event_buffer_size = buffer_size;
			aop_free_event_cache();
			/* allocate memory for event cache */
			if (aop_alloc_event_cache() == -ENOMEM) {
				aop_errk
				    ("Failed memory alloc aop_alloc_event_cache\n");
				return 1;
			}
		}
		PRINT_KD("\nbuffer size is %lu\n", aop_event_buffer_size);
	} else {
		PRINT_KD("\nStop OProfile before setting the buffer size.\n");
	}

	/* as this return value is mean to show or not show the kdebugd
	   menu options */
	return 0;
}

/* This function is used to show oprofile configuration */
static void aop_config_show_configuration(void)
{
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
	int ret;
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
	PRINT_KD("\nAdv OProfile Current Settings\n");
	PRINT_KD("--------------------------------------------------\n");
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
	PRINT_KD("Samples source = ");

	ret = aop_get_sampling_mode();

	if (ret == INVALID_SAMPLING_MODE) {
		PRINT_KD("INVALID SAMPLING MODE!!!\n");
		return;
	}

	if (ret == TIMER_SAMPLING) {
		PRINT_KD("timer\n");
		goto next;
	}

	switch (aop_get_counter_config(0)) {
	case PERF_COUNT_HW_CPU_CYCLES:
		PRINT_KD("CPU cycles (%d events per sample)",
			CONFIG_AOP_CYCLES_SAMPLING_PERIOD);
		break;
	case PERF_COUNT_HW_INSTRUCTIONS:
		PRINT_KD("Instructions Counter (%d events per sample)",
			CONFIG_AOP_INSTRUCTIONS_SAMPLING_PERIOD);
		break;
	case PERF_COUNT_HW_CACHE_L1D  | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
		PRINT_KD("L1D cache misses (%d events per sample)",
			CONFIG_AOP_L1D_MISSES_SAMPLING_PERIOD);
		break;
	case PERF_COUNT_HW_CACHE_L1I  | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
		PRINT_KD("L1I cache misses (%d events per sample)",
			CONFIG_AOP_L1I_MISSES_SAMPLING_PERIOD);
		break;
	case PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
		PRINT_KD("DTLB misses (%d events per sample)",
			CONFIG_AOP_DTLB_MISSES_SAMPLING_PERIOD);
		break;
	case PERF_COUNT_HW_CACHE_ITLB | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
		PRINT_KD("ITLB misses (%d events per sample)",
			CONFIG_AOP_ITLB_MISSES_SAMPLING_PERIOD);
		break;
	case PERF_COUNT_HW_CACHE_BPU  | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
		PRINT_KD("BPU errors (%d events per sample)",
			CONFIG_AOP_BPU_ERRORS_SAMPLING_PERIOD);
	}
	if (aop_get_counter_enabled(0))
		PRINT_KD(", enabled\n");
	else
		PRINT_KD(", disabled\n");

next:
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
	PRINT_KD("Buffer size = %lu KB\n", aop_event_buffer_size / AOP_KB);
	PRINT_KD("Sort option = %s\n",
		 aop_config_sort_string[aop_config_sort_option]);
	PRINT_KD("Symbol name demangle = %s\n",
		 aop_config_demangle ? "Yes" : "No");
}

/* This is oprofile configuration menu, user can setup, buffer size, sampling rate
report sort options etc., */
static void aop_config_menu(void)
{
	int operation = 0;

	PRINT_KD("1)  aop config: set buffer size (%dKBytes by default).\n",
		 AOP_CONFIG_DEFAULT_EVENT_BUFF / AOP_KB);
	PRINT_KD("2)  aop config: set symbol report sort option.\n");
	PRINT_KD("3)  aop config: set demangle symbol option.\n");

	PRINT_KD("\nSelect Option==>  ");

	operation = debugd_get_event_as_numeric(NULL, NULL);
	/* No need to put  in the contineution of previouse PRINT_KD */
	PRINT_KD("\n");

	switch (operation) {
	case AOP_CONFIG_BUFF_SIZE:
		/* to setup oprofile buffer size */
		aop_config_set_buffer_size();
		break;
	case AOP_CONFIG_SORT_OPTION:
		/* to setup sort key */
		PRINT_KD("Symbol Report Sort Option:\n");
		PRINT_KD("---------------------------------\n");
		PRINT_KD("\tIndex  Sort by\n");
		PRINT_KD("---------------------------------\n");
		PRINT_KD("\t[0] - %s\n", aop_config_sort_string[0]);
		PRINT_KD("\t[1] - %s\n", aop_config_sort_string[1]);
		PRINT_KD("\t[2] - %s\n", aop_config_sort_string[2]);
		PRINT_KD("---------------------------------\n");
		PRINT_KD("Sort Option ==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		if (operation < 0 || operation > 2)
			PRINT_KD("Invalid sort option\n");
		else
			aop_config_sort_option = operation;
		break;
	case AOP_CONFIG_DEMANGLE_SYM_NAME:
		/* to update symbol demangle option */
		PRINT_KD("Symbol Name demangle option: [0(No)/1(Yes)] ==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		if (operation > 1)
			PRINT_KD("Invalid demangle option\n");
		else
			aop_config_demangle = operation;
		break;
	default:
		PRINT_KD("\nInvalid Option....\n  ");
		break;
	}
	PRINT_KD("Adv OProfile configuration menu exit....\n");
}

/* This is main oprofile main menu, and it is catagorized into three
1. setup, 2. report & 3. USB & ELF parser
It just call appropriate api's for the option listed in this function.  */
static int aop_oprofile_menu(void)
{
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
	debugd_event_t event;
	int err;
	int mode;
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
	int operation = 0;
	int ret = 1;

	/* Oprofile initialization  */
	if (do_init()) {
		aop_errk("Initialization Failed\n");
		return -1;
	}

	do {
		if (ret) {
			PRINT_KD("\n");
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
			PRINT_KD("-------------------------------------------"
					"-------------------------\n");
			PRINT_KD("Current Sampling Interrupt Source \"");

			mode = aop_get_sampling_mode();
			if (mode == INVALID_SAMPLING_MODE) {
				PRINT_KD("INVALID!!\n");
				PRINT_KD("Can't Continue AOP!!!\n");
				return -1;
			}

			if (mode == TIMER_SAMPLING) {
				PRINT_KD("Timer\" (%d ms per sample)\n",
					1000 / HZ);
				goto next;
			}

			switch (aop_get_counter_config(0)) {
			case PERF_COUNT_HW_CPU_CYCLES:
				PRINT_KD("CPU cycles\" (%d events/sample)\n",
					CONFIG_AOP_CYCLES_SAMPLING_PERIOD);
				break;
			case PERF_COUNT_HW_INSTRUCTIONS:
				PRINT_KD("Instructions Counter\" "
						"(%d events/sample)\n",
				       CONFIG_AOP_INSTRUCTIONS_SAMPLING_PERIOD);
				break;
			case PERF_COUNT_HW_CACHE_L1D |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
				PRINT_KD("L1D cache misses\" "
						"(%d events per sample)\n",
					CONFIG_AOP_L1D_MISSES_SAMPLING_PERIOD);
				break;
			case PERF_COUNT_HW_CACHE_L1I |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
				PRINT_KD("L1I cache misses\" "
						"(%d events per sample)\n",
					CONFIG_AOP_L1I_MISSES_SAMPLING_PERIOD);
				break;
			case PERF_COUNT_HW_CACHE_DTLB |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
				PRINT_KD("DTLB misses\" "
						"(%d events per sample)\n",
					CONFIG_AOP_DTLB_MISSES_SAMPLING_PERIOD);
				break;
			case PERF_COUNT_HW_CACHE_ITLB |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
				PRINT_KD("ITLB misses\" "
						"(%d events per sample)\n",
					CONFIG_AOP_ITLB_MISSES_SAMPLING_PERIOD);
				break;
			case PERF_COUNT_HW_CACHE_BPU |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16):
				PRINT_KD("BPU errors\" "
						"(%d events per sample)\n",
					CONFIG_AOP_BPU_ERRORS_SAMPLING_PERIOD);
			}
next:
			PRINT_KD("-------------------------------------------"
					"-------------------------\n");
			PRINT_KD("a)  Sampling Source : Timer\n");
			PRINT_KD("b)  Sampling Source : "
						"CPU cycles counter\n");
			PRINT_KD("c)  Sampling Source : "
						"Instructions counter\n");
			PRINT_KD("d)  Sampling Source : "
						"L1 Data cache miss counter\n");
			PRINT_KD("e)  Sampling Source : "
					"L1 Instruction cache miss counter\n");
			PRINT_KD("f)  Sampling Source : "
						"Data TLB miss counter\n");
			PRINT_KD("g)  Sampling Source : "
					"Instruction TLB miss counter\n");
			PRINT_KD("h)  Sampling Source : "
				"BPU miss counter (Branch Prediction Unit)\n");
			PRINT_KD("-------------------------------------------"
					"-------------------------\n");
#endif /*CONFIG_CACHE_ANALYZER*/
#endif /*MP_DEBUG_TOOL_OPROFILE*/
			PRINT_KD("1)  Adv OProfile: setup configuration.\n");
			PRINT_KD("2)  Adv OProfile: show configuration.\n");
			PRINT_KD("3)  Adv OProfile: start oprofile.\n");
			PRINT_KD("4)  Adv OProfile: stop oprofile.\n");
			PRINT_KD
			    ("5)  Adv OProfile: show current profile state.\n");
			PRINT_KD
			    ("------------------------------------------------"
			     "--------------------\n");
			PRINT_KD("6)  Adv OProfile: generate report "
				 "for application Samples.\n");
			PRINT_KD
			    ("7)  Adv OProfile: generate report for library Samples.\n");
			PRINT_KD
			    ("8)  Adv OProfile: generate report for kernel Samples.\n");
			PRINT_KD
			    ("9)  Adv OProfile: generate report process ID wise.\n");
			PRINT_KD
			    ("10) Adv OProfile: generate report thread ID wise.\n");
			PRINT_KD
			    ("11) Adv OProfile: generate report for all Samples.\n");
			PRINT_KD("12) Adv OProfile: generate report "
				 "for system-wide symbol name.\n");
			PRINT_KD("13) Adv OProfile: generate report for "
				 "single application symbol.\n");
			PRINT_KD
			    ("14) Adv OProfile: generate report for all symbol offset\n");
#ifdef CONFIG_SMP
			PRINT_KD
			    ("15) Adv OProfile: generate report for CPU wise\n");
#endif
			PRINT_KD("16) Adv OProfile: Dump all samples Callgraph.\n");
#if AOP_DEBUG_ON
			PRINT_KD("17) Adv OProfile: Dump all raw samples.\n");
#endif /* AOP_DEBUG_ON */
			PRINT_KD
			    ("------------------------------------------------"
			     "--------------------\n");
			PRINT_KD("99) Adv OProfile: Exit Menu\n");
			PRINT_KD("\n");
			PRINT_KD("Select Option==>  ");
		}

#if defined(CONFIG_CACHE_ANALYZER) && (MP_DEBUG_TOOL_OPROFILE == 1)
		operation = debugd_get_event_as_numeric(&event, NULL);
#else
		operation = debugd_get_event_as_numeric(NULL, NULL);
#endif
		/* No need to put  in the contineution of previouse PRINT_KD */
		PRINT_KD("\n");

		if (operation >= AOP_MENU_REPORT_APPL_SAMPLE &&
		    operation <= AOP_MENU_REPORT_SYMBOL_ALL &&
		    aop_profile_running) {
			PRINT_KD("Adv OProfile is running....\n"
				 "Stop adv OProfile before view report\n");
			continue;
		}

		switch (operation) {
		case AOP_MENU_CHANGE_CONFIG:
			/* to open/setup oprofile configuration */
			if (!aop_profile_running) {
				PRINT_KD("Setup configuration.\n");
				aop_config_menu();
				ret = 1;	/* must show the oprofile menu */
			} else
				PRINT_KD
				    ("Stop Adv OProfile before change configuration\n");
			break;
		case AOP_MENU_SHOW_CONFIG:
			/* to show oprofile configuration */
			aop_config_show_configuration();
#if defined(CONFIG_CACHE_ANALYZER) && (MP_DEBUG_TOOL_OPROFILE == 1)
			ret = 1;
#else
			ret = 0;
#endif
			break;
		case AOP_MENU_START_PROFILE:
			/* to start oprofile and aop buff task  */
			ret = aop_start_oprofile();
			break;
		case AOP_MENU_STOP_PROFILE:
			/* to stop oprofile and aop buff task  */
			aop_stop_oprofile();
			ret = 1;
			break;
		case AOP_MENU_SHOW_CUR_STATE:
			/* to show oprofile running state */
			ret = aop_profile_current_state();
			break;
		case AOP_MENU_REPORT_APPL_SAMPLE:
			/* generate report for application Samples */
			PRINT_KD("Report Application Samples Only:\n");
			aop_op_generate_app_samples();
			ret = 1;	/* must show the oprofile menu */
			break;
		case AOP_MENU_REPORT_LIB_SAMPLE:
			/* generate report for library Samples */
			PRINT_KD("Report Library Samples Only:\n");
			aop_op_generate_lib_samples();
			ret = 1;	/* must show the oprofile menu */
			break;
		case AOP_MENU_REPORT_KERNEL_SAMPLE:
			/* generate report for kernel Samples */
			PRINT_KD
			    ("Report Kernel & Kernel Module Samples Only:\n");
			ret = aop_generate_kernel_samples();
			break;
		case AOP_MENU_REPORT_TGID:
			/* generate report --- ProcessID wise */
			PRINT_KD("Report Process ID Wise Samples Only:\n");
			ret = aop_op_generate_report_tgid();
			break;
		case AOP_MENU_REPORT_TID:
			/* generate report --- ThreadID wise */
			PRINT_KD("Report Thread ID Wise Samples Only:\n");
			ret =
			    aop_op_generate_report_tid(AOP_REPORT_TID_CPU_ALL);
			break;
		case AOP_MENU_REPORT_ALL_SAMPLE:
			/* generate report for all Samples */
			PRINT_KD("Report All Samples:\n");
			ret = aop_op_generate_all_samples();
			break;
		case AOP_MENU_REPORT_SYMBOL_SYSTEM_WIDE:
			/*Dump the system wide symbol info including application & library name */
			PRINT_KD("Report System wide function name:\n");
			aop_sym_report_system_wide_function_samples();
			break;
		case AOP_MENU_REPORT_SYMBOL_APPL_LIB_SAMPLE:
			/* Dump the application wise function name including library name */
			PRINT_KD
			    ("Report Application Wise Samples Including Library:\n");
			ret = aop_sym_report_per_application_n_lib();
			break;
		case AOP_MENU_REPORT_SYMBOL_ALL:
			PRINT_KD("Report all symbols (Test Function):\n");
			aop_sym_report_system_wide_samples();
			break;
#ifdef CONFIG_SMP
		case AOP_MENU_REPORT_CPU_WISE:
			PRINT_KD
			    ("Report Thread ID Wise Samples for each CPU:\n");
			ret =
			    aop_op_generate_report_tid(AOP_REPORT_TID_CPU_WISE);
			break;
#endif
		case AOP_DUMP_ALL_SAMPLES_CALLGRAPH:
			ret = aop_cp_show_menu();
			break;
#if AOP_DEBUG_ON
		case AOP_DUMP_ALL_RAW_SAMPLES:
			aop_dump_all_samples_callgraph();
			aop_dump_all_raw_samples();
			break;
#endif /* AOP_DEBUG_ON */

		case 99:
			/* AOP Menu Exit */

			break;
		default:
#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
				/*
				  Check if the sampling
					selection command was entered */
				if (event.input_string[0] == 'a' &&
						event.input_string[1] == 0) {
					err = aop_switch_to_timer_sampling();
					if (err) {
						PRINT_KD("ERR: Can't Continue AOP!!\n");
						return -1;
					}

					break;
				} else if (event.input_string[0] == 'b' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_CYCLES_SAMPLING_PERIOD,
						PERF_COUNT_HW_CPU_CYCLES,
						PERF_TYPE_HARDWARE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				} else if (event.input_string[0] == 'c' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_INSTRUCTIONS_SAMPLING_PERIOD,
						PERF_COUNT_HW_INSTRUCTIONS,
						PERF_TYPE_HARDWARE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				} else if (event.input_string[0] == 'd' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_L1D_MISSES_SAMPLING_PERIOD,
						PERF_COUNT_HW_CACHE_L1D |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
						PERF_TYPE_HW_CACHE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				} else if (event.input_string[0] == 'e' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_L1I_MISSES_SAMPLING_PERIOD,
						PERF_COUNT_HW_CACHE_L1I  |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
						PERF_TYPE_HW_CACHE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				} else if (event.input_string[0] == 'f' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_DTLB_MISSES_SAMPLING_PERIOD,
						PERF_COUNT_HW_CACHE_DTLB |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
						PERF_TYPE_HW_CACHE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				} else if (event.input_string[0] == 'g' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_ITLB_MISSES_SAMPLING_PERIOD,
						PERF_COUNT_HW_CACHE_ITLB |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
						PERF_TYPE_HW_CACHE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				} else if (event.input_string[0] == 'h' &&
						event.input_string[1] == 0) {
					err = aop_setup_counter(0,
					CONFIG_AOP_BPU_ERRORS_SAMPLING_PERIOD,
						PERF_COUNT_HW_CACHE_BPU |
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
						PERF_TYPE_HW_CACHE, 1);
					if (err) {
						PRINT_KD("Switching to Timer Mode..\n");
						if (aop_switch_to_timer_sampling() != 0) {
							PRINT_KD("ERR: Can't Continue AOP!!\n");
							return -1;
						}
					}
					break;
				}
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/
			PRINT_KD("Adv OProfile invalid option....\n");
			ret = 1;	/* to show menu */
			break;
		}
	} while (operation != 99);
	PRINT_KD("Adv OProfile menu exit....\n");

	/* as this return value is mean to show or not show the kdebugd menu options */
	return ret;
}

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_AOP_AUTO_START

/* The feature is for auto start cpu usage for taking the log
 * of the secified time */
static struct hrtimer aop_auto_timer;

/* Auto start loging the cpu usage data */
static enum hrtimer_restart aop_auto_start(struct hrtimer *ht)
{
	static int started;
	enum hrtimer_restart h_ret = HRTIMER_NORESTART;
	struct kdbg_work_t aop_start_work;
	struct timespec tv = {.tv_sec = CONFIG_AOP_DURATION, .tv_nsec = 0};

	/* Oprofile initialization  */

	BUG_ON(started != 0 && started != 1);

	/* Make the status running */
	if (!started) {

#if AOP_DEBUG_ON
		PRINT_KD("Posting the work On workQ\n");
#endif
		/*Post AOP start work */
		aop_start_work.data = (void *)NULL;
		aop_start_work.pwork = (void *)aop_start_oprofile;

		kdbg_workq_add_event(aop_start_work, NULL);
#if AOP_DEBUG_ON
		PRINT_KD("Posted on workQ\n");
#endif
		/* restart timer at finished seconds. */
		hrtimer_forward(&aop_auto_timer, ktime_get(),
				timespec_to_ktime(tv));
		started = 1;
		h_ret = HRTIMER_RESTART;

	} else {

		aop_start_work.data = (void *)NULL;
		aop_start_work.pwork = (void *)aop_stop_oprofile;
		kdbg_workq_add_event(aop_start_work, NULL);
		/* restart timer at finished seconds. */

		h_ret = HRTIMER_NORESTART;
	}
	return h_ret;
}

/* Wrapper function for launching AOP auto start */
static void aop_auto_start_wr(void *data)
{
	struct timespec tv = {.tv_sec = CONFIG_AOP_START_SEC, .tv_nsec = 0};
	/* First check for successful initialization */
	if (do_init()) {

		aop_errk("Initialization Failed\n");
		return;
	}

	/* Launch auto start now */
	hrtimer_init(&aop_auto_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aop_auto_timer._softexpires = timespec_to_ktime(tv);
	aop_auto_timer.function = aop_auto_start;
	hrtimer_start(&aop_auto_timer, aop_auto_timer._softexpires,
		      HRTIMER_MODE_REL);

}
#endif /* CONFIG_AOP_AUTO_START */
#endif /*MP_DEBUG_TOOL_OPROFILE*/

/* Allocate advance oprofile memory */
struct aop_register *aop_alloc(void)
{

	kdebugd_aop_oprofile =
	    (struct aop_register *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
							sizeof
							(*kdebugd_aop_oprofile),
							GFP_KERNEL);

	if (!kdebugd_aop_oprofile)
		aop_errk("[%s]: Memory Allocation Failed\n", __FUNCTION__);

	return kdebugd_aop_oprofile;
}

EXPORT_SYMBOL(aop_alloc);

/* Dellaocate advance oprofile memory */
void aop_dealloc(void)
{
	if (kdebugd_aop_oprofile)
		KDBG_MEM_DBG_KFREE(kdebugd_aop_oprofile);

	kdebugd_aop_oprofile = NULL;
}

EXPORT_SYMBOL(aop_dealloc);

static int do_init(void)
{
	static int aop_init_flag;
	int err_cnt = 0;

	/* One Time Initilaization */
	/* Check init flag */
	if (!aop_init_flag) {
		while (!kdebugd_aop_oprofile) {
			/* Print msg in each second */
			if (!(++err_cnt % 5))
				aop_errk
				    ("Oprofile driver not initialized yet\n");

			/* wait for 10 second */
			if (err_cnt == 50) {
				aop_errk
				    ("ERR: It Seems Oprofile Module Not Inserted\n");
				PRINT_KD
				    ("*********** WARNING *****************\n");
				PRINT_KD
				    ("****** Oprofile Module is not inserted *****\n");
				PRINT_KD
				    ("*********** WARNING *****************\n");
				return -1;
			}

			/*Sleep for 200 ms and check again */
			msleep(200);
		}
		/* BUG !!! if not allocated */
		BUG_ON(!kdebugd_aop_oprofile);
		/*Init function */
		if (aop_kdebug_init() <= 0) {
			aop_errk("Oprofile Init Failed.\n");
			return -1;;
		}
		/* Initialized Successfully */
		aop_init_flag = 1;
		PRINT_KD("Oprofile Init done.\n");
	} else {
		PRINT_KD("Already Initialized.\n");
	}

	return 0;
}

void aop_kdebug_start(void)
{

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_AOP_AUTO_START
	struct kdbg_work_t aop_start_work;
	aop_start_work.data = NULL;
	aop_start_work.pwork = aop_auto_start_wr;
	kdbg_workq_add_event(aop_start_work, NULL);
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/

	/* adv oprofile menu options */
	kdbg_register("PROFILE: Advanced OProfile",
		      aop_oprofile_menu, NULL, KDBG_MENU_AOP);

#if AOP_DEBUG_ON
	kdbg_register("PROFILE: wr offset",
		      aop_print_current_wr_offer, NULL,
		      KDBG_MENU_AOP_WR_OFFSET);
#endif

}

/*
  * oprofile init function, which initialize advance oprofile stop and start functions
  * and allocate event cache memory and open the event buffer to allow
  * sample data to be stored in event buffer & cpu buffer.
  */
int aop_kdebug_init(void)
{
	/* Driver Must be initilialize before */
	BUG_ON(!kdebugd_aop_oprofile->aop_is_oprofile_init());

	/* allocate memory for event cache */
	if (aop_alloc_event_cache() == -ENOMEM) {
		aop_errk("Failed memory alloc aop_alloc_event_cache\n");
		return -1;
	}
#ifdef CONFIG_ELF_MODULE
	/* elf symbol load/unload notification callback function */
	kdbg_elf_sym_register_oprofile_elf_load_notification_func
	    (aop_sym_elf_load_notification_callback);
#endif /* CONFIG_ELF_MODULE */

	/* open event/cpu buffer to collect samples and register dcookie link */
	kdebugd_aop_oprofile->aop_event_buffer_open(&aop_dcookie_user_data);

#if (MP_DEBUG_TOOL_OPROFILE == 1)
#ifdef CONFIG_CACHE_ANALYZER
	if (aop_switch_to_timer_sampling())
		return -1;
#endif
#endif /*MP_DEBUG_TOOL_OPROFILE*/

	return 1;
}

/* release the oprofile module by removing the oprofile module */
void aop_release(void)
{
	/* set the oprofile running status */
	if (aop_profile_running)
		aop_stop_oprofile();

	aop_free_event_cache();

#ifdef CONFIG_ELF_MODULE
	kdbg_elf_sym_delete();
#endif /* CONFIG_ELF_MODULE */

	/*reset all the resiurces taken for processing */
	aop_free_resources();

	/* callpath mem release */
	aop_cp_free_mem();

	if (aop_dcookie_user_data) {
		PRINT_KD("Dcoockies user data Address %p\n",
			 aop_dcookie_user_data);
		kdebugd_aop_oprofile->
		    aop_dcookie_release(aop_dcookie_user_data);
	} else {
		PRINT_KD("Dcoockies user data Address is NULL\n");
	}
}

EXPORT_SYMBOL(aop_release);

#ifdef KDBG_MEM_DBG
/* shutdown the aop for internal memory checking */
void aop_shutdown(void)
{
	aop_release();
}
#endif
