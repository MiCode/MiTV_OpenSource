#ifndef __MSTAR_USB_LIB_H
#define __MSTAR_USB_LIB_H


struct mstar_efuse {
	uintptr_t efuse_base_addr;
	u32 reg_set_addr;	//register offset for set_addr
	u32 reg_read;		//register offset for issue_read
	u32 reg_data;		//register offset for data
	u16 bank_addr;
	u16	issue_read;
};


extern u32 mstar_efuse_read(struct mstar_efuse *efuse);
extern u16 mstar_efuse_rterm(void);
extern void mstar_lib_clear_linestate_chg(struct usb_hcd *hcd);
extern int mstar_lib_get_linestate_chg(struct usb_hcd *hcd);
#endif
