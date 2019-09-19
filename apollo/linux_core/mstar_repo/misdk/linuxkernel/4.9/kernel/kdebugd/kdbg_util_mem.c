/*
 *  kernel/kdebugd/kdbg_util_mem.c
 *
 *  Implementation of memory leak and shutdown mode
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010.03.05  Created by gaurav.j.
 *
 */

#include <linux/types.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <kdebugd.h>
#include <linux/mm.h>

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
#include <sec_topthread.h>
#include <sec_cpuusage.h>
#include <sec_memusage.h>
#include <sec_diskusage.h>
#include <sec_netusage.h>
#endif /* CONFIG_KDEBUGD_COUNTER_MONITOR */

#include <linux/sched.h>

#include "kdbg_util.h"

#ifdef KDBG_MEM_DBG
#include "kdbg_elf_sym_debug.h"
#endif

#ifdef	CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_ADVANCE_OPROFILE
#include "aop_debug.h"
#include "aop_oprofile.h"	/* (TODO :error: implicit declaration of function "aop_shutdown" --GJ) */
#endif /* CONFIG_ADVANCE_OPROFILE */

/* M/m count for each module */
/* Hash Table */

#define HASH_ARRAY_SIZE 			1024
#define kdbg_mem_dbg_count_hashfn(nr) 	Key2Index(nr)

#ifdef KDBG_MEM_CHK
#define KDBG_MEM_DBG_PAD_LEN 			16
#define KDBG_MEM_DBG_TOTAL_PAD 		(2*KDBG_MEM_DBG_PAD_LEN)

/* 0xA5 = 10100101 this pattern may cause more errors for Testing only*/
const char kdbg_mem_dbg_chk_buff[16] = { 0xA5, 0xA5, 0xA5, 0xA5,
	0xA5, 0xA5, 0xA5, 0xA5,
	0xA5, 0xA5, 0xA5, 0xA5,
	0xA5, 0xA5, 0xA5, 0xA5
};
#endif

/* This member holds the head point of ELF name stored in database of ELF files */
static struct list_head *kdbg_mem_dbg_count_hash;

/*Once flag initialize means the hash table initialize.*/
static int kdbg_mem_init_flag;

static struct kmem_cache *mem_record_cache;
/*
  * This function generates the key for Hash index
  */
static inline unsigned int Key2Index(void *val)
{
	unsigned int hash = 0;

	hash = ((unsigned long)val / 8) % HASH_ARRAY_SIZE;

	return hash;
}

/*
 * This function initialises the mem database cache
  */
static void kdbg_mem_cache_init(int blk)
{
	if (!mem_record_cache) {
		sym_printk("mem_record_cache\n");
		mem_record_cache = kmem_cache_create("mem_record_cache",	/* Name */
						     blk,	/* Object Size */
						     0,	/* Alignment */
						     0,	/* Flags */
						     NULL);	/* Constructor/Deconstructor */
	}
}

/*
 * This function destroy the ELF cache
 */
static void remove_mem_cache(void)
{
	if (mem_record_cache) {
		PRINT_KD("Destroy Mem Cache\n");
		kmem_cache_destroy(mem_record_cache);
		mem_record_cache = NULL;
	}

	return;
}

/*
 * This function initialises the Hash index
 */
static int kdbg_mem_dbg_hash_init(void)
{
	int i;

	sym_printk("Enter\n");

	BUG_ON(kdbg_mem_dbg_count_hash);

	kdbg_mem_dbg_count_hash =
	    kmalloc(HASH_ARRAY_SIZE * sizeof(*(kdbg_mem_dbg_count_hash)),
		    GFP_KERNEL);
	if (!kdbg_mem_dbg_count_hash) {
		PRINT_KD("kdbg_mem_dbg_count_hash :No Mem %s\n", __FUNCTION__);
		return -ENOMEM;
	}

	for (i = 0; i < HASH_ARRAY_SIZE; i++)
		INIT_LIST_HEAD(&kdbg_mem_dbg_count_hash[i]);

	/*Turn on the Init Flag */
	kdbg_mem_init_flag = 1;

	return 0;
}

