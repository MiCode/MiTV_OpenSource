#ifndef _KDBG_TRACE_H_
#define _KDBG_TRACE_H_

#include <linux/stacktrace.h>
#include <asm/stacktrace.h>
#include <linux/hardirq.h>
#include "kdebugd.h"

#define KDEBUGD_BT_INVALID_VAL 0xffffffff
#define KDEBUGD_BT_MAX_INSNS_PARSED 	(1500)

/* placeholder for the arch registers */
struct kdbgd_bt_regs {
	unsigned int fp;	/* current frame pointer */
	unsigned int pc;	/* current instruction pointer */
	unsigned int lr;	/* current return address */
	unsigned int sp;	/* current stack pointer */
	unsigned int sp_end;	/* The limit of stack address */
};

struct kbdg_bt_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
	int skip;       /* input argument: How many entries to skip */
};

unsigned int get_user_value(struct mm_struct *bt_mm,
		unsigned long addr);

void __save_user_trace(struct kbdg_bt_trace *trace, unsigned long val);

int store_user_bt(struct kbdg_bt_trace *trace, unsigned int depth);

unsigned int
user_bt(struct kdbgd_bt_regs *tsk_regs, struct mm_struct *tsk_mm,
		struct kbdg_bt_trace *trace, unsigned int depth);

#endif /* _KDBG_TRACE_H_ */
