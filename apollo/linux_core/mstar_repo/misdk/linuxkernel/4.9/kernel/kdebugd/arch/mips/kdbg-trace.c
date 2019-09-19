/*
 * architecture specific file for backtrace
 */

#include <asm/uaccess.h>
#include <linux/mm.h>

#ifdef CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif
#include "kdbg-trace.h"

/*#define DEBUG */
#ifdef DEBUG
#define dprintk(x...) PRINT_KD(x)
#else
#define dprintk(x...)
#endif

#define CONFIG_CALL_DEPTH	(20)

#ifdef CONFIG_ELF_MODULE
static pid_t user_backtrace_tsk_pid;
#endif

/* use this variable to suppress extra prints from backtrace functions
 * (that are not required outside Kdebugd trace)
 */
int suppress_extra_prints;

/*
 * store_user_bt
 * This function is architecture specific wrapper to user_bt for storing
 * the user backtrace in an array with specified call depth.
 */
inline int store_user_bt(struct kbdg_bt_trace *trace, unsigned int depth)
{
	struct pt_regs *regs = task_pt_regs(current);
	struct kdbgd_bt_regs bt_regs = {regs->regs[30],
		regs->cp0_epc,
		regs->regs[31],
		regs->regs[29],
		current->user_ssp};

	if (!trace) {
		dprintk("Error: trace %p\n", trace);
		return 0;
	}

	/* Debug print for depth check because Ftrace and OProfile
	 * strictly avoids any PRINT_KD.
	 */
	if (!depth) {
		dprintk("Given depth %u, taking default depth = 2\n", depth);
		depth = 2;
	}

	return user_bt(&bt_regs, current->mm, trace, depth);
}

/*
 * Function Name : user_bt
 * This function use the prologue instruction to get the backtrace
 * of the user space task.
 * struct stack_trace* and depth added for storing addreses in case
 * of Ftrace and OProfile
 */
unsigned int
user_bt(struct kdbgd_bt_regs *tsk_regs, struct mm_struct *tsk_mm,
	struct kbdg_bt_trace *trace, unsigned int depth)
{
	unsigned int temp_addr = 0;
	unsigned int inst, high_word;
	unsigned int stack_size = 0, ra_offset = 0;
	unsigned int alloca_adjust = 0, fp_offset = 0, next_fp_address = 0;
	unsigned int old_gcc_fp_adjust = 0, new_gcc_fp_adjust = 0;
	long low_word;
	int reg, adjust = 0;
	int first = 1;
	unsigned int call_depth = 0;
	unsigned int start_addr = 0, jump = 0;
	unsigned int addr = 0, sp, ra, end_address, fp_address = 0, last_ra = 0;
	int first_inst = 1;
	int inst_ctr = 0;

#ifdef CONFIG_ELF_MODULE
	struct aop_symbol_info symbol_info;
	char *sym_name = NULL, *lib_name = NULL;
#ifdef CONFIG_DWARF_MODULE
	static struct aop_df_info df_info;
#endif
#endif /* CONFIG_ELF_MODULE */

	/* Debug print for depth check because Ftrace and OProfile
	 * strictly avoids any PRINT_KD.
	 */
	if (!depth) {
		dprintk("Given depth %u, taking default depth = 2\n", depth);
		depth = 2;
	}

	if (!trace && (!tsk_regs || !tsk_mm)) {
		PRINT_KD("Error: tsk_regs: %p, tsk_mm: %p\n", tsk_regs, tsk_mm);
		goto exit_free;
	}

#ifdef CONFIG_ELF_MODULE
	if (!trace) {
		sym_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				KDBG_ELF_SYM_NAME_LENGTH_MAX,
				GFP_KERNEL);
		if (!sym_name)
			goto exit_free;

		lib_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN,
				GFP_KERNEL);
		if (!lib_name)
			goto exit_free;
	}