static void kdbg_mem_int(void)
{
	kdbg_mem_dbg_hash_init();

	kdbg_mem_cache_init(sizeof(struct hashtable));
}

/*
  * This function call the respective free function for kfree and vfree
  * Also decrease the memory usage count from each module
  */
int kdbg_mem_dbg_free_(kdbg_mem_dbg_handle_mode mode, void *paddrptr,
		       const char *pfile, int line, int check)
{
	struct list_head *elem, *q = NULL;
	struct hashtable *htable = NULL;
	int item_found = 0;
	void *ptr = NULL;
	int idx = 0;

	if ((char *)paddrptr == NULL)
		return 0;
#ifdef KDBG_MEM_CHK
	else if (((unsigned long)paddrptr) <= KDBG_MEM_DBG_PAD_LEN) {
		PRINT_KD
		    ("%s:%d:***AOP_MEM:  WARNING: Address is incorrect = %lu\n",
		     pfile, line, ((unsigned long)paddrptr));
		return 1;
	}
#endif

	if (mode != KDBG_MEM_VFREE_MODE && mode != KDBG_MEM_KFREE_MODE &&
	    mode != KDBG_MEM_KREALLOC_MODE) {
		PRINT_KD("*** AOP_MEM: Warning: Incorrect Mode\n");
		return 1;
	}
#ifdef KDBG_MEM_CHK
	ptr = (char *)paddrptr - KDBG_MEM_DBG_PAD_LEN;
#else
	ptr = (char *)paddrptr;
#endif

	idx = kdbg_mem_dbg_count_hashfn(ptr);

	list_for_each_safe(elem, q, &kdbg_mem_dbg_count_hash[idx]) {
		htable = list_entry(elem, struct hashtable, mem_chain);
		if (!htable) {
			sym_errk("htable is NULL\n");
			break;
		}

		if (htable->table.ptr_addr == ptr) {

#ifdef KDBG_MEM_CHK
			if (check == KDBG_MEM_CHECK_MODE) {
				if (memcmp
				    (kdbg_mem_dbg_chk_buff, ptr,
				     KDBG_MEM_DBG_PAD_LEN) != 0) {
					PRINT_KD
					    ("%s:%d:*** AOP_MEM: Warning:M/m Corrupt  Before the pointer\n",
					     htable->table.pfile,
					     htable->table.lineno);
				}
				if (memcmp(kdbg_mem_dbg_chk_buff,
					   (ptr + htable->table.mem_size +
					    KDBG_MEM_DBG_PAD_LEN),
					   KDBG_MEM_DBG_PAD_LEN) != 0) {
					PRINT_KD
					    ("%s:%d:*** AOP_MEM: Warning:M/m Corrupt  after the pointer\n",
					     htable->table.pfile,
					     htable->table.lineno);
				}

				if (mode == KDBG_MEM_VFREE_MODE &&
				    htable->table.alloc_mode !=
				    KDBG_MEM_VMALLOC_MODE) {
					PRINT_KD
					    ("%s:%d:*** AOP_MEM: Warning:M/m deallocationconflict"
					     "kmalloc/krealloc -> vfree\n",
					     htable->table.pfile,
					     htable->table.lineno);
				} else if (mode == KDBG_MEM_KFREE_MODE
					   && !(htable->table.alloc_mode ==
						KDBG_MEM_KMALLOC_MODE
						|| htable->table.alloc_mode ==
						KDBG_MEM_KREALLOC_MODE)) {
					PRINT_KD
					    ("%s:%d:*** AOP_MEM: Warning: M/m deallocation conflict "
					     "vmalloc -> kfree\n",
					     htable->table.pfile,
					     htable->table.lineno);
				}
			}
#endif
			list_del(elem);
			kmem_cache_free(mem_record_cache, htable);
			htable = NULL;
			item_found = 1;
			break;
		}
	}

	if (!item_found) {
		PRINT_KD
		    ("%s:%d:***AOP_MEM:  WARNING: ITEM NOT FOUND FOR Addr = %lu(%lx)\n",
		     pfile, line, ((unsigned long)paddrptr), (long)paddrptr);
		return 1;
	}

	if (mode == KDBG_MEM_VFREE_MODE)
		vfree(ptr);
	else if (mode == KDBG_MEM_KFREE_MODE)
		kfree(ptr);

	return 0;
}

