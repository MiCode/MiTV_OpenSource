/*
 * kdbg_ftrace_output_helper.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

/* Include in trace_output.c */
#include <kdebugd.h>
#include "kdbg_util.h"
#include <trace/kdbg_ftrace_helper.h>

#ifdef CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif

int kdbg_ftrace_seq_print_user_ip(struct trace_seq *s, unsigned int ip, const struct userstack_entry *entry, int call_depth)
{
	int ret = 1;
#ifdef CONFIG_ELF_MODULE
	char *sym_name = NULL, *lib_name = NULL;
	struct aop_symbol_info symbol_info;
	unsigned int start_addr = 0;
#ifdef CONFIG_DWARF_MODULE
	static struct aop_df_info df_info;
#endif
#endif

	/* return if trace_seq is full */
	if (s->full)
		return 0;

#ifndef CONFIG_ELF_MODULE
	trace_seq_printf(s, "#%d  0x%08x in ?? ()", call_depth, ip);
#else
	/* allocate the sym_name and lib_name */
	sym_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
			KDBG_ELF_SYM_NAME_LENGTH_MAX,
			GFP_KERNEL);
	if (!sym_name)
		return 0;

	lib_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
			KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN,
			GFP_KERNEL);
	if (!lib_name) {
		if (sym_name)
			KDBG_MEM_DBG_KFREE(sym_name);
		return 0;
	}

	/* load the elf database for particular pid*/
	kdbg_elf_load_elf_db_by_pids((const pid_t *)&entry->tgid, 1);

	symbol_info.pfunc_name = sym_name;
#ifdef CONFIG_DWARF_MODULE
	symbol_info.df_info_flag = 1;
	symbol_info.pdf_info = &df_info;
#endif

	/* get symbol and library name */
	if (kdbg_elf_get_symbol_and_lib_by_pid(entry->tgid,
				ip, lib_name, &symbol_info,
				&start_addr)) {
#ifdef CONFIG_DWARF_MODULE
		if (symbol_info.pdf_info->df_line_no != 0)
			trace_seq_printf(s, "#%d  0x%08x in %s () at %s:%d", call_depth,
					ip, symbol_info.pfunc_name,
					symbol_info.pdf_info->df_file_name,
					symbol_info.pdf_info->df_line_no);
		else
			trace_seq_printf(s, "#%d  0x%08x in %s () from %s", call_depth,
					ip, sym_name, lib_name);
		symbol_info.pdf_info = NULL;
#else
		trace_seq_printf(s, "#%d  0x%08x in %s () from %s", call_depth, ip,
				sym_name, lib_name);
#endif
	} else {
		trace_seq_printf(s, "#%d  0x%08x in ?? ()", call_depth, ip);
	}

	/* free the sym_name and lib_name */
	if (sym_name)
		KDBG_MEM_DBG_KFREE(sym_name);
	if (lib_name)
		KDBG_MEM_DBG_KFREE(lib_name);
#endif

	return ret;
}


