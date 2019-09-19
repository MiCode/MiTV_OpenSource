

#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/oprofile.h>
/* Callgraph */
#include <linux/sort.h>
#include <linux/dcache.h>
#include <linux/sort.h>
/* Callgraph */
#include <kdebugd.h>
#include "aop_report.h"
#include "aop_oprofile.h"
#include "aop_kernel.h"
#include "../drivers/oprofile/event_buffer.h"
#include "aop_report_symbol.h"

#ifdef	CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#include "aop_debug.h"

/* lock for protecting aop_kernel_list_head */
static DEFINE_SPINLOCK(aop_kernel_list_lock);

/*for chelsea architecture and linux kernel 2.6.18*/
size_t kernel_pointer_size = 4;

/*position from which the value is to be read from the raw data buffer*/
static unsigned long aop_read_buf_pos;

/*samples which are remaining to get processed. Its value is cached with
write position of the buffer at the time of initialization of raw data buffer*/
static int aop_samples_remaining;

/*
 * Transient values used for parsing the raw data buffer aop_cache.
The relevant values are updated when each buffer entry is processed.
 */
static struct op_data aop_trans_data;

/*The samples are processed and the data specific to application which
are run are extracted and collected in this structure. aop_app_list_head is
the head of node of the linked list*/
aop_image_list *aop_app_list_head;

/*The samples are processed and the data specific to libraries which
are referred are extracted and collected in this structure.  aop_lib_list_head is the
head of node of the linked list*/
static aop_image_list *aop_lib_list_head;

/*The samples are processed and the data specific to tgid which
are run are extracted and collected in this structure. aop_tgid_list_head is
the head of node of the linked list*/
static aop_pid_list *aop_tgid_list_head;

/*The samples are processed and the data specific to pid which
are referred are extracted and collected in this structure.  aop_tid_list_head is the
head of node of the linked list*/
static aop_pid_list *aop_tid_list_head;

/*count of the total user samples collected on
processing the buffer aop_cache*/
static unsigned long aop_nr_user_samples;

/*total samples = user+kernel*/
unsigned long aop_nr_total_samples;

/*Thread name for the pid which is not registered in the kernel in task_struct*/
static const char *AOP_IDLE_TH_NAME = "Idle Task";

/*The samples are processed and the data specific to pid which
are referred are extracted and collected in this structure.  aop_dead_list_head is the
head of node of the linked list at the time of mortuary*/
aop_dead_list *aop_dead_list_head;

/*enum for denoting the type of image to be
decoded nd get the symbol information.*/
enum {
	IMAGE_TYPE_APPLICATION,
	IMAGE_TYPE_LIBRARY
};

/* Callgraph */
/* total no of  sample encountered during sampling*/
static int aop_total_report_samples;

/* Callgraph trace level */
static int g_aop_trace_flag;

static int g_aop_trace_depth;

/*count of the total kernel samples collected on
  processing the buffer aop_cache*/
extern unsigned long aop_nr_kernel_samples;

/* aop report sort option, by default it is none.
   It may be changed using setup configuration option */
extern int aop_config_sort_option;

/*The samples are processed and the data specific to caller which
  are run are extracted and collected in this structure. aop_caller_list_head is
  the head of node of the linked list*/
aop_caller_head_list *aop_caller_list_head;

/* apps callpath list head */
static aop_caller_head_list **aop_app_caller_list_head;

/*The samples are processed and the data specific to caller which
  are run are extracted and collected in this structure. aop_caller_kernel_list_head is
  kernel the head of node of the linked list*/
aop_caller_head_list *aop_caller_kernel_list_head;

static aop_caller_list  g_aop_cp_chain[10];

int aop_cp_item_idx;

/* enum for kdebugd menu num */
typedef enum {
	AOP_CP_SORT_APP_WISE = 1,
	AOP_CP_SORT_THREAD_WISE,
	AOP_CP_SORT_SAMPLE_WISE,
	AOP_CP_SORT_START_ADDR_WISE,
	AOP_CP_SORT_MAX
} AOP_CP_SORT_OPTION;

static char *aop_decode_cookie(aop_cookie_t cookie, char *buf, size_t buf_size);

/* adding values related to callgraph in callgraph list */
static void aop_cp_add_node(void)
{
	BUG_ON(g_aop_trace_flag < 0);
	if (g_aop_trace_depth >= 0 &&  g_aop_trace_depth < sizeof(g_aop_cp_chain)/sizeof(*g_aop_cp_chain)) {
		g_aop_cp_chain[g_aop_trace_depth].app_cookie = aop_trans_data.app_cookie;
		g_aop_cp_chain[g_aop_trace_depth].cookie = aop_trans_data.cookie;
		g_aop_cp_chain[g_aop_trace_depth].pc = aop_trans_data.pc;
		g_aop_cp_chain[g_aop_trace_depth].in_kernel = aop_trans_data.in_kernel;
		g_aop_cp_chain[g_aop_trace_depth].tid = aop_trans_data.tid;
		g_aop_cp_chain[g_aop_trace_depth].tgid = aop_trans_data.tgid;
		g_aop_trace_depth++;
	}
}

static void aop_cp_free_list(aop_caller_head_list *list_head)
{
	aop_caller_head_list *itr_head = NULL;
	aop_caller_head_list *new_itr_head = NULL;
	aop_caller_list *ptr = NULL;
	aop_caller_list *new_ptr = NULL;

	if (list_head) {
		itr_head = list_head;
		aop_printk("\nGoing to Delete CP List");
		while (itr_head != NULL) {
			ptr = itr_head->sample;
			for (; ptr;) {
				aop_printk("\nDeleting Samples of CP List");
				new_ptr = ptr->caller;
				KDBG_MEM_DBG_KFREE(ptr);
				ptr = new_ptr;
			}

			aop_printk("\nDeleting CP List");
			new_itr_head = itr_head->next;
			KDBG_MEM_DBG_KFREE(itr_head);
			itr_head = new_itr_head;
		}
	}
	return;
}

static aop_caller_head_list *aop_cp_copy_list(aop_caller_head_list *list)
{
	aop_caller_head_list *itr_head = NULL;
	aop_caller_head_list *tmp_head = NULL;
	aop_caller_head_list *prev_head = NULL;
	aop_caller_head_list *list_copy = NULL;
	aop_caller_list *ptr = NULL;
	aop_caller_list  *calleritem = NULL;
	aop_caller_list *prev_calleritem = NULL;

	itr_head = list;
	while (itr_head != NULL) {
		tmp_head = (aop_caller_head_list *)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_head_list), GFP_KERNEL);
		if (!tmp_head) {
			aop_errk("aop_caller_list_head: tmp_head: no memory\n");
			if (list_copy) {
				aop_printk("\n Deleting list_head...");
				aop_cp_free_list(list_copy);
			}
			return NULL; /* no memory!! */
		}

		memset(tmp_head, 0, sizeof(aop_caller_head_list));
		tmp_head->cnt = itr_head->cnt;

		for (ptr = itr_head->sample; ptr; ptr = ptr->caller) {
			aop_printk("\nAllocating Caller item\n");
			calleritem = (aop_caller_list *)KDBG_MEM_DBG_KMALLOC
				(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_list), GFP_KERNEL);
			if (!calleritem) {
				aop_errk("calleritem: no memory\n");
				if (tmp_head)
					KDBG_MEM_DBG_KFREE(tmp_head);
				return list_copy; /* no memory!! */
			}
			memset(calleritem, 0, sizeof(aop_caller_list));

			calleritem->app_cookie = ptr->app_cookie;
			calleritem->cookie = ptr->cookie;
			calleritem->pc = ptr->pc;
			calleritem->in_kernel = ptr->in_kernel;
			calleritem->self_sample_cnt = ptr->self_sample_cnt;
			calleritem->total_sample_cnt = ptr->total_sample_cnt;
			calleritem->tgid = ptr->tgid;
			calleritem->tid = ptr->tid;
			calleritem->start_addr = ptr->start_addr;
			calleritem->caller = NULL;
			if (prev_calleritem)
				prev_calleritem->caller = calleritem;
			else {
				aop_printk("Allocating Caller item %x\n", tmp_head);
				tmp_head->sample = calleritem;
			}

			prev_calleritem = calleritem;
		}

		if (list_copy == NULL && prev_head == NULL) {
			aop_printk("Initialing Caller List Head %x\n", tmp_head);
			list_copy = tmp_head;
		} else {
			BUG_ON(!prev_head);
			aop_printk("growing Caller List Head %x\n", tmp_head);
			prev_head->next = tmp_head;
		}
		prev_calleritem = NULL;
		prev_head = tmp_head;
		itr_head = itr_head->next;
	}

	return list_copy;
}

/* Finds length of list, which is usefull in selecting a random pivot */
static int aop_cp_ListLength(aop_caller_head_list *list)
{
	aop_caller_head_list *temp = list;
	int i = 0;
	while (temp != NULL) {
		i++;
		temp = temp->next;
	}
	return i;
}

/* Sort callpath list enteires */
static aop_caller_head_list *aop_cp_quicksort(aop_caller_head_list *list, int sort_option)
{
	aop_caller_head_list *less = NULL, *more = NULL, *next  = NULL, *end = NULL, *temp = NULL;

	/* Select a random pivot point  */
	aop_caller_head_list *pivot = list;


	/* Return NULL list  */
	if (aop_cp_ListLength(list) <= 1)
		return list;

	temp = list->next;

	/* Divide & Conq  */
	while (temp != NULL) {
		next = temp->next;

		if (sort_option == AOP_CP_SORT_APP_WISE) {
			if (temp->sample->tgid < pivot->sample->tgid) { /* TGID wise */
				temp->next = less;
				less = temp;
			} else {
				temp->next = more;
				more = temp;
			}
		} else if (sort_option == AOP_CP_SORT_SAMPLE_WISE) {
			if (temp->cnt > pivot->cnt) {  /* Sample wise */
				temp->next = less;
				less = temp;
			} else {
				temp->next = more;
				more = temp;
			}
		} else if (sort_option == AOP_CP_SORT_THREAD_WISE) {
			if (temp->sample->tid > pivot->sample->tid) {  /* TID wise */
				temp->next = less;
				less = temp;
			} else {
				temp->next = more;
				more = temp;
			}
		} else if (sort_option == AOP_CP_SORT_START_ADDR_WISE) {
			if (temp->sample->start_addr < pivot->sample->start_addr) {  /* Start addr wise */
				temp->next = less;
				less = temp;
			} else {
				temp->next = more;
				more = temp;
			}
		}

		temp = next;
	}

	/* Recursive Calls  */
	less = aop_cp_quicksort(less, sort_option);
	more = aop_cp_quicksort(more, sort_option);

	/* Merge */
	if (less != NULL) {
		end = less;
		while (end->next != NULL) {
			end = end->next;
		}
		end->next = list;
		list->next = more;
		return less;
	} else {
		list->next = more;
		return list;
	}
}

/* Remove all entries in callpath list */
static void aop_cp_remove_duplicate_entry(aop_caller_head_list *list)
{
	aop_caller_head_list *temp;
	aop_caller_list *ptr = NULL;
	aop_caller_list *new_ptr = NULL;
	aop_caller_head_list *head;

	if (!list)
		return;

	head = list;
	temp = list->next;

	while (temp != NULL) {
		aop_printk("start_addr => %08x :: %08x\n", head->sample->start_addr, temp->sample->start_addr);
		if (head->sample->start_addr == temp->sample->start_addr &&
				head->sample->cookie == temp->sample->cookie) {
			head->cnt += temp->cnt;
			head->sample->self_sample_cnt += temp->sample->self_sample_cnt;
			head->sample->total_sample_cnt += temp->sample->total_sample_cnt;

			head->next = temp->next;
			ptr = temp->sample;
			for (; ptr;) {
				new_ptr = ptr->caller;
				KDBG_MEM_DBG_KFREE(ptr);
				ptr = new_ptr;
			}
			KDBG_MEM_DBG_KFREE(temp);
			temp = head->next;
			continue;
		}
		head = head->next;
		temp = head->next;
	}
}

/* reverse call chain in callpath list */
static aop_caller_list *aop_cp_reverse(aop_caller_list *first)
{     aop_caller_list *cur = NULL, *tmp;
	while (first != NULL) {
		tmp = first;        /* insert value of 1st node  into p */
		first = first->caller;
		tmp->caller = cur;		/* insert p->next =NULL ,because H is equals to NULL (1st time) */
		cur = tmp;		/* insert address of p into H */
	}
	return cur;
}