/*
 * This function checks the pointer provided in hash list and
 * checks memory for it.
 */
int kdbg_mem_dbg_check_(kdbg_mem_dbg_handle_mode mode, void *paddrptr,
			const char *pfile, int line)
{
	struct list_head *elem = NULL;
	struct hashtable *htable = NULL;
	int item_found = 0;
	void *ptr = NULL;
	int idx = 0;

	if ((char *)paddrptr == NULL)
		return 0;
#ifdef KDBG_MEM_CHK
	else if (((unsigned long)paddrptr) <= KDBG_MEM_DBG_PAD_LEN) {
		PRINT_KD
		    ("%s:%d:***AOP_MEM:  WARNING: Address is incorrect = %lu\n",
		     pfile, line, ((unsigned long)paddrptr));
		return 1;
	}
#endif

	if (mode != KDBG_MEM_VFREE_MODE && mode != KDBG_MEM_KFREE_MODE &&
	    mode != KDBG_MEM_KREALLOC_MODE) {
		PRINT_KD("*** AOP_MEM: Warning: Incorrect Mode\n");
		return 1;
	}
#ifdef KDBG_MEM_CHK
	ptr = (char *)paddrptr - KDBG_MEM_DBG_PAD_LEN;
#else
	ptr = (char *)paddrptr;
#endif

	idx = kdbg_mem_dbg_count_hashfn(ptr);

	list_for_each(elem, &kdbg_mem_dbg_count_hash[idx]) {
		htable = list_entry(elem, struct hashtable, mem_chain);
		if (!htable) {
			sym_errk("htable is NULL\n");
			break;
		}

		if (htable->table.ptr_addr == ptr) {
#ifdef KDBG_MEM_CHK
			if (memcmp
			    (kdbg_mem_dbg_chk_buff, ptr,
			     KDBG_MEM_DBG_PAD_LEN) != 0) {
				PRINT_KD
				    ("%s:%d:*** AOP_MEM: Warning:M/m Corrupt  Before the pointer\n",
				     htable->table.pfile, htable->table.lineno);
			}
			if (memcmp(kdbg_mem_dbg_chk_buff,
				   (ptr + htable->table.mem_size +
				    KDBG_MEM_DBG_PAD_LEN),
				   KDBG_MEM_DBG_PAD_LEN) != 0) {
				PRINT_KD
				    ("%s:%d:*** AOP_MEM: Warning:M/m Corrupt  after the pointer\n",
				     htable->table.pfile, htable->table.lineno);
			}

			if (mode == KDBG_MEM_VFREE_MODE &&
			    htable->table.alloc_mode != KDBG_MEM_VMALLOC_MODE) {
				PRINT_KD
				    ("%s:%d:*** AOP_MEM: Warning:M/m deallocationconflict"
				     "kmalloc/krealloc -> vfree\n",
				     htable->table.pfile, htable->table.lineno);
			} else if (mode == KDBG_MEM_KFREE_MODE
				   && !(htable->table.alloc_mode ==
					KDBG_MEM_KMALLOC_MODE
					|| htable->table.alloc_mode ==
					KDBG_MEM_KREALLOC_MODE)) {
				PRINT_KD
				    ("%s:%d:*** AOP_MEM: Warning: M/m deallocation conflict "
				     "vmalloc -> kfree\n", htable->table.pfile,
				     htable->table.lineno);
			}
#endif
			item_found = 1;
			break;
		}
	}

	if (!item_found) {
		PRINT_KD
		    ("%s:%d:***AOP_MEM:  WARNING: ITEM NOT FOUND FOR Addr = %lu(%lx)\n",
		     pfile, line, ((unsigned long)paddrptr), (long)paddrptr);
		return 1;
	}

	return 0;
}

