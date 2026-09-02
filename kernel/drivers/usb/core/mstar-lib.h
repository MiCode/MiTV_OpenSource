#ifndef __MSTAR_USB_LIB_H
#define __MSTAR_USB_LIB_H

extern void usb_power_saving_enable(struct usb_hcd *hcd, bool enable);
extern void usb20mac_sram_power_saving(struct usb_hcd *hcd, bool enable);
extern void usb30mac_sram_power_saving(struct usb_hcd *hcd, bool enable);
extern void usb20mac_miu_gating(struct usb_hcd *hcd, bool enable);
extern void mstar_lib_clear_linestate_chg(struct usb_hcd *hcd);
extern int mstar_lib_get_linestate_chg(struct usb_hcd *hcd);
#endif
