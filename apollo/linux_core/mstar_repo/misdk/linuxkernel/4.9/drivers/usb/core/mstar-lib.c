/*
 * Mstar USB host lib
 * Copyright (C) 2014 MStar Inc.
 * Date: AUG 2014
 */

#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <asm/io.h>
#include "../host/ehci-mstar.h"
#include "mstar-lib.h"

#if defined(DYNAMIC_MIU_SIZE_MAPPING)
uintptr_t MIU0_PHY_BASE_ADDR;
uintptr_t MIU0_SIZE;
uintptr_t MIU1_PHY_BASE_ADDR;
uintptr_t MIU1_SIZE;
uintptr_t MIU2_PHY_BASE_ADDR;
uintptr_t MIU2_SIZE;
uintptr_t MIU3_PHY_BASE_ADDR;
uintptr_t MIU3_SIZE;
EXPORT_SYMBOL(MIU0_PHY_BASE_ADDR);
EXPORT_SYMBOL(MIU0_SIZE);
EXPORT_SYMBOL(MIU1_PHY_BASE_ADDR);
EXPORT_SYMBOL(MIU1_SIZE);
EXPORT_SYMBOL(MIU2_PHY_BASE_ADDR);
EXPORT_SYMBOL(MIU2_SIZE);
EXPORT_SYMBOL(MIU3_PHY_BASE_ADDR);
EXPORT_SYMBOL(MIU3_SIZE);

u8 USB_MIU_SEL0;
u8 USB_MIU_SEL1;
u8 USB_MIU_SEL2;
u8 USB_MIU_SEL3;
EXPORT_SYMBOL(USB_MIU_SEL0);
EXPORT_SYMBOL(USB_MIU_SEL1);
EXPORT_SYMBOL(USB_MIU_SEL2);
EXPORT_SYMBOL(USB_MIU_SEL3);
#endif

#if defined(ENABLE_MONITOR_LINE_STATE_IN_STR)
void mstar_lib_clear_linestate_chg(struct usb_hcd *hcd)
{
	// UTMI reg_en_reset 0x2d [bit9], clear line state change bit
	writeb(readb((void*)(hcd->utmi_base+0x5B*2-1)) | BIT1, (void*) (hcd->utmi_base+0x5B*2-1));
	writeb(readb((void*)(hcd->utmi_base+0x5B*2-1)) & ~BIT1, (void*) (hcd->utmi_base+0x5B*2-1));
}

int mstar_lib_get_linestate_chg(struct usb_hcd *hcd)
{
	int ret = 0;

	// UTMI reg_en_reset 0x2d [bit8], check line state change bit
	return(readb((void*)(hcd->utmi_base+0x5B*2-1)) & BIT0);
}

EXPORT_SYMBOL_GPL(mstar_lib_clear_linestate_chg);
EXPORT_SYMBOL_GPL(mstar_lib_get_linestate_chg);
#endif

static inline void mstar_efuse_set_addr(uintptr_t addr, u16 u16value)
{
	//writew(XHC_EFUSE_OFFSET,  (void*)(_MSTAR_EFUSE_BASE+0x4E*2));
	writew(u16value,  (void*)addr);
}

static inline void mstar_efuse_trig_read(uintptr_t addr, u16 u16value)
{
	writew(readw((void*)addr) | u16value,  (void*)addr);
}

void mstar_efuse_wait_complete(uintptr_t addr, u16 u16value)
{
	int i;

	for (i=0;  i<10000; i++) {
		if ((readw((void*)addr) & u16value) == 0)
			break;
		udelay(1);
	}
	if (10000==i) {
		// timeout: 10ms
		printk(" !!! WARNING: read eFuse timeout !!!\n");
		return;
	}
}

static inline u16 mstar_efuse_read_data(uintptr_t addr)
{
	return readw((void*)addr);
}

u32 mstar_efuse_read(struct mstar_efuse *efuse)
{
	unsigned long	flags;
	u16 val;
	u32 ret;
	spinlock_t	efuse_lock=__SPIN_LOCK_UNLOCKED(efuse_lock);

	spin_lock_irqsave (&efuse_lock, flags);

	//set address
	mstar_efuse_set_addr(efuse->efuse_base_addr+efuse->reg_set_addr, efuse->bank_addr);

	//trig read command
	mstar_efuse_trig_read(efuse->efuse_base_addr+efuse->reg_read, efuse->issue_read);

	//wait read complete
	mstar_efuse_wait_complete(efuse->efuse_base_addr+efuse->reg_read, efuse->issue_read);

	//read data
	val = mstar_efuse_read_data(efuse->efuse_base_addr+efuse->reg_data);
	ret = val;
	val = mstar_efuse_read_data(efuse->efuse_base_addr+efuse->reg_data+0x2*2);
	ret += ((u32)val<<16);

	spin_unlock_irqrestore (&efuse_lock, flags);

	return ret;

}

#if defined(MSTAR_EFUSE_RTERM)
u16 mstar_efuse_rterm(void)
{
	struct mstar_efuse efuse;

	efuse.efuse_base_addr = MSTAR_EFUSE_BASE;
	efuse.reg_set_addr = EFUSE_REG_ADDR;
	efuse.reg_read = EFUSE_REG_READ;
	efuse.reg_data = EFUSE_REG_DATA;
	efuse.bank_addr = RTERM_BANK;
	efuse.issue_read = EFUSE_READ_TRIG;

	return (u16)mstar_efuse_read(&efuse);
}

EXPORT_SYMBOL_GPL(mstar_efuse_read);
EXPORT_SYMBOL_GPL(mstar_efuse_rterm);
#endif