/* Separate all callchains for each apps/ Processes */
static aop_caller_head_list  *aop_cp_separate_app(aop_caller_head_list **app_head,
		aop_caller_head_list *main_head, aop_cookie_t app_cookie)
{
	aop_caller_head_list *temp = NULL;
	aop_caller_head_list *prev = NULL;
	int found = 0;
	int start_node = 0;
	aop_caller_head_list *head;

	head = main_head;

	while (head != NULL) {
		aop_printk("\napp = %x :: cookie = %x\n", (unsigned)app_cookie, (unsigned)head->sample->app_cookie);

		if (head->sample->app_cookie == app_cookie) {
			if (!found) {
				aop_printk("=>Found=> app = %x :: cookie = %x\n", (unsigned)app_cookie, (unsigned)head->sample->app_cookie);
				*app_head  = head;
				if (temp) {
					prev = temp;
				} else {
					aop_printk("=First Node Match=\n");
					start_node = 1;
				}
				found = 1;
			}
			aop_printk("= Match Found = %d=\n", found);
			if (head->next == NULL) {
				aop_printk("=>Last entry => app = %x :: cookie = %x\n", (unsigned)app_cookie, (unsigned)head->sample->app_cookie);
				if (temp)
					temp->next = head;
				if (prev)
					prev->next = NULL;
				else {
					aop_printk("=Single Node Match=\n");
					main_head = NULL;
				}
				break;
			}

		} else if (found) {
			aop_printk("=>travserse => app = %x :: cookie = %x\n", (unsigned)app_cookie, (unsigned)head->sample->app_cookie);
			temp->next = NULL;
			if (!start_node)
				prev->next = head;
			else
				return head;
			break;
		}

		aop_printk("= Move head Found = %d=\n", found);
		temp = head;
		head = head->next;
	}

	return main_head;
}

static int aop_cp_generate_report(aop_caller_head_list *list, int show_option)
{
	aop_caller_head_list *temp = NULL;
	aop_caller_list *ptr = NULL;
	aop_caller_head_list *head = NULL;
	aop_caller_head_list *list_head = NULL;
	aop_caller_head_list *tmp_head = NULL;
	aop_caller_head_list *prev_head = NULL;
	aop_caller_head_list *dup_list = NULL;
	aop_caller_head_list *itr_head = NULL;
	aop_caller_list  *calleritem = NULL;
	int mis_match = 0;
	aop_caller_list *prev_calleritem = NULL;
	aop_vma_t start = 0;
	pid_t caller_tid = 0;
	char lib_name[DNAME_INLINE_LEN+1];
	char app_name[DNAME_INLINE_LEN+1];
	int index = 0;
	int operation = 0;
	int ret = 0;
	int first_match_found = 0;
	pid_t prev_tid = 0;		/*thread ID */
	aop_cookie_t prev_cookie = 0;
#if AOP_DEBUG_ON
	itr_head = list;
	PRINT_KD("\n\n\n\n===========Main List=====================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif
	dup_list = aop_cp_copy_list(list);
	if (!dup_list) {
		aop_errk("Callgraph list is empy\n");
		return -1;
	}

#if AOP_DEBUG_ON
	itr_head = dup_list;
	PRINT_KD("\n\n\n\n===========dup List - I=====================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	if (show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) {
		dup_list = aop_cp_quicksort(dup_list, AOP_CP_SORT_THREAD_WISE);
	} else {
		dup_list = aop_cp_quicksort(dup_list, AOP_CP_SORT_START_ADDR_WISE);
	}

	if (!dup_list) {
		aop_errk("Callgraph list is empy\n");
		return -1;
	}
#if AOP_DEBUG_ON
	itr_head = dup_list;
	PRINT_KD("\n\n\n\n===========dup List - II=====================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	head = dup_list;
	temp = dup_list->next;

	while (head != NULL) {
		aop_printk("\nStart =%x,  head->sample->start_addr = %x", start,  head->sample->start_addr);
		if (show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) {
			if (caller_tid != head->sample->tid) {
				caller_tid = head->sample->tid;
				aop_printk("\nAllocating head");
				tmp_head = (aop_caller_head_list *)KDBG_MEM_DBG_KMALLOC
					(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_head_list), GFP_KERNEL);
				if (!tmp_head) {
					aop_errk("aop_caller_list_head: tmp_head: no memory\n");
					if (list_head) {
						aop_printk("\n Deleting list_head...");
						aop_cp_free_list(list_head);
					}

					if (dup_list) {
						aop_printk("\n Deleting dup_list...");
						aop_cp_free_list(dup_list);
					}
					return -1; /* no memory!! */
				}
				aop_printk("\n Allocating head %x", tmp_head);
				memset(tmp_head, 0, sizeof(aop_caller_head_list));
				prev_calleritem = NULL;
				mis_match = 1;
			} else {
				mis_match = 0;
			}
		} else {
			if (start != head->sample->start_addr) {
				start = head->sample->start_addr;
				aop_printk("Allocating head\n");
				tmp_head = (aop_caller_head_list *)KDBG_MEM_DBG_KMALLOC
					(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_head_list), GFP_KERNEL);
				if (!tmp_head) {
					aop_errk("aop_caller_list_head: tmp_head: no memory\n");
					if (list_head) {
						aop_printk("\n Deleting list_head...");
						aop_cp_free_list(list_head);
					}

					if (dup_list) {
						aop_printk("\n Deleting dup_list...");
						aop_cp_free_list(dup_list);
					}
					return -1; /* no memory!! */
				}
				aop_printk("\nAllocating head %x", tmp_head);
				memset(tmp_head, 0, sizeof(aop_caller_head_list));
				prev_calleritem = NULL;
				mis_match = 1;
			} else {
				mis_match = 0;
			}
		}

		for (ptr = head->sample; ptr; ptr = ptr->caller) {
			if (show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) {
				if (!first_match_found) {
					aop_printk("first_match_found\n");
					first_match_found = 1;
					continue;
				}
			} else {
				if ((tmp_head->sample) && start == ptr->start_addr) {
					aop_printk("start addr match found\n");
					continue;
				}
				if ((show_option == AOP_CP_PROCESS_THREAD_WISE) && (ptr->tid == prev_tid) && (ptr->cookie == prev_cookie)) {
					PRINT_KD("\nAOP_CP_PROCESS_THREAD_WISE => TID and Cookie match\n");
					continue;
				}
			}
			aop_printk("\nAllocating Caller item\n");
			calleritem = (aop_caller_list *)KDBG_MEM_DBG_KMALLOC
				(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_list), GFP_KERNEL);
			if (!calleritem) {
				aop_errk("calleritem: no memory\n");
				if (list_head) {
					aop_printk("\n Deleting list_head...");
					aop_cp_free_list(list_head);
				}

				if (dup_list) {
					aop_printk("\n Deleting dup_list...");
					aop_cp_free_list(dup_list);
				}

				if (tmp_head)
					KDBG_MEM_DBG_KFREE(tmp_head);

				return -1; /* no memory!! */
			}
			memset(calleritem, 0, sizeof(aop_caller_list));

			calleritem->app_cookie = ptr->app_cookie;
			calleritem->cookie = ptr->cookie;
			calleritem->pc = ptr->pc;
			calleritem->in_kernel = ptr->in_kernel;
			calleritem->self_sample_cnt = ptr->self_sample_cnt;
			calleritem->total_sample_cnt = ptr->total_sample_cnt;
			calleritem->tgid = ptr->tgid;
			calleritem->tid = ptr->tid;
			calleritem->start_addr = ptr->start_addr;
			calleritem->caller = NULL;

			prev_tid = ptr->tid;
			prev_cookie = ptr->cookie;

			if (prev_calleritem)
				prev_calleritem->caller = calleritem;
			else {
				aop_printk("Allocating Caller item %x\n", tmp_head);
				tmp_head->sample = calleritem;
			}

			tmp_head->sample->total_sample_cnt += ptr->self_sample_cnt;
			if (list_head) {
				list_head->cnt +=  ptr->self_sample_cnt;
			}
			prev_calleritem = calleritem;
		}

		if (show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE)
			first_match_found = 0;
		/* move to end of head */
		if (mis_match) {
			if (list_head == NULL && prev_head == NULL) {
				aop_printk("Initialing Caller List Head %x\n", tmp_head);
				list_head = tmp_head;
				if (tmp_head->sample)
					list_head->cnt +=  tmp_head->sample->total_sample_cnt;
			} else {
				BUG_ON(!prev_head);
				aop_printk("growing Caller List Head %x\n", tmp_head);
				prev_head->next = tmp_head;
			}
			prev_head = tmp_head;
		}

		aop_printk("%s:%d\n", __FUNCTION__, __LINE__);
		if (temp)
			head = temp;
		else
			break;
		temp = temp->next;
	}

#if AOP_DEBUG_ON
	itr_head = list_head;
	PRINT_KD("\n\n\n\n===========list_head=====================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif
	aop_cp_item_idx = 0;

	if (!list_head) {
		aop_errk("Callgraph list is empy\n");
		if (dup_list) {
			aop_printk("\n Deleting dup_list...");
			aop_cp_free_list(dup_list);
		}
		return -1;
	}
	itr_head = list_head;


	if (show_option == AOP_CP_PROCESS_FUNC_WISE) {
		PRINT_KD("\n===================================="
				"Function wise report"
				"====================================");
	} else if (show_option == AOP_CP_PROCESS_THREAD_WISE) {
		PRINT_KD("\n=============================="
				"Process thread wise report"
				"====================================");
	} else if (show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) {
		PRINT_KD("\n=========================="
				"Process thread Function wise report"
				"===============================");
	}
#ifdef CONFIG_DWARF_MODULE
	PRINT_KD("\nIndex Self   Total Type TID      SymImage\t  Location    \t    APP\\ LIB");
#else
	PRINT_KD("\nIndex Self   Total Type TID      SymImage    APP\ LIB");
#endif
	PRINT_KD("\n===================================="
				"===================="
				"====================================");
	PRINT_KD("\n%-5d %4d  %5d    %c  %3d    %-10.10s\t\t%c\t  %s", index,
				((show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) ? 0 : itr_head->sample->self_sample_cnt),
				itr_head->cnt,
				'P',
				itr_head->sample->tid,
				aop_decode_cookie_without_path(itr_head->sample->app_cookie,
						app_name, DNAME_INLINE_LEN),
				'-',
				aop_decode_cookie_without_path(itr_head->sample->cookie,
						lib_name, DNAME_INLINE_LEN));
	while (itr_head) {
		ret = aop_cp_report_process_func_wise(itr_head->sample, 0, show_option);
		itr_head = itr_head->next;
	}

	do {
		PRINT_KD("\n\nPlease enter Index no to Show Function name and Abs Path(Exit => 99)=>");
		operation = debugd_get_event_as_numeric(NULL, NULL);
		if (operation < 1) {
			PRINT_KD("\nPlease enter Index no(Exit => 99)=>");
			continue;
		}
		aop_cp_item_idx = 0;
		itr_head = list_head;
		while (itr_head) {
			ret = aop_cp_report_process_func_wise(itr_head->sample, operation, show_option);
			if (ret == 1) {
				aop_cp_item_idx = 0;
				break;
			}
			itr_head = itr_head->next;
		}
	} while (operation != 99);

	if (list_head) {
		aop_printk("\n Deleting list_head...");
		aop_cp_free_list(list_head);
	}

	if (dup_list) {
		aop_printk("\n Deleting dup_list...");
		aop_cp_free_list(dup_list);
	}
	return ret;
}