/*
  * This function call the respective malloc function for kmalloc/ vmalloc
  * Also maintains the memory usage count taken by each module
  */
void *kdbg_mem_dbg_alloc_(kdbg_mem_dbg_module module,
			  kdbg_mem_dbg_handle_mode mode, const void *p,
			  size_t size, unsigned int flag, const char *pfile,
			  int line)
{
	struct hashtable *htable = NULL;
	void *ptr = NULL;

	/*Initilaze the module first time */
	if (!kdbg_mem_init_flag) {

		/*Initiatize function */
		kdbg_mem_int();
	}

	/*!!!BUG- INIT Flag is On But Memory Pointer in NULL!! */
	BUG_ON(!kdbg_mem_dbg_count_hash);

	switch (mode) {
	case KDBG_MEM_VMALLOC_MODE:
#ifdef KDBG_MEM_CHK
		ptr = vmalloc(size + KDBG_MEM_DBG_TOTAL_PAD);
#else
		ptr = vmalloc(size);
#endif
		break;

	case KDBG_MEM_KMALLOC_MODE:
#ifdef KDBG_MEM_CHK
		ptr = kmalloc((size + KDBG_MEM_DBG_TOTAL_PAD), flag);
#else
		ptr = kmalloc(size, flag);
#endif
		break;

	case KDBG_MEM_KREALLOC_MODE:

		/* check old pointer in hash list and check memory */
		if (kdbg_mem_dbg_check_(mode, (void *)p, pfile, line))
			return NULL;

#ifdef KDBG_MEM_CHK
		/* kdbg_mem_dbg_check_ has already checked validity of pointer "p"
		 * and for p == NULL, no need to substract KDBG_MEM_DBG_PAD_LEN.
		 */
		ptr =
		    krealloc(p ? ((char *)p - KDBG_MEM_DBG_PAD_LEN) : p,
			     (size + KDBG_MEM_DBG_TOTAL_PAD), flag);
#else
		ptr = krealloc(p, size, flag);
#endif

		/* remove old pointer from hash list */
		if (p && ptr
		    && kdbg_mem_dbg_free_(mode, (void *)p, pfile, line,
					  KDBG_MEM_DEFAULT_MODE)) {
			return NULL;
		}
		break;

	default:
		PRINT_KD("*** AOP_MEM: Warning: Incorrect Mode\n");
		return NULL;
		break;
	}
	if (!ptr) {
		sym_errk("ERROR: Module: %d, size = %d, file = %s, Line = %d",
			 module, size, pfile, line);
		return NULL;
	}
#ifdef KDBG_MEM_CHK
	memset(ptr, 0xA5, KDBG_MEM_DBG_PAD_LEN);
	memset((ptr + size + KDBG_MEM_DBG_PAD_LEN), 0xA5, KDBG_MEM_DBG_PAD_LEN);
#endif

	if (mem_record_cache) {
		htable =
		    (struct hashtable *)kmem_cache_alloc(mem_record_cache,
							 GFP_KERNEL);
	}

	if (!htable) {
		if (mode == KDBG_MEM_VMALLOC_MODE)
			vfree(ptr);
		else if (mode == KDBG_MEM_KMALLOC_MODE)
			kfree(ptr);
		else if (mode == KDBG_MEM_KREALLOC_MODE)
			kfree(ptr);

		PRINT_KD("*** AOP_MEM: Hash Table Warning: kmalloc fails\n");
		return NULL;
	}

	htable->table.mem_size = size;
	htable->table.ptr_addr = ptr;
#ifdef KDBG_MEM_CHK
	htable->table.module = module;
	htable->table.pfile = pfile;
	htable->table.lineno = line;
	htable->table.alloc_mode = mode;
#endif

	list_add(&htable->mem_chain,
		 &kdbg_mem_dbg_count_hash[kdbg_mem_dbg_count_hashfn(ptr)]);

#ifdef KDBG_MEM_CHK
	return (char *)ptr + KDBG_MEM_DBG_PAD_LEN;
#else
	return (char *)ptr;
#endif
}

