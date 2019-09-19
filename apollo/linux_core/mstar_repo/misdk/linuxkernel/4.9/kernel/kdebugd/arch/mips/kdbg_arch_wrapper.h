/*
 *  kernel/kdebugd/arch/arm/kdbg_arch_wrapper.h
 *
 *  Kdebugd arch wrapper
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-07-21  Created by gaurav.j@samsung.com
 *
 */

#ifndef __KDBG_ARCH_WRAPPER_H_
#define __KDBG_ARCH_WRAPPER_H_

/********************************************************************
  INCLUDE FILES
 ********************************************************************/
#include <kdebugd.h>

inline void show_pid_maps_wr(struct task_struct *tsk)
{
	dump_pid_maps(tsk);
}

inline void show_regs_wr(struct pt_regs *regs)
{
	show_regs(regs);
}

#endif /* !__KDBG_ARCH_WRAPPER_H_ */