/* Show AOP cp report */
int aop_cp_show_report(int option)
{
	int operation = 0;
	aop_caller_head_list *itr_head = NULL;
	int app_count = 0;
	char app_name[DNAME_INLINE_LEN+1];
	int ret = 0;


	do {
		PRINT_KD
			("\n\n----------------------------");
		PRINT_KD("\n Index\t AppName");
		PRINT_KD
			("\n----------------------------");
		WARN_ON(aop_app_caller_list_head == NULL);
		for (app_count = 0; aop_app_caller_list_head[app_count] != NULL; app_count++) {
			itr_head = aop_app_caller_list_head[app_count];
			if (itr_head && itr_head->sample) {
				PRINT_KD("\n %3d\t%s", (app_count+1),
					aop_decode_cookie_without_path(itr_head->sample->app_cookie,
					app_name, DNAME_INLINE_LEN));
			} else {
				PRINT_KD("\nitr_head->sample is NULL!!!");
			}
		}
		PRINT_KD
			("\n----------------------------");
		PRINT_KD("\nPlease select APP Index(Exit => 99)=>");
		operation = debugd_get_event_as_numeric(NULL, NULL);

		if (operation >  (app_count) && operation != 99) {
			PRINT_KD("\nPlease select index(1 - %d)", app_count);
			continue;
		} else if (operation == 99)
			break;

		itr_head = aop_app_caller_list_head[operation-1];
		if (itr_head)
			ret = aop_cp_generate_report(itr_head, option);
		else {
			aop_errk("aop_app_caller_list_head[%d-1] head is NULL", operation);
			ret = -1;
		}
	}	while (operation != 99);

	return ret;
}

int aop_cp_report_menu(void)
{
	int operation = 0;
	int ret = 1;

	do {
		PRINT_KD("\n");
		PRINT_KD("\nOptions are:");
		PRINT_KD
			("\n------------------------------------------------"
			 "--------------------\n");
		PRINT_KD(" 1. Callpath for Process-> Function\n");
		PRINT_KD(" 2. Callpath for Process-> Thread-> Function\n");
		PRINT_KD(" 3. Callpath for Process-> Thread\n");
		PRINT_KD
			("------------------------------------------------"
			 "--------------------\n");
		PRINT_KD(" 99 [Adv Oprofile]: Callpath Exit Menu\n");
		PRINT_KD
			("------------------------------------------------"
			 "--------------------\n");
		PRINT_KD("[Adv Oprofile]: Callpath Option ==>  ");

		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (operation) {
		case AOP_CP_PROCESS_FUNC_WISE:
			aop_cp_show_report(AOP_CP_PROCESS_FUNC_WISE);
			break;
		case AOP_CP_PROCESS_THREAD_FUNC_WISE:
			aop_cp_show_report(AOP_CP_PROCESS_THREAD_FUNC_WISE);
			break;
		case AOP_CP_PROCESS_THREAD_WISE:
			aop_cp_show_report(AOP_CP_PROCESS_THREAD_WISE);
			break;
		case 99:
			break;
		default:
			PRINT_KD("[Adv Oprofile]: CallPath Invalid Option....\n");
			ret = 1;	/* to show menu */
			break;
		}
	} while (operation != 99);

	PRINT_KD("[Adv Oprofile]: CallPath Exit....\n");
	/* as this return value is mean to show or not show the kdebugd menu options */
	return ret;
}

