/*
 *  kernel/kdebugd/kdbg_util.h
 *
 *  Advance oprofile related functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010.03.05  Created by gaurav.j
 *
 */

#ifndef _LINUX_KDBG_UTIL_H
#define _LINUX_KDBG_UTIL_H

#include <linux/list.h>
#include <linux/slab.h>

#ifdef KDBG_MEM_DBG
#include "kdbg_util_mem.h"
#endif

#ifdef CONFIG_KDEBUGD_LIFE_TEST
#include "kdbg_key_test_player.h"
#endif

extern struct proc_dir_entry *kdebugd_dir;

/*Init functions*/
int kdbg_trace_init(void);
int kdbg_cpuusage_init(void);
int kdbg_diskusage_init(void);
int kdbg_memusage_init(void);
int kdbg_netusage_init(void);
int kdbg_topthread_init(void);
bool init_sched_history(void);
int kdbg_futex_init(void);
#ifdef CONFIG_KDEBUGD_FTRACE
int kdbg_ftrace_init(void);
void kdbg_ftrace_exit(void);
#endif
int kdbg_usage_core_init(void);
void destroy_sched_history(void);

struct task_struct *get_task_with_pid(void);

void show_user_bt(struct task_struct *tsk);

extern void show_user_stack(struct task_struct *task, struct pt_regs *regs);
extern int get_user_stack(struct task_struct *task,
			  unsigned int **us_content, unsigned long *start,
			  unsigned long *end);

/* Debugging Memory Leak */
#ifdef KDBG_MEM_DBG
#define KDBG_MEM_DBG_KMALLOC(a, b, c)		kdbg_mem_dbg_alloc_(a, KDBG_MEM_KMALLOC_MODE, NULL, b, c, __FILE__, __LINE__)
#define KDBG_MEM_DBG_KREALLOC(a, b, c, d)	kdbg_mem_dbg_alloc_(a, KDBG_MEM_KREALLOC_MODE, b, c, d, __FILE__, __LINE__)
#define KDBG_MEM_DBG_VMALLOC(a, b)		kdbg_mem_dbg_alloc_(a, KDBG_MEM_VMALLOC_MODE, NULL, b, 0, __FILE__, __LINE__)
#define KDBG_MEM_DBG_VFREE(a)			kdbg_mem_dbg_free_(KDBG_MEM_VFREE_MODE, a, __FILE__, __LINE__, KDBG_MEM_CHECK_MODE)
#define KDBG_MEM_DBG_KFREE(a)			kdbg_mem_dbg_free_(KDBG_MEM_KFREE_MODE, a, __FILE__, __LINE__, KDBG_MEM_CHECK_MODE)
#else
#define KDBG_MEM_DBG_KMALLOC(a, b, c)		kmalloc(b, c)
#define KDBG_MEM_DBG_KREALLOC(a, b, c, d) 	krealloc(b, c, d)
#define KDBG_MEM_DBG_VMALLOC(a, b)  		vmalloc(b)
#define KDBG_MEM_DBG_VFREE(a)			vfree(a)
#define KDBG_MEM_DBG_KFREE(a)			kfree(a)
#endif

/*maximum length of the name of the directory.*/
#define AOP_MAX_SYM_NAME_LENGTH 1024

#ifdef CONFIG_DWARF_MODULE
#define AOP_DF_MAX_FILENAME   256

struct aop_df_info {
	char df_file_name[AOP_DF_MAX_FILENAME];
	unsigned int df_line_no;
};
#endif /* CONFIG_DWARF_MODULE */

/* structure for symbol info */
/* Dwarf: structure for symbol info */
struct aop_symbol_info {
	char *pfunc_name;	/* function name of containing symbol */
	unsigned int start_addr;	/* start address of function  */
	unsigned int virt_addr;	/* virtual address of function  */
#ifdef CONFIG_DWARF_MODULE
	unsigned int df_info_flag;
	struct aop_df_info *pdf_info;
#endif				/* CONFIG_DWARF_MODULE */
};

/* aop sort doubly linked list compare function prototype */
typedef int (*aop_cmp) (const struct list_head *a, const struct list_head *b);

/*
 *  Link list  sorting in ascending order
 */
void aop_list_sort(struct list_head *head, aop_cmp cmp);

#endif /* !_LINUX_KDBG_UTIL_H */
