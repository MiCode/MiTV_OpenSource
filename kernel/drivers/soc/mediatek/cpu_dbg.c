/*
 * CPU kernel entry/exit control
 *
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//#include <asm/cpu_ops.h>
#include <asm/io.h>
#include <asm/smp_plat.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/syscore_ops.h>
#include <linux/platform_device.h>
#define RIU_BASE		0x1F000000
#define MIU_BANK		0xc00
#define MIU_UNUSED_OFFSET	0x4
#define WDT_RESET_REASON_VALUE	0xDEAD

extern ptrdiff_t mstar_pm_base;

static void __iomem *wdt_bank_address;

int is_panic = 0;

void init_panic_reg(void)
{
	wdt_bank_address = (ptrdiff_t)ioremap(RIU_BASE + MIU_BANK, SZ_4K);
	writel(0x0, wdt_bank_address + MIU_UNUSED_OFFSET);
}

static int panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
    writel(WDT_RESET_REASON_VALUE, wdt_bank_address + MIU_UNUSED_OFFSET);
    is_panic = 1;

    return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call = panic_event,
};

static int __init mtk_panic_setup(void)
{
	init_panic_reg();
	/* Setup panic notifier */
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
	return 0;
}

postcore_initcall(mtk_panic_setup);