/* swap function for topthread_info */
int aop_cp_generate_trace_samples(void)
{
	aop_caller_head_list *itr_head = NULL;
	aop_caller_head_list *prev_node = NULL;
	aop_image_list *tmp_app_data = NULL;
	int app_count = 0;
	int i = 0;

	aop_printk("==============plain callgraph========\n");
	itr_head = aop_caller_list_head;
	while (itr_head) {
		itr_head->sample->self_sample_cnt = itr_head->cnt;
		itr_head->sample->total_sample_cnt = 0;
		aop_cp_add_start_addr(itr_head->sample);
		itr_head = itr_head->next;
	}

#if AOP_DEBUG_ON
	itr_head = aop_caller_list_head;
	PRINT_KD("\n\n\n\n========plain callgraph===============\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	aop_printk("==============sorting with APP Wise========\n");
	aop_caller_list_head = aop_cp_quicksort(aop_caller_list_head, AOP_CP_SORT_START_ADDR_WISE);
	if (!aop_caller_list_head) {
		aop_errk("aop_caller_list_head is empy\n");
		return -1;
	}

#if AOP_DEBUG_ON
	itr_head = aop_caller_list_head;
	PRINT_KD("\n\n\n\n=========sorting with APP Wise================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	aop_printk("==============Spearate Kernel callgraph============\n");
	itr_head = aop_caller_list_head;
	while (itr_head) {
		if (itr_head->sample->in_kernel) {
		    aop_caller_kernel_list_head = itr_head;
		    break;
		}
		prev_node = itr_head;
		itr_head = itr_head->next;
	}
	if (prev_node)
		prev_node->next = NULL; /* chk pt-> gj*/
	if (!aop_caller_kernel_list_head) {
		aop_errk("aop_caller_kernel_list_head is empy\n");
		return -1;
	}

#if AOP_DEBUG_ON
	itr_head = aop_caller_kernel_list_head;
	PRINT_KD("\n\n\n\n=========Separate kernel Samples================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}

	itr_head = aop_caller_list_head;
	PRINT_KD("\n\n\n\n=========Separate APP Samples================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	aop_printk("==============Reverse  kernel Samples============\n");
	itr_head = aop_caller_kernel_list_head;
	while (itr_head) {
		itr_head->sample = aop_cp_reverse(itr_head->sample);
		itr_head = itr_head->next;
	}

	aop_printk("==============check  kernel Samples============\n");
	if (!aop_caller_kernel_list_head) {
		aop_errk("aop_caller_kernel_list_head is empy\n");
		return -1;
	}
#if AOP_DEBUG_ON
	itr_head = aop_caller_kernel_list_head;
	PRINT_KD("\n\n\n\n=========Reverse kernel Samples================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	aop_printk("==============sorting with APP Wise========\n");
	aop_caller_list_head = aop_cp_quicksort(aop_caller_list_head, AOP_CP_SORT_APP_WISE);
	if (!aop_caller_list_head) {
		aop_errk("aop_caller_list_head is empy\n");
		return -1;
	}

#if AOP_DEBUG_ON
	itr_head = aop_caller_list_head;
	PRINT_KD("\n\n\n\n=========sorted APP Samples================\n");
	PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
	PRINT_KD("===================================\n");
	while (itr_head) {
		PRINT_KD("\n[%d]\t", itr_head->cnt);
		aop_sym_report_caller_sample(itr_head->sample);
		itr_head = itr_head->next;
	}
#endif

	if (aop_nr_user_samples) {
		if (!aop_caller_list_head) {
			aop_errk("Callgraph list is empy\n");
			return -1;
		}
		tmp_app_data = aop_app_list_head;
		while (tmp_app_data) {
			tmp_app_data = tmp_app_data->next;
			app_count++;
		}
		aop_printk("aop_count = %d\n", app_count);

		aop_app_caller_list_head = (aop_caller_head_list **)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_head_list *) * (app_count+1), GFP_KERNEL);
		if (!aop_app_caller_list_head) {
			aop_errk("aop_app_caller_list_head: no memory\n");
			return -1; /* no memory!! */
		}
		for (i = 0; i <= app_count; i++)
			aop_app_caller_list_head[i] = NULL;

		app_count = 0;

		tmp_app_data = aop_app_list_head;
		while (tmp_app_data) {
			aop_printk("Inside while\n");
			itr_head = aop_caller_list_head;
			aop_caller_list_head = aop_cp_separate_app(&aop_app_caller_list_head[app_count],
					itr_head, tmp_app_data->cookie_value);

			aop_printk("==============sorting each app callgraph with Samples========\n");

			aop_app_caller_list_head[app_count] = aop_cp_quicksort(aop_app_caller_list_head[app_count], AOP_CP_SORT_START_ADDR_WISE);

			if (!aop_app_caller_list_head[app_count]) {
				aop_errk("Callgraph list is empy\n");
				return -1;
			}

#if AOP_DEBUG_ON
			itr_head = aop_app_caller_list_head[app_count];
			PRINT_KD("\n\n\n\n=========sorting with Start Addr================\n");
			PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
			PRINT_KD("===================================\n");
			while (itr_head) {
				PRINT_KD("\n[%d]\t", itr_head->cnt);
				aop_sym_report_caller_sample(itr_head->sample);
				itr_head = itr_head->next;
			}
#endif

			aop_printk("==============Remove duplicate addr========\n");
			aop_cp_remove_duplicate_entry(aop_app_caller_list_head[app_count]);
			if (!aop_app_caller_list_head[app_count]) {
				aop_errk("aop_caller_list_head is empy\n");
				return -1;
			}
#if AOP_DEBUG_ON
			itr_head = aop_app_caller_list_head[app_count];
			PRINT_KD("\n\n\n\n=========Remove duplicate addr================\n");
			PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
			PRINT_KD("===================================\n");
			while (itr_head) {
				PRINT_KD("\n[%d]\t", itr_head->cnt);
				aop_sym_report_caller_sample(itr_head->sample);
				itr_head = itr_head->next;
			}
#endif
			aop_printk("==============aop_cp_reverse app Samples============\n");
			itr_head = aop_app_caller_list_head[app_count];
			while (itr_head) {
				itr_head->sample = aop_cp_reverse(itr_head->sample);
				itr_head = itr_head->next;
			}
			if (!aop_app_caller_list_head[app_count]) {
				aop_errk("aop_caller_list_head is empy\n");
				return -1;
			}

#if AOP_DEBUG_ON
			itr_head = aop_app_caller_list_head[app_count];
			PRINT_KD("\n\n\n\n===========Reverse App[%d]=====================\n", app_count);
			PRINT_KD("Depth\t  vma\t  \tAppName \tLibName \tSymImage\n");
			PRINT_KD("===================================\n");
			while (itr_head) {
				PRINT_KD("\n[%d]\t", itr_head->cnt);
				aop_sym_report_caller_sample(itr_head->sample);
				itr_head = itr_head->next;
			}
#endif
			tmp_app_data = tmp_app_data->next;
			app_count++;
		}
	}
	return 0;
}

/* delete all app, kernel CP list */
void aop_cp_free_mem()
{
	aop_caller_list *ptr;
	aop_caller_head_list *itr_head;
	aop_caller_head_list *new_itr_head;
	aop_caller_list *new_ptr = NULL;

	int i = 0;

	if (aop_caller_list_head) {
		itr_head = aop_caller_list_head;
		aop_printk("\nGoing to Delete CP List");
		while (itr_head != NULL)  {
			ptr = itr_head->sample;
			for (; ptr;) {
				aop_printk("\nDeleting Samples CP List");
				new_ptr = ptr->caller;
				KDBG_MEM_DBG_KFREE(ptr);
				ptr = new_ptr;
			}

			aop_printk("\nDeleting CP List");
			new_itr_head = itr_head->next;
			KDBG_MEM_DBG_KFREE(itr_head);
			itr_head = new_itr_head;
		}
	}

	if (aop_caller_kernel_list_head) {
		itr_head = aop_caller_kernel_list_head;
		aop_printk("\nGoing to Delete Kernel CP List");
		while (itr_head != NULL) {
			ptr = itr_head->sample;
			for (; ptr;) {
				aop_printk("\nDeleting Kernel Samples CP List");
				new_ptr = ptr->caller;
				KDBG_MEM_DBG_KFREE(ptr);
				ptr = new_ptr;
			}

			aop_printk("\nDeleting Kernel CP List");
			new_itr_head = itr_head->next;
			KDBG_MEM_DBG_KFREE(itr_head);
			itr_head = new_itr_head;
		}
		aop_caller_kernel_list_head = NULL;
	}

	if (aop_app_caller_list_head) {
		aop_printk("\nGoing to Delete App CP List");
		for (i = 0; aop_app_caller_list_head[i] != NULL ; i++) {
			aop_printk("\nGoing to Delete APP[%d] CP List", i);
			itr_head = aop_app_caller_list_head[i];
			while (itr_head != NULL) {
				ptr = itr_head->sample;
				for (; ptr;) {
					aop_printk("\nDeleting App Samples CP List");
					new_ptr = ptr->caller;
					KDBG_MEM_DBG_KFREE(ptr);
					ptr = new_ptr;
				}

				aop_printk("\nDeleting APP[%d] CP List", i);
				new_itr_head = itr_head->next;
				KDBG_MEM_DBG_KFREE(itr_head);
				itr_head = new_itr_head;
			}
			aop_app_caller_list_head[i] = NULL;
		}
		aop_printk("\nDeleting App CP List");
		KDBG_MEM_DBG_KFREE(aop_app_caller_list_head);
		aop_app_caller_list_head = NULL;
	}

	return;
}

/* to show CP menu */
int aop_cp_show_menu()
{
	if(!aop_nr_user_samples){
		PRINT_KD("\nUser CallPath list is empty");
		PRINT_KD("\n[WARNING !!!]Please run user application\n");
		return 1;
	}

	if (aop_caller_list_head) {
		if (aop_cp_generate_trace_samples() < 0)
			return -1;
	}

	if ((aop_caller_head_list **)aop_app_caller_list_head) {
		if (aop_cp_report_menu() > 0)
			return 1;
		else
			return -1;
	} else {
		PRINT_KD("\nCallPath list/ aop_app_caller_list_head is empty");
		PRINT_KD("\n[WARNING !!!]Please run user application\n");
		return 1;
	}
}

/* Process/ Manage Callpath chain list */
void  aop_cp_process_current_call_sample(void)
{
	aop_caller_head_list *itr_head = NULL;
	aop_caller_head_list *tmp_head = NULL;
	aop_caller_head_list *prev_head = NULL;
	aop_caller_list *ptr;
	aop_caller_list *prev = NULL;
	aop_caller_list  *calleritem = NULL;
	int item = 0;
	int found = 0;
	int depth = 0;

	if (!g_aop_trace_depth) {
		aop_printk("[%s]: Depth = %d\n", __FUNCTION__, g_aop_trace_depth);
		return;
	}

	itr_head = aop_caller_list_head;

	while (itr_head) {
		prev_head = itr_head;

		ptr = prev_head->sample;

		found = 0;
		for (depth = 0; ptr && depth < g_aop_trace_depth; ptr = ptr->caller, depth++) {
			if (ptr->cookie != g_aop_cp_chain[depth].cookie
					|| ptr->pc != g_aop_cp_chain[depth].pc) {
				break;
			}
		}

		if (g_aop_trace_depth == depth && ptr == NULL)
			found = 1;
		aop_printk("Found = %d\n", found);
		if (found) {
			prev_head->cnt++;
			break;
		}

		itr_head = itr_head->next;
	}

	aop_printk("[%s]: Trace Flag[%d]  = %d\n", __FUNCTION__, found,
		.......);

	if (!found) {
		tmp_head = (aop_caller_head_list *)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_head_list), GFP_KERNEL);
		if (!tmp_head) {
			aop_errk("aop_caller_list_head: tmp_head: no memory\n");
			return; /* no memory!! */
		}

		memset(tmp_head, 0, sizeof(aop_caller_head_list));
		tmp_head->cnt = 1;

		aop_printk("Trace Flag: Not Found= %d\n", g_aop_trace_depth);

		for (item = 0; item < g_aop_trace_depth; item++) {
			calleritem = (aop_caller_list *)KDBG_MEM_DBG_KMALLOC
				(KDBG_MEM_REPORT_MODULE, sizeof(aop_caller_list), GFP_KERNEL);
			if (!calleritem) {
				aop_errk("calleritem: no memory\n");
				break; /* no memory!! */
			}

			memset(calleritem, 0, sizeof(aop_caller_list));

			calleritem->app_cookie = g_aop_cp_chain[item].app_cookie;
			calleritem->cookie = g_aop_cp_chain[item].cookie;
			calleritem->pc = g_aop_cp_chain[item].pc;
			calleritem->in_kernel = g_aop_cp_chain[item].in_kernel;
			calleritem->tid = g_aop_cp_chain[item].tid;
			calleritem->tgid = g_aop_cp_chain[item].tgid;
			calleritem->caller = NULL;

			if (prev)
				prev->caller = calleritem;
			else
				tmp_head->sample = calleritem;

			prev = calleritem;
		}

		itr_head = aop_caller_list_head;
		while (itr_head) {
			prev_head = itr_head;
			itr_head = itr_head->next;
		}

		/* move to end of head */
		if (aop_caller_list_head == NULL && prev_head == NULL) {
			aop_printk("Initialing Caller List Head----> \n");
			aop_caller_list_head = tmp_head;
		} else {
			BUG_ON(!prev_head);
			prev_head->next = tmp_head;
		}
	}

}
/* Callgraph */

/*sort the linked list by sample count.
Algorithm used is selection sort.
This sorts the application and library based linked list*/
static void aop_sort_image_list(int type, int cpu)
{
	struct aop_image_list *a = NULL;
	struct aop_image_list *b = NULL;
	struct aop_image_list *c = NULL;
	struct aop_image_list *d = NULL;
	struct aop_image_list *tmp = NULL;
	struct aop_image_list *head = NULL;
	int sample_count_a = 0;
	int sample_count_b = 0;

	BUG_ON(cpu < 0 || cpu > NR_CPUS);

	switch (type) {
	case AOP_TYPE_APP:
		head = aop_app_list_head;
		break;
	case AOP_TYPE_LIB:
		head = aop_lib_list_head;
		break;
	default:
		PRINT_KD("\n");
		PRINT_KD("wrong type(%d)\n", type);
		return;
	}

	a = c = head;

	while (a->next != NULL) {
		d = b = a->next;
		while (b != NULL) {
			/* sample_count_a = a->samples_count[cpu] */
			if (cpu < NR_CPUS) {
				sample_count_a = a->samples_count[cpu];
				sample_count_b = b->samples_count[cpu];
			} else {
				sample_count_a =
				    COUNT_SAMPLES(a, samples_count, NR_CPUS);
				sample_count_b =
				    COUNT_SAMPLES(b, samples_count, NR_CPUS);
			}

			if (sample_count_a < sample_count_b) {
				/* neighboring linked list node */
				if (a->next == b) {
					if (a == head) {
						a->next = b->next;
						b->next = a;
						tmp = a;
						a = b;
						b = tmp;
						head = a;
						c = a;
						d = b;
						b = b->next;
					} else {
						a->next = b->next;
						b->next = a;
						c->next = b;
						tmp = a;
						a = b;
						b = tmp;
						d = b;
						b = b->next;
					}
				} else {
					if (a == head) {
						tmp = b->next;
						b->next = a->next;
						a->next = tmp;
						d->next = a;
						tmp = a;
						a = b;
						b = tmp;
						d = b;
						b = b->next;
						head = a;
					} else {
						tmp = b->next;
						b->next = a->next;
						a->next = tmp;
						c->next = b;
						d->next = a;
						tmp = a;
						a = b;
						b = tmp;
						d = b;
						b = b->next;
					}
				}
			} else {
				d = b;
				b = b->next;
			}
		}
		c = a;
		a = a->next;
	}
	switch (type) {
	case AOP_TYPE_APP:
		aop_app_list_head = head;
		break;
	case AOP_TYPE_LIB:
		aop_lib_list_head = head;
		break;
	default:
		PRINT_KD("\nwrong type(%d)\n", type);
		return;
	}
}

/*sort the linked list by sample count.
Algorithm used is selection sort.
This sorts the TID and IGID based linked list*/
static void aop_sort_pid_list(int type, int cpu)
{
	struct aop_pid_list *a = NULL;
	struct aop_pid_list *b = NULL;
	struct aop_pid_list *c = NULL;
	struct aop_pid_list *d = NULL;
	struct aop_pid_list *tmp = NULL;
	struct aop_pid_list *head = NULL;
	int sample_count_a = 0;
	int sample_count_b = 0;

	BUG_ON(cpu < 0 || cpu > NR_CPUS);

	switch (type) {
	case AOP_TYPE_TGID:
		head = aop_tgid_list_head;
		break;
	case AOP_TYPE_TID:
		head = aop_tid_list_head;
		break;
	default:
		PRINT_KD("\n");
		PRINT_KD("wrong type(%d)\n", type);
		return;
	}

	a = c = head;

	while (a->next != NULL) {
		d = b = a->next;
		while (b != NULL) {
			if (cpu < NR_CPUS) {
				sample_count_a = a->samples_count[cpu];
				sample_count_b = b->samples_count[cpu];
			} else {
				sample_count_a =
				    COUNT_SAMPLES(a, samples_count, NR_CPUS);
				sample_count_b =
				    COUNT_SAMPLES(b, samples_count, NR_CPUS);
			}

			if (sample_count_a < sample_count_b) {
				/* neighboring linked list node */
				if (a->next == b) {
					if (a == head) {
						a->next = b->next;
						b->next = a;
						tmp = a;
						a = b;
						b = tmp;
						head = a;
						c = a;
						d = b;
						b = b->next;
					} else {
						a->next = b->next;
						b->next = a;
						c->next = b;
						tmp = a;
						a = b;
						b = tmp;
						d = b;
						b = b->next;
					}
				} else {
					if (a == head) {
						tmp = b->next;
						b->next = a->next;
						a->next = tmp;
						d->next = a;
						tmp = a;
						a = b;
						b = tmp;
						d = b;
						b = b->next;
						head = a;
					} else {
						tmp = b->next;
						b->next = a->next;
						a->next = tmp;
						c->next = b;
						d->next = a;
						tmp = a;
						a = b;
						b = tmp;
						d = b;
						b = b->next;
					}
				}
			} else {
				d = b;
				b = b->next;
			}
		}
		c = a;
		a = a->next;
	}
	switch (type) {
	case AOP_TYPE_TGID:
		aop_tgid_list_head = head;
		break;
	case AOP_TYPE_TID:
		aop_tid_list_head = head;
		break;
	default:
		PRINT_KD("\n");
		PRINT_KD("wrong type(%d)\n", type);
		return;
	}
}

/*free all the resources that are taken by the system
while processing the tid and tgid data*/
void aop_free_tgid_tid_resources(void)
{
	aop_pid_list *tmp_tgid_data = aop_tgid_list_head;
	aop_pid_list *tmp_pid_data = aop_tid_list_head;

	/*free the TGID data */
	while (tmp_tgid_data) {
		aop_tgid_list_head = tmp_tgid_data->next;
		KDBG_MEM_DBG_KFREE(tmp_tgid_data->thread_name);
		KDBG_MEM_DBG_KFREE(tmp_tgid_data);
		tmp_tgid_data = aop_tgid_list_head;
	}
	/*free the PID data */
	while (tmp_pid_data) {
		aop_tid_list_head = tmp_pid_data->next;
		KDBG_MEM_DBG_KFREE(tmp_pid_data->thread_name);
		KDBG_MEM_DBG_KFREE(tmp_pid_data);
		tmp_pid_data = aop_tid_list_head;
	}
	aop_tgid_list_head = NULL;
	aop_tid_list_head = NULL;
}

/*free all the resources taken by the system while processing the data*/
void aop_free_resources(void)
{
	aop_image_list *tmp_app_data = aop_app_list_head;
	aop_image_list *tmp_lib_data = aop_lib_list_head;

	aop_read_buf_pos = 0;
	aop_samples_remaining = 0;
	memset(&aop_trans_data, 0, sizeof(struct op_data));

	spin_lock(&aop_kernel_list_lock);
	aop_free_kernel_data();	/*free the kernel data */
	spin_unlock(&aop_kernel_list_lock);
	aop_sym_report_free_sample_data();	/* free the sym data */

	/*free the application data */
	while (tmp_app_data) {
		aop_app_list_head = tmp_app_data->next;
		KDBG_MEM_DBG_KFREE(tmp_app_data);
		tmp_app_data = aop_app_list_head;
	}
	/*free the library data */
	while (tmp_lib_data) {
		aop_lib_list_head = tmp_lib_data->next;
		KDBG_MEM_DBG_KFREE(tmp_lib_data);
		tmp_lib_data = aop_lib_list_head;
	}

	/*free up the buffer that is taken by the system
	   when collecting and processing by tid and tgid */
	aop_free_tgid_tid_resources();

	aop_app_list_head = NULL;
	aop_lib_list_head = NULL;

	aop_nr_user_samples = 0;
	aop_nr_total_samples = 0;
}

/*initializes the data and structures before processing*/
static void aop_init_processing(void)
{
	/*caching the write offset before processing.
	   Otherwise the race conditions may occur. */
	aop_samples_remaining = aop_cache.wr_offset / AOP_ULONG_SIZE;

	aop_printk("%s:Data entries to process aop_samples_remaining=%d\n",
		   __FUNCTION__, aop_samples_remaining);
}

/*decode the cookie into the path name of application and libraries.*/
static char *aop_decode_cookie(aop_cookie_t cookie, char *buf, size_t buf_size)
{
	/*call the function to decode the cookie value into directory PATH */
	if (buf)
		aop_sys_lookup_dcookie(cookie, buf, buf_size);
	return buf;
}

/*Delete the nodes in the linked list whose sample count is zero.*/
static void aop_clean_tgid_list(void)
{
	aop_pid_list *tmp_tgid_data = NULL;
	aop_pid_list *tmp = NULL;

	/* If no sample recieved, head is not allocated.
	 * Check and return*/
	if (!aop_tgid_list_head) {
		PRINT_KD("\n");
		PRINT_KD("TGID List Empty !!!\n");
		return;
	}

	while (!COUNT_SAMPLES(aop_tgid_list_head, samples_count, NR_CPUS)) {
		tmp = aop_tgid_list_head->next;
		KDBG_MEM_DBG_KFREE(aop_tgid_list_head->thread_name);
		KDBG_MEM_DBG_KFREE(aop_tgid_list_head);
		aop_tgid_list_head = tmp;
		if (!aop_tgid_list_head) {
			PRINT_KD("\n");
			PRINT_KD("TGID List Empty !!!\n");
			return;
		}
	}

	tmp = aop_tgid_list_head;
	tmp_tgid_data = tmp->next;

	while (tmp_tgid_data) {
		if (!COUNT_SAMPLES(tmp_tgid_data, samples_count, NR_CPUS)) {
			tmp->next = tmp_tgid_data->next;
			KDBG_MEM_DBG_KFREE(tmp_tgid_data->thread_name);
			KDBG_MEM_DBG_KFREE(tmp_tgid_data);
			tmp_tgid_data = tmp->next;
		} else {
			tmp = tmp->next;
			tmp_tgid_data = tmp->next;
		}
	}
}

/*Delete the nodes in the linked list whose sample count is zero.*/
static void aop_clean_tid_list(void)
{
	aop_pid_list *tmp_tid_data = NULL;
	aop_pid_list *tmp = NULL;

	/* If no sample recieved, head is not allocated.
	 * Check and return*/
	if (!aop_tid_list_head) {
		PRINT_KD("\n");
		PRINT_KD("TID List Empty !!!\n");
		return;
	}

	while (!COUNT_SAMPLES(aop_tid_list_head, samples_count, NR_CPUS)) {
		tmp = aop_tid_list_head->next;
		KDBG_MEM_DBG_KFREE(aop_tid_list_head->thread_name);
		KDBG_MEM_DBG_KFREE(aop_tid_list_head);
		aop_tid_list_head = tmp;
		if (!aop_tid_list_head) {
			PRINT_KD("\n");
			PRINT_KD("TID List Empty!!!\n");
			return;
		}
	}

	tmp = aop_tid_list_head;
	tmp_tid_data = tmp->next;
	while (tmp_tid_data) {
		if (!COUNT_SAMPLES(tmp_tid_data, samples_count, NR_CPUS)) {
			tmp->next = tmp_tid_data->next;

			KDBG_MEM_DBG_KFREE(tmp_tid_data->thread_name);
			KDBG_MEM_DBG_KFREE(tmp_tid_data);
			tmp_tid_data = tmp->next;
		} else {
			tmp = tmp->next;
			tmp_tid_data = tmp->next;
		}
	}
}

/* Get the process name for given pid/tgid */
void aop_get_comm_name(int flag, pid_t pid, char *t_name)
{
	struct task_struct *tsk = NULL;

	if (!t_name)
		return;

	/*Take RCU read lock register can be changed */
	rcu_read_lock();
	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);

	if (tsk)
		get_task_struct(tsk);

	rcu_read_unlock();

	if (tsk) {
		/*Unlock */
		task_lock(tsk);

		/* Namit: taking lock is safe in printk ?? */
		aop_printk("Given %s %d, TID %d TGID %d comm name %s\n",
			   (flag) ? "tgid" : "pid", pid, tsk->pid, tsk->tgid,
			   tsk->comm);

		strlcpy(t_name, tsk->comm, TASK_COMM_LEN);
		task_unlock(tsk);
		put_task_struct(tsk);

	} else if (pid) {
		/*This is for the thread which are created and died
		   between the time the sampling stops and processing is done */
		strncpy(t_name, "---", 4);
	} else {
		/*This is for idle task having pid = 0 */
		strncpy(t_name, AOP_IDLE_TH_NAME, TASK_COMM_LEN);
	}

}