#endif /* CONFIG_ELF_MODULE */

	addr = tsk_regs->pc;
	sp = tsk_regs->sp;
	ra = tsk_regs->lr;
	fp_address = tsk_regs->fp;
	end_address = tsk_regs->sp_end;

	if (trace) {
		__save_user_trace(trace, addr);
	} else {

#ifdef CONFIG_ELF_MODULE
	symbol_info.pfunc_name = sym_name;
#ifdef CONFIG_DWARF_MODULE
	symbol_info.df_info_flag = 1;
	symbol_info.pdf_info = &df_info;
#endif /* CONFIG_DWARF_MODULE */
	if (kdbg_elf_get_symbol_and_lib_by_pid(user_backtrace_tsk_pid,
					       addr, lib_name, &symbol_info,
					       &start_addr)) {
#ifdef CONFIG_DWARF_MODULE
		if (symbol_info.pdf_info->df_line_no != 0)
			PRINT_KD("#%d  0x%08x in %s () at %s:%d\n", call_depth,
				 addr, symbol_info.pfunc_name,
				 symbol_info.pdf_info->df_file_name,
				 symbol_info.pdf_info->df_line_no);
		else
			PRINT_KD("#%d  0x%08x in %s () from %s\n", call_depth,
				 addr, sym_name, lib_name);
		symbol_info.pdf_info = NULL;

#else
		PRINT_KD("#%d  0x%08x in %s () from %s\n", call_depth, addr,
			 sym_name, lib_name);
#endif
	} else {
		PRINT_KD("#%d  0x%08x in ?? ()\n", call_depth, addr);
	}

	if ((strcmp(sym_name, "main") == 0) ||
			(strcmp(sym_name, "__thread_start") == 0) ||
			(strcmp(sym_name, "__libc_start_main") == 0)) {
		goto exit_stop_bt;
	}

#else
	PRINT_KD("#%d  0x%08x in ?? ()\n", call_depth, addr);
#endif

	}
	call_depth++;

	if (depth == call_depth) {
		if (trace) {
			dprintk("Stack trace full, depth: %u\n", depth);
			return depth;
		} else
			goto exit_stop_bt;
	}

	while (sp < end_address) {
		/* Fetch the instruction.   */
		inst = get_user_value(tsk_mm, addr);
		dprintk("Instruction %08x (at %08x) sp %08x\n", inst, addr, sp);
		if (inst == KDEBUGD_BT_INVALID_VAL)
			goto exit_stop_bt;

		/* Save some code by pre-extracting some useful fields.  */
		high_word = (inst >> 16) & 0xffff;
		low_word = inst & 0xffff;
		reg = high_word & 0x1f;

		if (unlikely(first_inst)) {
			first_inst = 0;

			/* addiu $sp,$sp,-i */
			/* addi $sp,$sp,-i */
			/* daddiu $sp,$sp,-i */
			if (high_word == 0x27bd
					|| high_word == 0x23bd || high_word == 0x67bd) {
				/* negative stack adjustment */
				if ((low_word & 0x8000) && (start_addr == 0))
					jump = 1;
			}
			dprintk("First instruction: %08x, so skipped!!!\n", inst);
		}

		/* addiu $sp,$sp,-i */
		/* addi $sp,$sp,-i */
		/* daddiu $sp,$sp,-i */
		else if (high_word == 0x27bd
				|| high_word == 0x23bd || high_word == 0x67bd) {
			dprintk("inst %08x--->%d\n", (unsigned int)inst,
				__LINE__);
			/* negative stack adjustment */
			if (low_word & 0x8000) {
				stack_size = abs((short)low_word);
			} else {
				dprintk("Continue\n");	/* Skip, if a positive stack adjustment is found */
				addr -= 4;
				continue;
			}

			if (start_addr == 0)
				jump = 1;
		} else if (high_word == 0xafbf	/* sw ra,offset($sp) */
			   || high_word == 0xffbf) {	/* sd reg,offset($sp) */
			ra_offset = low_word;
		} else if (inst == 0x03a3e823) {	/* subu sp,sp,vi */
			temp_addr = addr;
			while (high_word != 0x3403
			       && (unsigned long)temp_addr > start_addr) {
				temp_addr -= 4;
				inst = get_user_value(tsk_mm, temp_addr);
				high_word = (inst >> 16) & 0xffff;
				low_word = inst & 0xffff;
			}
			if (high_word == 0x3403) {
				dprintk("li inst found %08x\n",
					(unsigned int)low_word);
				sp = sp + low_word;
				adjust = 1;
			}
		}
		/* move $30,$sp */
		else if (inst == 0x03a0f021) {
			new_gcc_fp_adjust = 1;
			dprintk("new_gcc_fp_adjust %08x\n",
				(unsigned int)new_gcc_fp_adjust);
		}
		/* addiu $30,$sp,size */
		else if (high_word == 0x27be) {
			old_gcc_fp_adjust = low_word;
			fp_address = fp_address - old_gcc_fp_adjust;
			dprintk("old_gcc_fp_adjust %08x\n",
				(unsigned int)old_gcc_fp_adjust);
		} else if (high_word == 0xafbe) {	/* sw s8,offset($sp) */
			dprintk("fp_address %08x\n", (unsigned int)fp_address);
			if (old_gcc_fp_adjust || new_gcc_fp_adjust) {

				if (first) {
					sp = fp_address;
					first = 0;
				}
				if (!adjust) {
					if (!first && fp_address && !temp_addr) {
						alloca_adjust = fp_address - sp;
						dprintk("alloca_adjust  %08x\n",
							(unsigned int)
							alloca_adjust);
						if (alloca_adjust)
							sp = sp + alloca_adjust;
					}
				} else {
					adjust = 0;
				}
				fp_offset = low_word;
				next_fp_address =
				    get_user_value(tsk_mm, sp + fp_offset);
				dprintk
				    ("sp %08x, fp_offset %08x, next_fp_address %08x\n",
				     sp, fp_offset, next_fp_address);
				fp_offset = 0;

				if (next_fp_address) {
					fp_address = next_fp_address;
					dprintk("fp_address %08x\n",
						(unsigned int)fp_address);
					next_fp_address = 0;
				}
				new_gcc_fp_adjust = old_gcc_fp_adjust = 0;

			} else {
				fp_offset = low_word;
				fp_address =
				    get_user_value(tsk_mm, sp + fp_offset);
				dprintk("fp_address %08x\n",
					(unsigned int)fp_address);
			}
		} else {
			dprintk("Instruction: %08x skipped...\n", inst);
		}

		/* Skip the rest instructions */
		addr -= 4; /* addr is decreased by 4 as MIPS has 32 bit instruction */

		/*
		 * Special case for Ftrace/Oprofile APIs
		 * Test and increment the counter for KDEBUGD_BT_MAX_INSNS_PARSED
		 */
		if (trace && (++inst_ctr > KDEBUGD_BT_MAX_INSNS_PARSED)) {
			dprintk("Error: inst_ctr(%d) > KDEBUGD_BT_MAX_INSNS_PARSED(%d)\n",
					inst_ctr, KDEBUGD_BT_MAX_INSNS_PARSED);
			goto exit_stop_bt;
		}

		if ((((unsigned int)addr < start_addr) || jump)) {

			if (ra_offset) {
				ra = get_user_value(tsk_mm, sp + ra_offset);
				dprintk("sp %08x, ra_offset %08x, ra %08x\n",
					sp, ra_offset, ra);
			}

			if (stack_size) {
				dprintk("stack_size %d\n", stack_size);
				sp = sp + stack_size;

			}

			stack_size = 0;
			ra_offset = 0;

			/* jump to calling function */
			addr = ra - 4;

			/* if found last function or stack has corrupt
			 * and getting same lr again. come out from bt
			 */
			if (ra == last_ra)
				goto exit_stop_bt;

			if (trace) {
				__save_user_trace(trace, addr);
				if (call_depth == (depth - 1)) {
					dprintk("Stack trace full, depth: %u\n", depth);
					return depth;
				}
			} else {

#ifdef CONFIG_ELF_MODULE
			symbol_info.pfunc_name = sym_name;
#ifdef CONFIG_DWARF_MODULE
			symbol_info.df_info_flag = 1;
			/* No need to check for poniter existence already take care inside the function */
			symbol_info.pdf_info = &df_info;
#endif /* CONFIG_DWARF_MODULE */
			if (kdbg_elf_get_symbol_and_lib_by_pid
			    (user_backtrace_tsk_pid, addr, lib_name,
			     &symbol_info, &start_addr)) {
#ifdef CONFIG_DWARF_MODULE
				if (symbol_info.pdf_info->df_line_no != 0)
					PRINT_KD
					    ("#%d  0x%08x in %s () at %s:%d\n",
					     call_depth, addr,
					     symbol_info.pfunc_name,
					     symbol_info.pdf_info->df_file_name,
					     symbol_info.pdf_info->df_line_no);
				else
					PRINT_KD
					    ("#%d  0x%08x in %s () from %s\n",
					     call_depth, ra, sym_name,
					     lib_name);
				symbol_info.pdf_info = NULL;

#else
				PRINT_KD("#%d  0x%08x in %s () from %s\n",
					 call_depth, ra, sym_name, lib_name);
#endif /* CONFIG_DWARF_MODULE */
			} else {
				start_addr = 0;
				PRINT_KD("#%d  0x%08x in ?? ()\n", call_depth,
					 ra);
			}

			if ((strcmp(sym_name, "main") == 0) ||
				(strcmp(sym_name, "__thread_start") == 0) ||
				(strcmp(sym_name, "__libc_start_main") == 0)) {
				goto exit_stop_bt;
			}
#else
			PRINT_KD("#%d  0x%08x in ?? ()\n", call_depth,
				 (unsigned int)addr);
#endif
			}

			dprintk
			    ("addr %08x, ra %08x, sp %08x, end %08x, fp %08x\n",
			     addr, ra, sp, end_address, fp_address);

			last_ra = ra;
			jump = 0;

			call_depth++;
			/* limit call depth */
			if (call_depth > depth) {
				PRINT_KD("call_depth %d\n", call_depth);
				goto exit_stop_bt;

			}
		}
	}

