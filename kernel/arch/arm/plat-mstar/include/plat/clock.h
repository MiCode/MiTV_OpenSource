#ifndef PLAT_CLOCK_H
#define PLAT_CLOCK_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,53)
#include <asm/hardware/icst.h>
#endif

struct clk_ops {
	long	(*round)(struct clk *, unsigned long);
	int	(*set)(struct clk *, unsigned long);
	void	(*setvco)(struct clk *, struct icst_vco);
};

int icst_clk_set(struct clk *, unsigned long);
long icst_clk_round(struct clk *, unsigned long);

#endif
