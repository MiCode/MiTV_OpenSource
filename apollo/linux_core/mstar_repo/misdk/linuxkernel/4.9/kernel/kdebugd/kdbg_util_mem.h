/*
 *  kernel/kdebugd/aop/kdbg_util_mem.h
 *
 *  Advance oprofile related declarations
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010.03.05 Created by gaurav.j
 *
 */

#ifndef _LINUX_KDBG_MEM_DBG_H
#define _LINUX_KDBG_MEM_DBG_H

#include <linux/list.h>
#include <linux/vmalloc.h>

enum {
	KDBG_MEM_DEFAULT_MODE = 0,	/* default mode */
	KDBG_MEM_CHECK_MODE,
};

typedef enum {
	KDBG_MEM_VFREE_MODE,
	KDBG_MEM_KFREE_MODE,
	KDBG_MEM_VMALLOC_MODE,
	KDBG_MEM_KMALLOC_MODE,
	KDBG_MEM_KREALLOC_MODE
} kdbg_mem_dbg_handle_mode;

typedef enum {
	KDBG_MEM_ELF_MODULE = 1,
	KDBG_MEM_SAMPLING_BUFFER_MODULE,
	KDBG_MEM_REPORT_MODULE,
	KDBG_MEM_PUBLIC_INTERFACE_MODULE,
	KDBG_MEM_DEMANGLER_MODULE,
	KDBG_MEM_KDEBUGD_MODULE,
#ifdef CONFIG_KDEBUGD_FTRACE
	KDBG_MEM_FTRACE_MODULE,
#endif /* CONFIG_KDEBUGD_FTRACE */
	KDBG_MEM_MAX_MODULE
} kdbg_mem_dbg_module;

struct record {
	size_t mem_size;
	void *ptr_addr;
#ifdef KDBG_MEM_CHK
	kdbg_mem_dbg_handle_mode alloc_mode;
	kdbg_mem_dbg_module module;
	const char *pfile;
	int lineno;
#endif
};

struct hashtable {
	struct list_head mem_chain;
	struct record table;
};

int kdbg_mem_dbg_free_(kdbg_mem_dbg_handle_mode mode, void *ptr,
		       const char *pfile, int line, int check);

void *kdbg_mem_dbg_alloc_(kdbg_mem_dbg_module module,
			  kdbg_mem_dbg_handle_mode mode, const void *p,
			  size_t size, unsigned int flag, const char *pfile,
			  int line);

void kdbg_mem_dbg_count(void);

void kdbg_mem_dbg_print(void);

/*
 * Mem DBG  Module init function, which initialize Mem DBG Module and start functions
  * and allocate kdbg mem module (Hash table)
  */
int kdbg_mem_init(void);

#endif /* _LINUX_KDBG_MEM_DBG_H */