exit_stop_bt:
	if (!trace) {
	if (!suppress_extra_prints)
		PRINT_KD("Backtrace Stop\n");
	}

exit_free:
#ifdef CONFIG_ELF_MODULE
	if (!trace) {
	if (sym_name)
		KDBG_MEM_DBG_KFREE(sym_name);
	if (lib_name)
		KDBG_MEM_DBG_KFREE(lib_name);
	}
#endif /* CONFIG_ELF_MODULE */
	return call_depth;
}

/*
 * Function Name: show_user_stack_backtrace
 * This is the startup function for backtrace. It get pt_regs value from the task and load the elf
 * for that task pid and pass pt_regs  value to function to start backtrace.
 * Dump backtrace(User) w/ pid,
 */
void show_user_backtrace_task(struct task_struct *tsk, int load_elf)
{
	struct pt_regs *regs;
	struct vm_area_struct *vma;
	struct mm_struct *tsk_mm;
	unsigned long tsk_user_ssp;
	pid_t tsk_pid;
	struct kdbgd_bt_regs tsk_regs;

	BUG_ON(!tsk);		/* validity should be insured in caller */

	PRINT_KD("\n\nPid: %d, comm: %20s", tsk->pid, tsk->comm);
#ifdef CONFIG_SMP
	PRINT_KD("[%d]", task_cpu(tsk));
#endif
	PRINT_KD("\n");

	task_lock(tsk);
	regs = task_pt_regs(tsk);
	tsk_mm = tsk->mm;

	tsk_user_ssp = tsk->user_ssp;
	tsk_pid = tsk->pid;

	task_unlock(tsk);

	vma = find_vma(tsk_mm, tsk_user_ssp);

	if (vma) {
#ifdef CONFIG_ELF_MODULE
		user_backtrace_tsk_pid = tsk_pid;
		if (load_elf) {
			kdbg_elf_load_elf_db_by_pids(&user_backtrace_tsk_pid,
						     1);
		}
#endif /* CONFIG_ELF_MODULE */

#ifdef DEBUG
		PRINT_KD("\n");
		PRINT_KD
		    ("================================================================================");
		PRINT_KD("Stack area(0x%lx ~ 0x%lx)", regs->regs[29],
			 tsk_user_ssp);
		PRINT_KD
		    ("================================================================================\n");
		kdbg_dump_mem("dump user stack", regs->regs[29], tsk_user_ssp);
#endif

		/* set up the registers for user_bt */
		tsk_regs.fp = regs->regs[30];
		tsk_regs.pc = regs->cp0_epc;
		tsk_regs.lr = regs->regs[31];
		tsk_regs.sp = regs->regs[29];
		tsk_regs.sp_end = tsk_user_ssp;

		if (!suppress_extra_prints) {
			PRINT_KD
			    ("===============================================================================\n");
			PRINT_KD("kdebugd back trace\n");
			PRINT_KD
			    ("===============================================================================\n");
			PRINT_KD("REGS: FP: 0x%08x, PC: 0x%08x, RA: 0x%08x,"
				 "SP: 0x%08x, Stack End: 0x%08x\n", tsk_regs.fp,
				 tsk_regs.pc, tsk_regs.lr, tsk_regs.sp,
				 tsk_regs.sp_end);
		}

		user_bt(&tsk_regs, tsk_mm, NULL, CONFIG_CALL_DEPTH);
		PRINT_KD
		    ("===============================================================================\n");
	}
}