void *aop_create_node(int type)
{
	void *ret_buf = NULL;
	aop_pid_list *tmp_tgid_data = NULL;
	aop_pid_list *tmp_tid_data = NULL;
	switch (type) {
	case AOP_TYPE_TGID:
		tmp_tgid_data =
		    (aop_pid_list *)
		    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					 sizeof(aop_pid_list), GFP_KERNEL);
		if (!tmp_tgid_data) {
			aop_errk("tmp_tgid_data: no memory\n");
			return NULL;	/* no memory!! */
		}

		memset(tmp_tgid_data, 0, sizeof(aop_pid_list));

		tmp_tgid_data->thread_name =
		    (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 TASK_COMM_LEN, GFP_KERNEL);
		if (!tmp_tgid_data->thread_name) {
			aop_errk("tmp_tgid_data->thread_name: no memory\n");
			KDBG_MEM_DBG_KFREE(tmp_tgid_data);
			return NULL;	/* no memory!! */
		}

		ret_buf = (void *)tmp_tgid_data;
		break;
	case AOP_TYPE_TID:
		tmp_tid_data =
		    (aop_pid_list *)
		    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					 sizeof(aop_pid_list), GFP_KERNEL);
		if (!tmp_tid_data) {
			aop_errk("tmp_tid_data: no memory\n");
			return NULL;	/* no memory!! */
		}
		memset(tmp_tid_data, 0, sizeof(aop_pid_list));

		tmp_tid_data->thread_name =
		    (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 TASK_COMM_LEN, GFP_KERNEL);
		if (!tmp_tid_data->thread_name) {
			aop_errk("tmp_tid_data->thread_name: no memory\n");
			KDBG_MEM_DBG_KFREE(tmp_tid_data);
			return NULL;	/* no memory!! */
		}
		ret_buf = (void *)tmp_tid_data;
		break;
	default:
		aop_errk("Iinvalid Type....\n");
	}
	return ret_buf;
}

/*add the sample to generate the report by TGID(process ID) wise.*/
void aop_add_sample_tgid(void)
{
	aop_pid_list *tmp_tgid_data = NULL;
	aop_pid_list *tgiditem = NULL;

	/*Check if it is the first sample.If it is, create the head node of
	   the tgid link list. */
	if (!aop_tgid_list_head) {
		aop_tgid_list_head =
		    (aop_pid_list *) aop_create_node(AOP_TYPE_TGID);
		if (!aop_tgid_list_head) {
			aop_errk("\nFailed to create TGID...\n");
			return;
		}

		aop_get_comm_name(1, aop_trans_data.tgid,
				  aop_tgid_list_head->thread_name);
		aop_tgid_list_head->tgid = aop_trans_data.tgid;
		aop_tgid_list_head->pid = aop_trans_data.tid;

		aop_tgid_list_head->samples_count[aop_trans_data.cpu] = 1;
		aop_tgid_list_head->next = NULL;
		return;
	}

	tmp_tgid_data = aop_tgid_list_head;

	/*add the sample in tgid link list and increase the count */
	while (1) {
		if (tmp_tgid_data) {
			if (tmp_tgid_data->tgid == aop_trans_data.tgid) {
				/*if match found, increment the count */
				tmp_tgid_data->samples_count[aop_trans_data.
							     cpu]++;
				break;
			}
			if (tmp_tgid_data->next) {
				tmp_tgid_data = tmp_tgid_data->next;
			} else {
				/*create the node for some pids which are not
				   yet registered in link list */
				tgiditem =
				    (aop_pid_list *)
				    aop_create_node(AOP_TYPE_TGID);
				if (!tgiditem) {
					aop_errk
					    ("\nFailed to create TGID...\n");
					return;
				}

				aop_get_comm_name(1, aop_trans_data.tgid,
						  tgiditem->thread_name);
				tgiditem->tgid = aop_trans_data.tgid;
				tgiditem->pid = aop_trans_data.tid;
				tgiditem->samples_count[aop_trans_data.cpu] = 1;
				tgiditem->next = NULL;
				tmp_tgid_data->next = tgiditem;
				break;
			}
		} else {
			PRINT_KD("\n");
			PRINT_KD
			    ("aop_add_sample_tgid:check head of link list\n");
			break;
		}
	}
}

void aop_create_dead_list(struct task_struct *tsk)
{
	aop_dead_list *node = NULL;

	if (!tsk) {
		aop_errk("NULL task\n");
		return;
	}

	node = (aop_dead_list *) KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						      sizeof(aop_dead_list),
						      GFP_KERNEL);
	if (!node) {
		aop_errk("aop_dead_list : node: no memory\n");
		return;		/* no memory!! */
	}
	node->thread_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
							 TASK_COMM_LEN,
							 GFP_KERNEL);
	if (!node->thread_name) {
		aop_errk("aop_dead_list : node->thread_name: no memory\n");
		KDBG_MEM_DBG_KFREE(node);
		node = NULL;
		return;		/* no memory!! */
	}

	if (!node) {
		aop_errk("\nFailed to create TID...\n");
		return;
	}

	/* Already called from safe point no need to protect */
	strlcpy(node->thread_name, tsk->comm, TASK_COMM_LEN);
	node->pid = tsk->pid;
	node->tgid = tsk->tgid;

	aop_printk("Created pid %d, TID COMM %s\n", node->pid,
		   node->thread_name);

	node->next = aop_dead_list_head;
	aop_dead_list_head = node;
}

EXPORT_SYMBOL(aop_create_dead_list);

/* prepare tgid and Tid list for the report showing */
static void aop_process_dead_list(void)
{
	aop_dead_list *node = NULL;
	aop_dead_list *prev_node = NULL;
	aop_pid_list *tid_list_node = NULL;
	aop_pid_list *tgid_list_node = NULL;
	int i;

	for (node = aop_dead_list_head; node;) {
		tid_list_node = (aop_pid_list *) aop_create_node(AOP_TYPE_TID);
		if (!tid_list_node) {
			aop_errk("\nFailed to create TID...\n");
			return;
		}
		memcpy(tid_list_node->thread_name, node->thread_name,
		       TASK_COMM_LEN);
		aop_printk("Created pid %d, comm %s & TID COMM %s\n",
			   node->pid, tid_list_node->thread_name,
			   node->thread_name);
		tid_list_node->pid = node->pid;
		tid_list_node->tgid = node->tgid;

		for (i = 0; i < NR_CPUS; i++)
			tid_list_node->samples_count[i] = 0;

		tid_list_node->next = aop_tid_list_head;
		aop_tid_list_head = tid_list_node;

		if (node->pid == node->tgid) {
			tgid_list_node =
			    (aop_pid_list *) aop_create_node(AOP_TYPE_TGID);
			if (!tgid_list_node) {
				aop_errk("\nFailed to create TGID...\n");
				return;
			}

			memcpy(tgid_list_node->thread_name, node->thread_name,
			       TASK_COMM_LEN);
			aop_printk("Created tgid %d, comm %s & TID COMM %s\n",
				   node->tgid, tgid_list_node->thread_name,
				   node->thread_name);
			tgid_list_node->tgid = node->tgid;
			for (i = 0; i < NR_CPUS; i++)
				tgid_list_node->samples_count[i] = 0;

			tgid_list_node->next = aop_tgid_list_head;
			aop_tgid_list_head = tgid_list_node;
		}
		prev_node = node;
		node = node->next;
		/*free the mortuary data */
		KDBG_MEM_DBG_KFREE(prev_node->thread_name);
		KDBG_MEM_DBG_KFREE(prev_node);
		prev_node = NULL;
	}
	aop_dead_list_head = NULL;
}

