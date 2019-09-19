/*
 * kdbg_ftrace_stack_helper.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

/* Include in trace_stack.c */
#include <kdebugd.h>
#include "kdbg_util.h"
#include <trace/kdbg_ftrace_helper.h>

int kdbg_ftrace_stack_dump(struct kdbg_ftrace_conf *pfconf)
{
	long i = 0;
	int size = 0;
	unsigned long addr = 0;
	unsigned long flags;
	int event = 0;

	local_irq_save(flags);
	arch_spin_lock(&max_stack_lock);

	if (!max_stack_trace.nr_entries) {
		info("Trace Buffer Empty.\n");
		arch_spin_unlock(&max_stack_lock);
		local_irq_restore(flags);
		return -1;
	}

	menu("        Depth    Size   Location"
			"    (%d entries)\n"
			"        -----    ----   --------\n",
			max_stack_trace.nr_entries - 1);


	while (i < max_stack_trace.nr_entries) {

		if (stack_dump_trace[i] == ULONG_MAX)
			break;

		if (i+1 == max_stack_trace.nr_entries || stack_dump_trace[i+1] == ULONG_MAX)
			size = stack_dump_index[i];
		else
			size = stack_dump_index[i] - stack_dump_index[i+1];

		menu("%3ld) %8d   %5d   ", i, stack_dump_index[i], size);

		addr = stack_dump_trace[i];
		menu("%pF\n", (void *)addr);

		i++;

		if (pfconf->trace_lines_per_print &&
				(i % pfconf->trace_lines_per_print == 0)) {
			menu("Press 99 To Stop Trace Dump, Else Continue.. ");
			event = debugd_get_event_as_numeric(NULL, NULL);
			menu("\n");
			if (event == 99)
				break;
		}
	}

	arch_spin_unlock(&max_stack_lock);
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_stack_dump);

unsigned long kdbg_ftrace_get_trace_max_stack(void)
{
	return max_stack_size;
}
EXPORT_SYMBOL(kdbg_ftrace_get_trace_max_stack);

void kdbg_ftrace_set_trace_max_stack(unsigned long val)
{
	unsigned long flags;
	int cpu;

	local_irq_save(flags);

	/*
	 * In case we trace inside arch_spin_lock() or after (NMI),
	 * we will cause circular lock, so we also need to increase
	 * the percpu trace_active here.
	 */
	cpu = smp_processor_id();
	per_cpu(trace_active, cpu)++;

	arch_spin_lock(&max_stack_lock);
	max_stack_size  = val;
	arch_spin_unlock(&max_stack_lock);

	per_cpu(trace_active, cpu)--;
	local_irq_restore(flags);

}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_max_stack);
