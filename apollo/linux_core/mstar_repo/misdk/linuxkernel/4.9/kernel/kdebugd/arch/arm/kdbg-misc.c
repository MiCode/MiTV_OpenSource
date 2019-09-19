/*
 * kdbg-misc.c
 *
 * Copyright (C) 2009 Samsung Electronics
 *
 * Created by gaurav.j@samsung.com
 *
 * 2009-09-09 : Arch Specific Misc.
 * NOTE:
 *
 */

#include <kdebugd.h>
#include "../../kdbg-trace.h"
#include <linux/oprofile.h>

void kdbg_unwind_mem_stack_kernel(const char *str, unsigned long bottom,
				  unsigned long top)
{
	unsigned long p = bottom & ~31;
	int i;

	PRINT_KD("%s\n", str);

	for (p = bottom & ~31; p <= top;) {

		for (i = 0; i < 8; i++, p += 4) {

			if (p < bottom || p > top)
				PRINT_KD("         ");
			else
				PRINT_KD("%08lx ", *(unsigned long *)p);
		}
		PRINT_KD("\n");
	}
}

int kdbg_elf_chk_machine_type(Elf32_Ehdr Ehdr)
{
	if (Ehdr.e_machine == EM_ARM)
		return 0;
	else
		return -ENXIO;
}


#define KDBG_AOP_TRACE_ENTRIES 3

void kdbg_aop_user_backtrace(struct pt_regs * const regs, unsigned int depth)
{
        #if defined(CONFIG_ADVANCE_OPROFILE) && (MP_DEBUG_TOOL_OPROFILE == 1)
        int idx = 0;
        #endif

	struct kdbgd_bt_regs bt_regs = {regs->ARM_fp,
									regs->ARM_pc,
									regs->ARM_lr,
									regs->ARM_sp,
									current->user_ssp};
	unsigned long stack_dump_trace[KDBG_AOP_TRACE_ENTRIES+1] = {
			[0 ... (KDBG_AOP_TRACE_ENTRIES)] = ULONG_MAX };
	struct kbdg_bt_trace max_stack_trace = {
		.max_entries  = KDBG_AOP_TRACE_ENTRIES,
		.entries        	= stack_dump_trace,
		.nr_entries	= 0,
	};
	unsigned int ret = 0;



	ret = user_bt(&bt_regs, current->mm, &max_stack_trace, depth);
#if defined(CONFIG_ADVANCE_OPROFILE) && (MP_DEBUG_TOOL_OPROFILE == 1)
	while (ret--) {
		oprofile_add_trace(max_stack_trace.entries[idx++]);
	}
#endif /*CONFIG_ADVANCE_OPROFILE && MP_DEBUG_TOOL_OPROFILE*/
	return;
}