/*add the sample to generate the report by tid(thread ID) wise.*/
static void aop_add_sample_tid(void)
{
	aop_pid_list *tmp_tid_data = NULL;
	aop_pid_list *tiditem = NULL;

	/*Check if it is the first sample.If it is, create the head node of
	   the tid link list. */
	if (!aop_tid_list_head) {
		aop_tid_list_head =
		    (aop_pid_list *) aop_create_node(AOP_TYPE_TID);
		if (!aop_tid_list_head) {
			aop_errk("\nFailed to create TID...\n");
			return;
		}
		aop_get_comm_name(0, aop_trans_data.tid,
				  aop_tid_list_head->thread_name);
		aop_tid_list_head->pid = aop_trans_data.tid;
		aop_tid_list_head->tgid = aop_trans_data.tgid;
		aop_tid_list_head->samples_count[aop_trans_data.cpu] = 1;
		aop_tid_list_head->next = NULL;
		return;
	}

	tmp_tid_data = aop_tid_list_head;

	/*add the sample in tid link list and increase the count */
	while (1) {
		if (tmp_tid_data) {
			if (tmp_tid_data->pid == aop_trans_data.tid) {
				/*if match found, increment the count */
				tmp_tid_data->samples_count[aop_trans_data.
							    cpu]++;
				break;
			}
			if (tmp_tid_data->next) {
				tmp_tid_data = tmp_tid_data->next;
			} else {
				/*create the node for some pids which are not
				   yet registered in link list */
				tiditem =
				    (aop_pid_list *)
				    aop_create_node(AOP_TYPE_TID);
				if (!tiditem) {
					aop_errk("\nFailed to create TID...\n");
					return;
				}

				aop_get_comm_name(0, aop_trans_data.tid,
						  tiditem->thread_name);
				tiditem->pid = aop_trans_data.tid;
				tiditem->tgid = aop_trans_data.tgid;
				tiditem->samples_count[aop_trans_data.cpu] = 1;
				tiditem->next = NULL;
				tmp_tid_data->next = tiditem;
				break;
			}
		} else {
			PRINT_KD("\n");
			PRINT_KD
			    ("aop_add_sample_tid:check head of link list\n");
			break;
		}
	}
}

/*context changes are prefixed by an escape code.
This will return if the code is escape code or not.*/
static inline int aop_is_escape_code(uint64_t code)
{
	return kernel_pointer_size == 4 ? code == ~0LU : code == ~0LLU;
}

/*pop the raw data buffer value*/
static int aop_pop_buffer_value(unsigned long *val)
{
	if (!aop_samples_remaining) {
		aop_errk("BUG: popping empty buffer !\n");
		return -ENOBUFS;
	}
	*val = aop_cache.buffer[aop_read_buf_pos++];
	aop_samples_remaining--;

	return 0;
}

/*returns if size number of elements are in data buffer or not.*/
static int aop_enough_remaining(size_t size)
{
	if (aop_samples_remaining >= size)
		return 1;

	aop_printk("%s: Dangling ESCAPE_CODE.\n", __FUNCTION__);
	return 0;
}

/*process the pc and event sample*/
static void aop_put_sample(unsigned long pc)
{
	unsigned long event;
	aop_image_list *tmp_app_data = NULL;
	aop_image_list *tmp_lib_data = NULL;
	aop_image_list *appitem = NULL;
	aop_image_list *libitem = NULL;

	/*before popping the value, check if it avaiable in data buffer */
	if (!aop_enough_remaining(1)) {
		aop_samples_remaining = 0;
		return;
	}

	/* Callgraph */
	aop_total_report_samples++;
	/* Callgraph */

	if (aop_pop_buffer_value(&event) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, Buffer empty...returning\n", __FUNCTION__);
		return;
	}

	/* Callgraph */
	if (event)
		aop_printk("**********Event is Not Zero = %lx\n", event);

	if (aop_trans_data.tracing != AOP_TRACING_ON)
		aop_trans_data.event = event;

	aop_trans_data.pc = pc;


	if (g_aop_trace_flag > 0) {
		/* set trans data */
		aop_cp_add_node();
		++g_aop_trace_flag;
		aop_printk("[%s]: Trace Flag = %d\n", __FUNCTION__,  g_aop_trace_flag);
		return;
	}

	++g_aop_trace_flag;
	/* Callgraph */

	aop_nr_total_samples++;

	/*add the sample for tgid */
	aop_add_sample_tgid();

	/*add the sample for pid */
	aop_add_sample_tid();

	if (aop_trans_data.tracing != AOP_TRACING_ON)
		aop_trans_data.event = event;

	aop_trans_data.pc = pc;

	/* to log symbol wise samples */
	aop_sym_report_update_sample_data(&aop_trans_data);

	/*	WARN_ON(aop_trans_data.cpu >= NR_CPUS); */

	/*	if ((aop_trans_data.cpu >= NR_CPUS)) */
/* callgraph */
	WARN_ON(aop_trans_data.cpu >= NR_CPUS);
	if (aop_trans_data.cpu >= NR_CPUS)
/* callgraph */
		return;

	/*find the context, whether the sample is for kernel context or user */
	if (aop_trans_data.in_kernel) {
		spin_lock(&aop_kernel_list_lock);
		aop_update_kernel_sample(&aop_trans_data);
		spin_unlock(&aop_kernel_list_lock);
	} else {
		aop_nr_user_samples++;

		/*Check if it is the first sample.If it is, create the head nodes of
		   the library and application link list. */
		if (!aop_app_list_head) {
			aop_app_list_head =
			    (aop_image_list *)
			    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 sizeof(aop_image_list),
						 GFP_KERNEL);
			if (!aop_app_list_head) {
				aop_errk
				    ("aop_image_list: aop_app_list_head: no memory\n");
				aop_nr_user_samples = 0;
				return;	/* no memory!! */
			}

			memset(aop_app_list_head, 0, sizeof(aop_image_list));

			aop_app_list_head->cookie_value =
			    aop_trans_data.app_cookie;
			aop_app_list_head->samples_count[aop_trans_data.cpu] =
			    1;
			aop_app_list_head->next = NULL;
		}

		if (!aop_lib_list_head) {
			aop_lib_list_head =
			    (aop_image_list *)
			    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 sizeof(aop_image_list),
						 GFP_KERNEL);
			if (!aop_lib_list_head) {
				aop_errk
				    ("aop_image_list: aop_lib_list_head: no memory\n");
				aop_nr_user_samples = 0;
				KDBG_MEM_DBG_KFREE(aop_app_list_head);
				return;	/* no memory!! */
			}
			memset(aop_lib_list_head, 0, sizeof(aop_image_list));

			aop_lib_list_head->cookie_value = aop_trans_data.cookie;
			aop_lib_list_head->samples_count[aop_trans_data.cpu] =
			    1;
			aop_lib_list_head->next = NULL;
			return;
		}

		tmp_app_data = aop_app_list_head;
		tmp_lib_data = aop_lib_list_head;

		/*add the sample in application link list and increase the count */
		while (1) {
			if (tmp_app_data) {
				if (tmp_app_data->cookie_value ==
				    aop_trans_data.app_cookie) {
					/*if match found, increment the count */
					tmp_app_data->
					    samples_count[aop_trans_data.cpu]++;
					break;
				}
				if (tmp_app_data->next) {
					tmp_app_data = tmp_app_data->next;
				} else {
					/*create the node */
					appitem =
					    (aop_image_list *)
					    KDBG_MEM_DBG_KMALLOC
					    (KDBG_MEM_REPORT_MODULE,
					     sizeof(aop_image_list),
					     GFP_KERNEL);
					if (!appitem) {
						aop_errk
						    ("aop_image_list: appitem: no memory\n");
						break;	/* no memory!! */
					}
					memset(appitem, 0,
					       sizeof(aop_image_list));

					appitem->cookie_value =
					    aop_trans_data.app_cookie;
					appitem->samples_count[aop_trans_data.
							       cpu] = 1;
					appitem->next = NULL;
					tmp_app_data->next = appitem;
					break;
				}
			}
		}

		/*add the sample in library link list and increase the count */
		while (1) {
			if (tmp_lib_data) {
				if (tmp_lib_data->cookie_value ==
				    aop_trans_data.cookie) {
					/*if match found, increment the count */
					tmp_lib_data->
					    samples_count[aop_trans_data.cpu]++;
					break;
				}
				if (tmp_lib_data->next) {
					tmp_lib_data = tmp_lib_data->next;
				} else {
					/*create the node */
					libitem =
					    (aop_image_list *)
					    KDBG_MEM_DBG_KMALLOC
					    (KDBG_MEM_REPORT_MODULE,
					     sizeof(aop_image_list),
					     GFP_KERNEL);
					if (!libitem) {
						aop_errk
						    ("aop_image_list: libitem: no memory\n");
						break;	/* no memory!! */
					}
					memset(libitem, 0,
					       sizeof(aop_image_list));

					libitem->cookie_value =
					    aop_trans_data.cookie;
					libitem->samples_count[aop_trans_data.
							       cpu] = 1;
					libitem->next = NULL;
					tmp_lib_data->next = libitem;
					break;
				}
			}
		}
	}
}

static void aop_code_unknown(void)
{
	aop_printk("enter\n");
}

static void aop_code_ctx_switch(void)
{
	unsigned long val;

	aop_printk("enter\n");

	/*This handler would require 5 samples in the data buffer.
	   check if 5 elements exists in buffer. */
	if (!aop_enough_remaining(5)) {
		aop_samples_remaining = 0;
		return;
	}

	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, TID Buffer empty... returning ---\n",
			 __FUNCTION__);
		return;
	}
	aop_trans_data.tid = val;
	aop_printk("tid %d ", aop_trans_data.tid);

	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, APP_COOKIE Buffer empty...returning ---\n",
			 __FUNCTION__);
		return;
	}
	aop_trans_data.app_cookie = val;

	aop_printk("app_cookie %lu ", (unsigned long)aop_trans_data.app_cookie);

	/*
	   must be ESCAPE_CODE, CTX_TGID_CODE, tgid. Like this
	   because tgid was added later in a compatible manner.
	 */
	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, ESCAPE_CODE Buffer empty...returning ---\n",
			 __FUNCTION__);
		return;
	}
	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, CTX_TGID_CODE Buffer empty...returning ---\n",
			 __FUNCTION__);
		return;
	}

	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, TGID Buffer empty...returning ---\n",
			 __FUNCTION__);
		return;
	}
	aop_trans_data.tgid = val;
	aop_printk("tgid %d\n", aop_trans_data.tgid);
}

static void aop_code_cpu_switch(void)
{
	unsigned long val;
	if (!aop_enough_remaining(1)) {
		aop_samples_remaining = 0;
		return;
	}

	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, Buffer empty...returning\n", __FUNCTION__);
		return;
	}
	aop_trans_data.cpu = val;

}

static void aop_code_cookie_switch(void)
{
	unsigned long val;
	aop_printk("enter\n");

	if (!aop_enough_remaining(1)) {
		aop_samples_remaining = 0;
		return;
	}

	if (aop_pop_buffer_value(&val) == -ENOBUFS) {
		aop_samples_remaining = 0;
		PRINT_KD("%s, Buffer empty...returning\n", __FUNCTION__);
		return;
	}
	aop_trans_data.cookie = val;

	aop_printk("cookie 0x%lx\n", (unsigned long)aop_trans_data.cookie);
}

static void aop_code_kernel_enter(void)
{
	aop_printk("enter\n");
	aop_trans_data.in_kernel = 1;
}

static void aop_code_user_enter(void)
{
	aop_printk("enter\n");
	aop_trans_data.in_kernel = 0;
}

static void aop_code_module_loaded(void)
{
	aop_printk("enter\n");
}

static void aop_code_trace_begin(void)
{
	aop_printk("TRACE_BEGIN\n");
	aop_trans_data.tracing = AOP_TRACING_START;
	/* Callgraph */
	aop_cp_process_current_call_sample();
	g_aop_trace_flag = 0;
	g_aop_trace_depth = 0;
	memset(g_aop_cp_chain, 0, sizeof(g_aop_cp_chain));
}

static void aop_code_trace_end(void)
{
	aop_printk("TRACE_END\n");
	aop_trans_data.tracing = AOP_TRACING_OFF;
/* Callgraph */
}

/*handlers are registered which are responsible for every type of code*/
aop_handler_t handlers[TRACE_END_CODE + 1] = {
	&aop_code_unknown,
	&aop_code_ctx_switch,
	&aop_code_cpu_switch,
	&aop_code_cookie_switch,
	&aop_code_kernel_enter,
	&aop_code_user_enter,
	&aop_code_module_loaded,
	&aop_code_unknown,
	&aop_code_trace_begin,
	/* Callgraph */
	&aop_code_trace_end,
	/* Callgraph */
};

