/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_IRQ_H
#define __ASM_IRQ_H

#ifndef __ASSEMBLER__

#include <asm-generic/irq.h>

struct pt_regs;

#if defined(CONFIG_SMP) && defined(CONFIG_MP_DEBUG_TOOL_SYSRQ)
extern void arch_trigger_cpumask_backtrace(const cpumask_t *mask,
                                                   bool exclude_self);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace
#endif

static inline int nr_legacy_irqs(void)
{
	return 0;
}

#endif /* !__ASSEMBLER__ */
#endif