/*
  * This function :  traverse the hash list to get the total count of
  *  memory usage  taken by each module also return the total memory consumed
  * by Advance oprofile
  */
void kdbg_mem_dbg_count()
{
#ifdef KDBG_MEM_CHK
	unsigned int mem_count_elf_module = 0;
	unsigned int mem_count_sampling_buffer_module = 0;
	unsigned int mem_count_report_module = 0;
	unsigned int mem_count_public_interface_module = 0;
	unsigned int mem_count_demangle_module = 0;
	unsigned int mem_count_kdebugd_module = 0;
#ifdef CONFIG_KDEBUGD_FTRACE
	unsigned int mem_count_ftrace_module = 0;
#endif /* CONFIG_KDEBUGD_FTRACE */
#else
	unsigned int total_mem_count = 0;
#endif /* KDBG_MEM_CHK */

	int i = 0;
	struct list_head *h = NULL;
	struct hashtable *htable = NULL;

	if (!kdbg_mem_init_flag) {

		PRINT_KD("Not Yet Initialized memory\n");
		/*!!!BUG */
		BUG_ON(kdbg_mem_dbg_count_hash);
		return;
	}

	/* !!!BUG- INIT Flag is On But Memory Pointer in NULL!! */
	BUG_ON(!kdbg_mem_dbg_count_hash);

	for (i = 0; i < HASH_ARRAY_SIZE; i++) {
		list_for_each(h, &kdbg_mem_dbg_count_hash[i]) {
			htable = list_entry(h, struct hashtable, mem_chain);
			if (!htable) {
				sym_errk("htable is NULL\n");
				break;
			}
#ifdef KDBG_MEM_CHK
			switch (htable->table.module) {
			case KDBG_MEM_ELF_MODULE:
				mem_count_elf_module += htable->table.mem_size;
				break;
			case KDBG_MEM_SAMPLING_BUFFER_MODULE:
				mem_count_sampling_buffer_module +=
				    htable->table.mem_size;
				break;
			case KDBG_MEM_REPORT_MODULE:
				mem_count_report_module +=
				    htable->table.mem_size;
				break;
			case KDBG_MEM_PUBLIC_INTERFACE_MODULE:
				mem_count_public_interface_module +=
				    htable->table.mem_size;
				break;
			case KDBG_MEM_DEMANGLER_MODULE:
				mem_count_demangle_module +=
				    htable->table.mem_size;
				break;
			case KDBG_MEM_KDEBUGD_MODULE:
				mem_count_kdebugd_module +=
				    htable->table.mem_size;
				break;
#ifdef CONFIG_KDEBUGD_FTRACE
			case KDBG_MEM_FTRACE_MODULE:
				mem_count_ftrace_module +=
					htable->table.mem_size;
				break;
#endif /* CONFIG_KDEBUGD_FTRACE */
			default:
				sym_errk
				    ("[INVALID MODULE]DEFAULT :: Size = %d\n",
				     htable->table.mem_size);
				BUG_ON(0);
			}
#else
			total_mem_count += htable->table.mem_size;
#endif
		}
	}
#ifdef KDBG_MEM_CHK
	PRINT_KD("Memory Table For Kdebugd\n");
	PRINT_KD("==================================================\n");
	PRINT_KD("ELF_MODULE = %u Bytes\n", mem_count_elf_module);
	PRINT_KD("SAMPLING_BUFFER_MODULE = %u Bytes\n",
		 mem_count_sampling_buffer_module);
	PRINT_KD("REPORT_MODULE = %u Bytes\n", mem_count_report_module);
	PRINT_KD("PUBLIC_INTERFACE_MODULE = %u Bytes\n",
		 mem_count_public_interface_module);
	PRINT_KD("DEMANGLE_MODULE = %u Bytes\n", mem_count_demangle_module);
	PRINT_KD("KDEBUGD_MODULE = %u Bytes\n", mem_count_kdebugd_module);
#ifdef CONFIG_KDEBUGD_FTRACE
	PRINT_KD("FTRACE_MODULE = %u Bytes\n", mem_count_ftrace_module);
#endif /* CONFIG_KDEBUGD_FTRACE */
	PRINT_KD("==================================================\n");

	PRINT_KD("Total Memory Consumed by Adv Oprofile Module = %u Bytes\n",
		 mem_count_sampling_buffer_module
		 + mem_count_report_module + mem_count_public_interface_module);
	PRINT_KD("Total Memory Consumed by ELF Module = %u Bytes\n",
		 mem_count_elf_module + mem_count_demangle_module);
	PRINT_KD("==================================================\n");
	PRINT_KD("Total Memory Consumed by Kdebugd = %u Bytes\n",
		 mem_count_elf_module
		 + mem_count_sampling_buffer_module
		 + mem_count_report_module
		 + mem_count_public_interface_module
		 + mem_count_demangle_module
		 + mem_count_kdebugd_module
#ifdef CONFIG_KDEBUGD_FTRACE
		+ mem_count_ftrace_module
#endif /* CONFIG_KDEBUGD_FTRACE */
		);
	PRINT_KD("==================================================\n");
#else
	PRINT_KD("==================================================\n");
	PRINT_KD("Total Memory Consumed by Kdebugd = %u Bytes\n",
		 total_mem_count);
	PRINT_KD("==================================================\n");
#endif

	return;
}