#if defined(AOP_DEBUG_ON) && (AOP_DEBUG_ON != 0)
void aop_chk_resources(void)
{
	if (aop_app_list_head != NULL)
		aop_errk("aop_app_list_head is not NULL\n");
	if (aop_lib_list_head != NULL)
		aop_errk("aop_lib_list_head is not NULL\n");

	if (aop_nr_user_samples != 0)
		aop_errk("aop_nr_user_samples is not NULL\n");

	if (aop_nr_total_samples != 0)
		aop_errk("aop_nr_total_samples is not NULL\n");

	if (aop_tgid_list_head != NULL)
		aop_errk("aop_tgid_list_head is not NULL\n");

	if (aop_tid_list_head != NULL)
		aop_errk("aop_tid_list_head is not NULL\n");

	if (aop_dead_list_head != NULL)
		aop_errk("aop_dead_list_head is not NULL\n");
}
#endif

/*process all the samples from the buffer*/
int aop_process_all_samples(void)
{
	unsigned long code;
	int count_sample, total_no_of_samples, prev_per = 0;
	int total_escape_code = 0;

	AOP_PRINT_TID_LIST(__FUNCTION__);
	AOP_PRINT_TGID_LIST(__FUNCTION__);

	/*reset all the resiurces taken for processing */
	/* aop_free_resources(); */
	aop_chk_resources();

	/* prepare tgid and Tid list for the report showing */
	aop_process_dead_list();

	/*initialize the data that is required for processing */
	aop_init_processing();

	spin_lock(&aop_kernel_list_lock);
	/* init kernel data with/without vmlinux */
	aop_create_vmlinux();
	spin_unlock(&aop_kernel_list_lock);

	/* allocate memory for symbol report */
	if (aop_sym_report_init() != 0) {
		aop_printk("Failed to init symbol info head list\n");
		return 1;
	}

	total_no_of_samples = aop_samples_remaining;

	/*process all the samples one by one. */
	while (aop_samples_remaining) {
		count_sample = total_no_of_samples - aop_samples_remaining;
		if (count_sample) {
			int per = ((count_sample * 100) / total_no_of_samples);
			if (!(per % 10) && (prev_per != per)) {
				prev_per = per;
				PRINT_KD("Processing Samples ...%d%%\r", per);
			}
		}

		if (aop_pop_buffer_value(&code) == -ENOBUFS) {
			aop_samples_remaining = 0;
			PRINT_KD("\n");
			PRINT_KD("%s, Buffer empty...returning\n",
				 __FUNCTION__);
			return -ENOBUFS;
		}

		if (!aop_is_escape_code(code)) {
			aop_put_sample(code);
			continue;
		} else {
			total_escape_code++;
		}

		if (!aop_samples_remaining) {
			PRINT_KD("\n");
			PRINT_KD("%s: Dangling ESCAPE_CODE.\n", __FUNCTION__);
			break;
		}

		/*started with ESCAPE_CODE, next is type */
		if (aop_pop_buffer_value(&code) == -ENOBUFS) {
			aop_samples_remaining = 0;
			PRINT_KD("\n");
			PRINT_KD("%s, Buffer empty...returning\n",
				 __FUNCTION__);
			return -ENOBUFS;
		}

		/* if (code >= TRACE_END_CODE) { */
		if (code > (TRACE_END_CODE + 1)) { /* callgraph */
			PRINT_KD("\n");
			aop_printk("%s: Unknown code %lu\n", __FUNCTION__, code);
			continue;
		}

		if (code < (TRACE_END_CODE + 1))
			handlers[code] ();
	}

	/*Delete the nodes in the linked list whose sample count is zero. */
	aop_clean_tgid_list();
	aop_clean_tid_list();

	/* loop will quit before calculate the processing level, so updated here */
	PRINT_KD("Processing Samples ...100%%\n");
	aop_printk("Total Samples processed=%lu\n", aop_nr_total_samples);
	aop_printk("Total user Samples processed=%lu\n", aop_nr_user_samples);
	aop_printk("Total kernel Samples processed=%lu\n",
		   aop_nr_kernel_samples);
	aop_printk("Total escape code =%d\n", total_escape_code);
	aop_printk("Processing Done...\n");

	AOP_PRINT_TID_LIST(__FUNCTION__);
	AOP_PRINT_TGID_LIST(__FUNCTION__);

	return 0;
}

/*Dump the application data*/
int aop_op_generate_app_samples(void)
{
	char *buf;
	size_t buf_size = AOP_MAX_SYM_NAME_LENGTH;
	aop_image_list *tmp_app_data = NULL;
	int perc = 0;
	int index = 1;
	unsigned int choice = 0;
	aop_cookie_t app_cookie = 0;
	int sample_count = 0;

	buf = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					   buf_size, GFP_KERNEL);
	if (!buf) {
		aop_errk("buf: no memory\n");
		return 0;
	}
	aop_printk("Total user Samples collected (%lu)\n", aop_nr_user_samples);
	PRINT_KD("Total Samples (%lu)\n", aop_nr_total_samples);

	if (aop_nr_user_samples) {
		PRINT_KD("Report Generation For [ALL]\n");
		PRINT_KD("\n");
		PRINT_KD("Index\t  Samples  %%\tApplication Image\n");
		PRINT_KD("----------------------------------------\n");
		if (aop_config_sort_option == AOP_SORT_BY_SAMPLES)
			aop_sort_image_list(AOP_TYPE_APP, NR_CPUS);
		tmp_app_data = aop_app_list_head;
		while (tmp_app_data) {
			sample_count =
			    COUNT_SAMPLES(tmp_app_data, samples_count, NR_CPUS);
			perc = sample_count * 100 / aop_nr_total_samples;
			PRINT_KD("%d\t%8u %3d%%\t%s\n", index, sample_count,
				 perc,
				 aop_decode_cookie(tmp_app_data->cookie_value,
						   buf, buf_size));
			tmp_app_data = tmp_app_data->next;
			++index;
		}
		PRINT_KD("[9999] Exit\n");

		while (1) {
			PRINT_KD("\n");
			PRINT_KD("Select Option (1 to %d & Exit - 9999)==>",
				 index - 1);
			choice = debugd_get_event_as_numeric(NULL, NULL);

			if (choice == 9999) {
				PRINT_KD("\n");
				break;
			}

			if (choice >= index || choice < 1) {
				PRINT_KD("\n");
				PRINT_KD("Invalid choice\n");
				continue;
			}
			tmp_app_data = aop_app_list_head;
			while ((--choice) != 0)
				tmp_app_data = tmp_app_data->next;

			app_cookie = tmp_app_data->cookie_value;

			/* No need to put  in the continuation of previouse PRINT_KD */
			PRINT_KD("\n");
			PRINT_KD("Symbol profiling for Application %s\n",
				 aop_decode_cookie(tmp_app_data->cookie_value,
						   buf, buf_size));
			aop_sym_report_per_image_user_samples
			    (IMAGE_TYPE_APPLICATION, app_cookie);
		}
		choice = 0;
		index = 1;
	}
	KDBG_MEM_DBG_KFREE(buf);
	return 0;
}

/*Dump the library data*/
int aop_op_generate_lib_samples(void)
{
	char *buf;
	size_t buf_size = AOP_MAX_SYM_NAME_LENGTH;
	aop_image_list *tmp_lib_data = NULL;
	int perc = 0;
	int index = 1;
	unsigned int choice = 0;
	aop_cookie_t lib_cookie = 0;
	int sample_count = 0;

	buf =
	    (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE, buf_size,
					 GFP_KERNEL);
	if (!buf) {
		aop_errk("buf: no memory\n");
		return 0;
	}

	aop_printk("Total user Samples collected (%lu)\n", aop_nr_user_samples);
	PRINT_KD("Total Samples (%lu)\n", aop_nr_total_samples);

	if (aop_nr_user_samples) {
		PRINT_KD("Report Generation For [ALL]\n");
		PRINT_KD("\n");
		PRINT_KD("Index\t  Samples  %%\tLibrary Image\n");
		PRINT_KD("----------------------------------------\n");
		if (aop_config_sort_option == AOP_SORT_BY_SAMPLES)
			aop_sort_image_list(AOP_TYPE_LIB, NR_CPUS);
		tmp_lib_data = aop_lib_list_head;
		while (tmp_lib_data) {
			sample_count =
			    COUNT_SAMPLES(tmp_lib_data, samples_count, NR_CPUS);
			perc = sample_count * 100 / aop_nr_total_samples;
			PRINT_KD("%d\t%8u %3d%%\t%s\n", index, sample_count,
				 perc,
				 aop_decode_cookie(tmp_lib_data->cookie_value,
						   buf, buf_size));
			tmp_lib_data = tmp_lib_data->next;
			++index;
		}
		PRINT_KD("[9999] Exit\n");

		while (1) {
			PRINT_KD("\n");
			PRINT_KD("Select Option (1 to %d & Exit - 9999)==>",
				 index - 1);
			choice = debugd_get_event_as_numeric(NULL, NULL);

			if (choice == 9999) {
				PRINT_KD("\n");
				break;
			}

			if (choice >= index || choice < 1) {
				PRINT_KD("\n");
				PRINT_KD("Invalid choice\n");
				continue;
			}

			PRINT_KD("Report Generation For [ALL]\n");
			tmp_lib_data = aop_lib_list_head;
			while ((--choice) != 0)
				tmp_lib_data = tmp_lib_data->next;

			lib_cookie = tmp_lib_data->cookie_value;

			/* No need to put  in the continuation of previouse PRINT_KD */
			PRINT_KD("\n");
			PRINT_KD("Symbol profiling for Library %s\n",
				 aop_decode_cookie(tmp_lib_data->cookie_value,
						   buf, buf_size));
			aop_sym_report_per_image_user_samples
			    (IMAGE_TYPE_LIBRARY, lib_cookie);
		}
		choice = 0;
		index = 1;
	}

	KDBG_MEM_DBG_KFREE(buf);
	return 0;
}

/* to prepare img list to show at report all symbol report */
static int aop_report_prepare_img_list(struct list_head *img_list_head)
{
	aop_image_list *tmp_lib_data = aop_lib_list_head;
	int sample_count = 0;

	while (tmp_lib_data) {
		sample_count =
		    COUNT_SAMPLES(tmp_lib_data, samples_count, NR_CPUS);
		if (sample_count) {
			struct aop_report_all_list *img_data;
			img_data =
			    (struct aop_report_all_list *)
			    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 sizeof(struct
							aop_report_all_list),
						 GFP_KERNEL);
			if (!img_data) {
				aop_errk
				    ("Add image data failed: aop_report_all_list: img_data: no memory\n");
				return 0;	/* no memory!!, at the same time we report other data  */
			}

			/* assign the sample information to  new img data */
			img_data->is_kernel = 0;
			img_data->report_type.cookie_value =
			    tmp_lib_data->cookie_value;
			img_data->samples_count = sample_count;
			list_add_tail(&img_data->report_list, img_list_head);
		}
		tmp_lib_data = tmp_lib_data->next;
	}

	return 0;
}

/*Dump whole data*/
int aop_op_generate_all_samples(void)
{
	char *buf;
	size_t buf_size = AOP_MAX_SYM_NAME_LENGTH;
	struct list_head *all_list_head;
	struct aop_report_all_list *plist;
	struct list_head *pos, *q;

	if (!aop_nr_kernel_samples && !aop_nr_user_samples) {
		PRINT_KD("No Samples found\n");
		return 1;
	}

	all_list_head = (struct list_head *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						      sizeof(struct list_head), GFP_KERNEL);
	if (!all_list_head) {
		return 1;
	}

	INIT_LIST_HEAD(all_list_head);

	buf =
	    (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE, buf_size,
					 GFP_KERNEL);
	if (!buf) {
		aop_errk("buf: no memory\n");
		KDBG_MEM_DBG_KFREE(all_list_head);
		return 1;
	}

	aop_printk("\nTotal kernel Samples collected (%lu)\n",
		   aop_nr_kernel_samples);
	aop_printk("\nTotal user Samples collected (%lu)\n",
		   aop_nr_user_samples);

	PRINT_KD("Total Samples (%lu)\n", aop_nr_total_samples);
	PRINT_KD("Samples\t  %%\tImage [module] name\n");
	PRINT_KD("-----------------------------------------\n");

	if (aop_nr_kernel_samples){
		spin_lock(&aop_kernel_list_lock);
		aop_kernel_prepare_report(all_list_head, NR_CPUS);
		spin_unlock(&aop_kernel_list_lock);
	}

	if (aop_nr_user_samples)
		aop_report_prepare_img_list(all_list_head);

	aop_list_sort(all_list_head, aop_kernel_report_cmp);

	/* print kernel module samples */
	list_for_each_safe(pos, q, all_list_head) {
		/* loop thru all the nodes */
		plist =
		    list_entry(pos, struct aop_report_all_list, report_list);
		if (plist) {
			int perc =
			    ((plist->samples_count * 100) /
			     aop_nr_total_samples);
			PRINT_KD("%8u %3d%%\t%s\n", plist->samples_count, perc,
				 (plist->is_kernel) ? plist->report_type.
				 kernel_name : aop_decode_cookie(plist->
								 report_type.
								 cookie_value,
								 buf,
								 buf_size));

			list_del(pos);

			/* free all report item memory */
			KDBG_MEM_DBG_KFREE(plist);
		}
	}
	PRINT_KD("-----------------------------------------\n");

	KDBG_MEM_DBG_KFREE(all_list_head);
	KDBG_MEM_DBG_KFREE(buf);
	return 1;
}

/*Dump data- TGID wise*/
int aop_op_generate_report_tgid(void)
{
	aop_pid_list *tmp_tgid_data = NULL;
	int perc = 0;
	int sample_count = 0;

	AOP_PRINT_TID_LIST(__FUNCTION__);
	AOP_PRINT_TGID_LIST(__FUNCTION__);

	PRINT_KD("Total Samples (%lu)\n", aop_nr_total_samples);
	if (aop_nr_total_samples) {

		if (aop_config_sort_option == AOP_SORT_BY_SAMPLES)
			aop_sort_pid_list(AOP_TYPE_TGID, NR_CPUS);

		tmp_tgid_data = aop_tgid_list_head;
		PRINT_KD("\n");
		PRINT_KD("Samples\t  %%\tPid\tProcess Name\n");
		PRINT_KD("--------------------------------------------\n");
		while (tmp_tgid_data) {
			sample_count =
			    COUNT_SAMPLES(tmp_tgid_data, samples_count,
					  NR_CPUS);
			perc = sample_count * 100 / aop_nr_total_samples;
			PRINT_KD("%8u %3d%%\t%d\t%.20s\n", sample_count,
				 perc, tmp_tgid_data->tgid,
				 tmp_tgid_data->thread_name);
			tmp_tgid_data = tmp_tgid_data->next;
		}
	}
	return 1;		/* to show the kdebug menu */
}

/* Dump data- TID wise */
int aop_op_generate_report_tid(int cpu_wise)
{
	aop_pid_list *tmp_tid_data = NULL;
	int perc = 0;
	pid_t pid;
	int cpu_option = 0;
	int sample_count = 0;
	int i = 0;
	int cpu_wise_perc[NR_CPUS] = { 0,};
	int cpu_wise_samples[NR_CPUS] = { 0,};

	AOP_PRINT_TID_LIST(__FUNCTION__);
	AOP_PRINT_TGID_LIST(__FUNCTION__);

	PRINT_KD("Total Samples (%lu)\n", aop_nr_total_samples);
	if (aop_nr_total_samples) {
		while (cpu_option != 9999) {
			if (cpu_wise) {

				for (i = 0; i < NR_CPUS; i++) {
					cpu_wise_perc[i] = 0;
					cpu_wise_samples[i] = 0;
				}

				if (aop_config_sort_option ==
				    AOP_SORT_BY_SAMPLES)
					aop_sort_pid_list(AOP_TYPE_TID,
							  cpu_option);

				PRINT_KD(" Samples    %%\tCPU\n");

				tmp_tid_data = aop_tid_list_head;
				while (tmp_tid_data) {
					for (i = 0; i < NR_CPUS; i++) {
						cpu_wise_perc[i] +=
						    tmp_tid_data->
						    samples_count[i] * 100 /
						    aop_nr_total_samples;
						cpu_wise_samples[i] +=
						    tmp_tid_data->
						    samples_count[i];
					}
					tmp_tid_data = tmp_tid_data->next;
				}

				for (i = 0; i < NR_CPUS; i++) {
					PRINT_KD("%8u %3d%%\t%d\n",
						 cpu_wise_samples[i],
						 cpu_wise_perc[i], i);
				}

				PRINT_KD("Select(9999 for Exit)==>  ");
				cpu_option =
				    debugd_get_event_as_numeric(NULL, NULL);

				if (cpu_option < 0
				    || cpu_option > (NR_CPUS - 1)) {
					PRINT_KD("\n");
					PRINT_KD("Invalid choice\n");
					if (cpu_option == 9999)
						break;
					cpu_option = 0;
					continue;
				}
			}

			PRINT_KD("\n");

			if (!cpu_wise)
				cpu_option = NR_CPUS;

			if (cpu_option < NR_CPUS) {
				while (1) {
					PRINT_KD
					    ("Report Generation For [CPU - %d]\n",
					     cpu_option);
					PRINT_KD("\n");
					PRINT_KD
					    (" Samples    %%\tTID\tTGID\tProcess Name\n");
					PRINT_KD
					    ("--------------------------------------------\n");

					if (aop_config_sort_option ==
					    AOP_SORT_BY_SAMPLES)
						aop_sort_pid_list(AOP_TYPE_TID,
								  cpu_option);

					tmp_tid_data = aop_tid_list_head;
					while (tmp_tid_data) {
						if (tmp_tid_data->
						    samples_count[cpu_option]) {
							perc =
							    tmp_tid_data->
							    samples_count
							    [cpu_option] * 100 /
							    aop_nr_total_samples;
							PRINT_KD
							    ("%8u %3d%%\t%d\t%d\t%.20s\n",
							     tmp_tid_data->
							     samples_count
							     [cpu_option], perc,
							     tmp_tid_data->pid,
							     tmp_tid_data->tgid,
							     tmp_tid_data->
							     thread_name);
						}
						tmp_tid_data =
						    tmp_tid_data->next;
					}
					PRINT_KD("\n");
					PRINT_KD
					    ("Enter TID for symbol wise report(9999 for Exit) ==>");
					pid =
					    debugd_get_event_as_numeric(NULL,
									NULL);
					PRINT_KD("\n");
					if (pid == 9999)
						break;

					aop_sym_report_per_tid(pid);
				}
			} else {
				while (1) {
					PRINT_KD
					    ("Report Generation For [ALL]\n");
					PRINT_KD("\n");
					PRINT_KD
					    (" Samples    %%\tTID\tTGID\tProcess Name\n");
					PRINT_KD
					    ("--------------------------------------------\n");

					if (aop_config_sort_option ==
					    AOP_SORT_BY_SAMPLES)
						aop_sort_pid_list(AOP_TYPE_TID,
								  NR_CPUS);

					tmp_tid_data = aop_tid_list_head;
					while (tmp_tid_data) {
						sample_count =
						    COUNT_SAMPLES(tmp_tid_data,
								  samples_count,
								  NR_CPUS);
						perc =
						    sample_count * 100 /
						    aop_nr_total_samples;
						PRINT_KD
						    ("%8u %3d%%\t%d\t%d\t%.20s\n",
						     sample_count, perc,
						     tmp_tid_data->pid,
						     tmp_tid_data->tgid,
						     tmp_tid_data->thread_name);
						tmp_tid_data =
						    tmp_tid_data->next;
					}
					PRINT_KD("\n");
					PRINT_KD
					    ("Enter TID for symbol wise report(9999 for Exit) ==>");
					pid =
					    debugd_get_event_as_numeric(NULL,
									NULL);
					PRINT_KD("\n");
					if (pid == 9999)
						break;

					aop_sym_report_per_tid(pid);
				}
			}

			if (!cpu_wise)
				cpu_option = 9999;
		}
	}
	return 1;		/* to show the kdebug menu */
}

#if AOP_DEBUG_ON
/*Dump the raw data from buffer aop_cache*/
int aop_dump_all_samples_callgraph(void)
{
	/*Read from the zeroth entry to maximum filled.
	   Cache the write offset. */
	int tmp_buf_write_pos = aop_cache.wr_offset;
	int count = 0;
	int prev_code = 0;

	PRINT_KD("\n");
	PRINT_KD("Data available %d\n", tmp_buf_write_pos / AOP_ULONG_SIZE);

	for (count = 0; count < tmp_buf_write_pos / AOP_ULONG_SIZE; count++) {
		if (prev_code == ESCAPE_CODE) {
			switch (aop_cache.buffer[count]) {
			case CTX_SWITCH_CODE:
				PRINT_KD("CTX_SWITCH_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case CPU_SWITCH_CODE:
				PRINT_KD("CPU_SWITCH_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case COOKIE_SWITCH_CODE:
				PRINT_KD("COOKIE_SWITCH_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case KERNEL_ENTER_SWITCH_CODE:
				PRINT_KD("KERNEL_ENTER_SWITCH_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case KERNEL_EXIT_SWITCH_CODE:
				PRINT_KD("KERNEL_EXIT_SWITCH_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case MODULE_LOADED_CODE:
				PRINT_KD("MODULE_LOADED_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case CTX_TGID_CODE:
				PRINT_KD("CTX_TGID_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case TRACE_BEGIN_CODE:
				PRINT_KD("TRACE_BEGIN_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			case TRACE_END_CODE:
				PRINT_KD("TRACE_END_CODE(%ld)\n", aop_cache.buffer[count]);
				break;
			default:
				PRINT_KD("----*** UNKOWN CODE ***%lu(%08lx)\n", aop_cache.buffer[count], aop_cache.buffer[count]);
				break;
			}

		} else {
			PRINT_KD("%lu(%08lx)\n", aop_cache.buffer[count], aop_cache.buffer[count]);
		}
		prev_code = aop_cache.buffer[count];
	}
	return 0;
}
#endif

/*
  * oprofile report init function
  */
#if AOP_DEBUG_ON
static int __init aop_opreport_kdebug_init(void)
{
	kdbg_register("PROFILE: process All Samples",
		      aop_process_all_samples, NULL,
		      KDBG_MENU_AOP_PROCESS_ALL_SAMPLES);

	kdbg_register("PROFILE: dump All raw data Samples",
				aop_dump_all_samples_callgraph, NULL,
		      KDBG_MENU_AOP_DUMP_ALL_SAMPLES);
	return 0;
}

__initcall(aop_opreport_kdebug_init);
#endif

/* collect all the process id and collect elf files belongs to the process
and load the elf database */
int aop_load_elf_db_for_all_samples(void)
{
	char *buf = NULL;
	char *filename = NULL;
	size_t buf_size = AOP_MAX_SYM_NAME_LENGTH;
	aop_image_list *tmp_lib_data = NULL;

	buf =
	    (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE, buf_size,
					 GFP_KERNEL);
	if (!buf) {
		PRINT_KD("buf: no memory\n");
		return 0;
	}

	if (!aop_nr_user_samples) {
		aop_printk(" No Samples\n");
		KDBG_MEM_DBG_KFREE(buf);
		return 0;
	}

	tmp_lib_data = aop_lib_list_head;
	while (tmp_lib_data) {
		filename =
		    aop_decode_cookie(tmp_lib_data->cookie_value, buf,
				      buf_size);

#ifdef CONFIG_ELF_MODULE
		kdbg_elf_load_elf_db_by_elf_file(filename);
#endif /* CONFIG_ELF_MODULE */
		tmp_lib_data = tmp_lib_data->next;
	}

	KDBG_MEM_DBG_KFREE(buf);
	return 0;
}

#if defined(AOP_DEBUG_ON) && (AOP_DEBUG_ON != 0)
void AOP_PRINT_TID_LIST(const char *msg)
{
	aop_pid_list *tmp_tid_data = NULL;
	tmp_tid_data = aop_tid_list_head;

	PRINT_KD("TID LIST: %s\n", msg);
	while (tmp_tid_data) {
		PRINT_KD("pid= 0x%x, tgid= 0x%x, [%s]\n", tmp_tid_data->pid,
			 tmp_tid_data->tgid, tmp_tid_data->thread_name);
		tmp_tid_data = tmp_tid_data->next;
	}
}

void AOP_PRINT_TGID_LIST(const char *msg)
{
	aop_pid_list *tmp_tgid_data = NULL;
	tmp_tgid_data = aop_tgid_list_head;

	PRINT_KD("TGID LIST: %s\n", msg);
	while (tmp_tgid_data) {
		PRINT_KD("pid= 0x%x, tgid= 0x%x, [%s]\n", tmp_tgid_data->pid,
			 tmp_tgid_data->tgid, tmp_tgid_data->thread_name);
		tmp_tgid_data = tmp_tgid_data->next;
	}
}
#endif