#ifdef KDBG_MEM_CHK
/*
  * This fucntion print line number and filename of each module, helps ine
  * debugging the M/m count
  */
void kdbg_mem_dbg_print(void)
{
	int i;
	struct list_head *elem;
	struct hashtable *htable;
	kdbg_mem_dbg_module module = 1;
	int module_found = 0;
	unsigned int total_mem_size = 0;

	if (!kdbg_mem_init_flag) {

		PRINT_KD("Not Yet Initialized memory\n");
		/*!!!BUG */
		BUG_ON(kdbg_mem_dbg_count_hash);
		return;
	}

	/* !!!BUG- INIT Flag is On But Memory Pointer in NULL!! */
	BUG_ON(!kdbg_mem_dbg_count_hash);

	PRINT_KD("Options are:\n");
	PRINT_KD(" 1. KDBG_MEM_ELF_MODULE\n");
	PRINT_KD(" 2. KDBG_MEM_SAMPLING_BUFFER_MODULE\n");
	PRINT_KD(" 3. KDBG_MEM_REPORT_MODULE\n");
	PRINT_KD(" 4. KDBG_MEM_PUBLIC_INTERFACE_MODULE\n");
	PRINT_KD(" 5. KDBG_MEM_DEMANGLE_MODULE\n");
	PRINT_KD(" 6. KDBG_MEM_KDEBUGD_MODULE\n");
#ifdef CONFIG_KDEBUGD_FTRACE
	PRINT_KD(" 7. KDBG_MEM_FTRACE_MODULE\n");
#endif /* CONFIG_KDEBUGD_FTRACE */

	PRINT_KD("\n");
	PRINT_KD("[Adv Oprofile:M/m Usage Module] Option ==>  ");

	module = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD("\n");

	if (module < 0 || module >= KDBG_MEM_MAX_MODULE) {
		PRINT_KD("Invalid Option...\n");
		return;
	}

	/* Search all chains since old address/hash is unknown */
	for (i = 0; i < HASH_ARRAY_SIZE; i++) {
		list_for_each(elem, &kdbg_mem_dbg_count_hash[i]) {
			htable = list_entry(elem, struct hashtable, mem_chain);
			if (!htable) {
				sym_errk("htable is NULL\n");
				break;
			}

			if (htable->table.module == module) {
				PRINT_KD("File Name = %s\t",
					 htable->table.pfile);
				PRINT_KD("Line No = %d\n",
					 htable->table.lineno);
				total_mem_size += htable->table.mem_size;
				module_found = 1;
			}
		}
	}

	PRINT_KD("==================================================\n");
	if (!module_found) {
		PRINT_KD("Total Memory Consumed by Module = 0 Bytes\n");
	} else {
		PRINT_KD("Total Memory Consumed by Module = %d Bytes\n",
			 total_mem_size);
	}
	PRINT_KD("==================================================\n");

}
#endif
/*
FUNCTION NAME	 	:	kdbg_mem_dbg_kdmenu
DESCRIPTION			:	main entry routine for the Mem Debug
ARGUMENTS			:	option , File Name
RETURN VALUE	 	:	0 for success
AUTHOR			 	:	Gaurav Jindal
**********************************************/
int kdbg_mem_dbg_kdmenu(void)
{
	int operation = 0;
	int ret = 1;

	do {
		if (ret) {
			PRINT_KD("\n");
			PRINT_KD("Options are:\n");
			PRINT_KD
				("------------------------------------------------"
				 "--------------------\n");
			PRINT_KD(" 1. Kdebugd Memory Count(Internal Test)\n");
			PRINT_KD(" 2. Kdebugd Shutdown(Internal Test)\n");
#ifdef KDBG_MEM_CHK
			PRINT_KD
				(" 3. Kdebugd Module Memory Usage(Internal Check)\n");
#endif
			PRINT_KD
				("------------------------------------------------"
				 "--------------------\n");
			PRINT_KD(" 99 Mem DBG : Exit Menu\n");
			PRINT_KD
				("------------------------------------------------"
				 "--------------------\n");
			PRINT_KD("[Mem DBG] Option ==>  ");
		}

		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

			switch (operation) {
			case 1:
				kdbg_mem_dbg_count();
				break;
			case 2:
				PRINT_KD
					("*** Warning: Destroying Adv Oprofile internal structure\n");
				PRINT_KD
					("*** Warning: Please do not use oprofile, as it is done for internal Testing\n");
				PRINT_KD
					("*** Warning: Please use only option - ELF: Symbol (ELF) Menu\n");
#if defined(CONFIG_KDEBUGD_MISC) && defined(CONFIG_SCHED_HISTORY)
				destroy_sched_history();
#endif /* defined(CONFIG_KDEBUGD_MISC) && defined(CONFIG_SCHED_HISTORY) */

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
				sec_cpuusage_off();
				sec_diskusage_destroy();
				sec_netusage_destroy();
				sec_topthread_off();
				sec_memusage_destroy();
#endif /* CONFIG_KDEBUGD_COUNTER_MONITOR */

#ifdef CONFIG_KDEBUGD_FTRACE
				kdbg_ftrace_exit();
#endif /* CONFIG_KDEBUGD_FTRACE */

#ifdef CONFIG_ADVANCE_OPROFILE
				aop_shutdown();
#elif defined(CONFIG_ELF_MODULE)
				kdbg_elf_sym_delete();
#endif /* CONFIG_ELF_OPROFILE */
				remove_mem_cache();
				kdbg_mem_dbg_count();
				break;
#ifdef KDBG_MEM_CHK
			case 3:
				kdbg_mem_dbg_print();
				break;
#endif /* KDBG_MEM_CHK */
			case 99:
				/* Mem DBG Menu Exit */
				break;

			default:
				PRINT_KD("Mem debug invalid option....\n");
				ret = 1;	/* to show menu */
				break;
		}
	} while (operation != 99);

	PRINT_KD("MEM DBG menu exit....\n");
	/* as this return value is mean to show or not show the kdebugd menu options */
	return ret;
}

/*
 * Mem DBG  Module init function, which initialize Mem DBG Module and start functions
  * and allocate kdbg mem module (Hash table)
  */
int kdbg_mem_init(void)
{
	/* Kdbg Mem Dbg menu options */
	kdbg_register("MEM DBG: Memory Debug",
		      kdbg_mem_dbg_kdmenu, NULL, KDBG_MENU_MEM_DBG);

	return 0;
}
