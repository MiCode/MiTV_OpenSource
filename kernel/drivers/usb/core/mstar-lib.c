/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/******************************************************************************
 *
 * This file is provided under a dual license.  When you use or
 * distribute this software, you may choose to be licensed under
 * version 2 of the GNU General Public License ("GPLv2 License")
 * or BSD License.
 *
 * GPLv2 License
 *
 * Copyright(C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 *
 * BSD LICENSE
 *
 * Copyright(C) 2019 MediaTek Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <asm/io.h>
#include "../host/ehci-mstar.h"
#include "mstar-lib.h"

void usb_power_saving_enable(struct usb_hcd *hcd, bool enable)
{
	if (enable) {
		/* printk("utmi off\n"); */
		if (hcd->utmi_base != 0) {
			/* ([0]: power down override, [1]:Termination override, [6]:15Kohm pull low for dp, [7] :15Kohm pull low for dm) */
			writeb(readb((void*)(hcd->utmi_base)) | (BIT6|BIT7), (void*) (hcd->utmi_base));
			/* ([2]:TED power down, [3]Preamp power down, [6]TX power down) */
			writeb(readb((void*)(hcd->utmi_base+0x1*2-1)) | (BIT2|BIT3|BIT6), (void*) (hcd->utmi_base+0x1*2-1));

			/* new HW term overwrite: on */
			writeb(readb((void*)(hcd->utmi_base+0x52*2)) | (BIT5|BIT4|
				BIT3|BIT2|BIT1|BIT0), (void*) (hcd->utmi_base+0x52*2));
			writeb(readb((void*)(hcd->utmi_base)) | BIT1, (void*) (hcd->utmi_base));

#if defined(ENABLE_UTMI_DYNAMIC_POWER_SAVING)
			writeb(readb((void*)(hcd->utmi_base+0x4C*2)) | BIT2,
					(void*)(hcd->utmi_base+0x4C*2));
#endif
		}

		if (hcd->bc_base != 0) {
			/* BC power down */
			writeb(0xFF, (void*) (hcd->bc_base));
		}
	} else {
		/* printk("utmi on\n"); */
		if (hcd->utmi_base != 0) {
			/* ([0]: power down override, [1]:Termination override, [6]:15Kohm pull low for dp, [7] :15Kohm pull low for dm) */
			writeb(readb((void*)(hcd->utmi_base)) & (u8)(~(BIT1)), (void*) (hcd->utmi_base));
			/* new HW term overwrite: off */
			writeb(readb((void*)(hcd->utmi_base+0x52*2)) & (u8)(~(BIT5|BIT4|
				BIT3|BIT2|BIT1|BIT0)), (void*) (hcd->utmi_base+0x52*2));

			/* ([2]:TED power down, [3]Preamp power down, [6]TX power down) */
			writeb(readb((void*)(hcd->utmi_base+0x1*2-1)) & (u8)(~(BIT2|BIT3|BIT6)), (void*) (hcd->utmi_base+0x1*2-1));
			writeb(readb((void*)(hcd->utmi_base)) & (u8)(~(BIT6|BIT7)), (void*) (hcd->utmi_base));

#if defined(ENABLE_UTMI_DYNAMIC_POWER_SAVING)
			writeb(readb((void*)(hcd->utmi_base+0x4C*2)) | BIT1,
					(void*)(hcd->utmi_base+0x4C*2));
#endif
		}

		if (hcd->bc_base != 0) {
			/* BC power on */
			writeb(0, (void*) (hcd->bc_base));
		}
	}
}

EXPORT_SYMBOL_GPL(usb_power_saving_enable);

#ifdef USB3_MAC_SRAM_POWER_DOWN_ENABLE
void usb30mac_sram_power_saving(struct usb_hcd *hcd, bool enable)
{
	bool usb3_sram_pd = !!(readw((void*)USB3_MAC_SRAM_CTRL_ADDR(hcd)) & USB3_MAC_SRAM_CTRL_BIT(hcd));

	if (-1 == USB3_MAC_SRAM_CTRL_BIT(hcd)) {
		printk("[USB3][SRAM PD] CTRL_BIT error!\n");
		BUG();
	}

	if ((enable ^ usb3_sram_pd) == 0)
		return;

	if (enable) {
		printk("usb3 port %d sram off\n", hcd->port_index);
		writew(readw((void*) USB3_MAC_SRAM_CTRL_ADDR(hcd)) | (u16)USB3_MAC_SRAM_CTRL_BIT(hcd), (void*) USB3_MAC_SRAM_CTRL_ADDR(hcd));
	}
	else {
		printk("usb3 port %d sram on\n", hcd->port_index);
		writew(readw((void*) USB3_MAC_SRAM_CTRL_ADDR(hcd)) & (u16)(~USB3_MAC_SRAM_CTRL_BIT(hcd)), (void*) USB3_MAC_SRAM_CTRL_ADDR(hcd));
	}

	/* printk("[USB DBG] SRAM PD Reg value 0x%x\n", readw((void*)USB3_MAC_SRAM_CTRL_ADDR(hcd)) & USB3_MAC_SRAM_CTRL_BIT(hcd)); */

}

EXPORT_SYMBOL_GPL(usb30mac_sram_power_saving);
#endif

#ifdef USB_MAC_SRAM_POWER_DOWN_ENABLE
void usb20mac_sram_power_saving(struct usb_hcd *hcd, bool enable)
{
	bool usb2_sram_pd = !!(readw((void*)USB_MAC_SRAM_CTRL_ADDR(hcd)) & USB_MAC_SRAM_CTRL_BIT(hcd));

	if (-1 == USB_MAC_SRAM_CTRL_BIT(hcd)) {
		printk("[USB2][SRAM PD] CTRL_BIT error!\n");
		BUG();
	}

	if (USB_MAC_SRAM_CTRL_BIT(hcd) == 0)
		return;

	if ((enable ^ usb2_sram_pd) == 0)
		return;

	if (enable) {
		printk("usb2 port %d sram off\n", hcd->port_index);
		writew(readw((void*) USB_MAC_SRAM_CTRL_ADDR(hcd)) | (u16)USB_MAC_SRAM_CTRL_BIT(hcd), (void*) USB_MAC_SRAM_CTRL_ADDR(hcd));
	}
	else {
		printk("usb2 port %d sram on\n", hcd->port_index);
		writew(readw((void*) USB_MAC_SRAM_CTRL_ADDR(hcd)) & (u16)(~USB_MAC_SRAM_CTRL_BIT(hcd)), (void*) USB_MAC_SRAM_CTRL_ADDR(hcd));
	}

	/* printk("[USB DBG] SRAM PD Reg value 0x%x\n", readw((void*)USB_MAC_SRAM_CTRL_ADDR(hcd)) & USB_MAC_SRAM_CTRL_BIT(hcd)); */

}
EXPORT_SYMBOL_GPL(usb20mac_sram_power_saving);
#endif

#ifdef USB_MAC_MIU_POWER_DOWN_ENABLE
#define USB_MAC_MIU_CTRL_ADDR(h) ((h)->usbc_base+0x20*2)
#define USB_MAC_MIU_CTRL_BIT(h) (0x8000)
void usb20mac_miu_gating(struct usb_hcd *hcd, bool enable)
{
	bool reg_disable = !enable;
	bool usb2_miu_pd = !!(readw((void*)USB_MAC_MIU_CTRL_ADDR(hcd)) &
					USB_MAC_MIU_CTRL_BIT(hcd));

	if (-1 == USB_MAC_MIU_CTRL_BIT(hcd)) {
		printk("[USB2][MIU PD] CTRL_BIT error!\n");
		BUG();
	}

	if ((reg_disable ^ usb2_miu_pd) == 0)
		return;

	if (reg_disable) {
		printk("usb2 port %d miu off\n", hcd->port_index);
		writew(readw((void*) USB_MAC_MIU_CTRL_ADDR(hcd)) |
				(u16)USB_MAC_MIU_CTRL_BIT(hcd),
				(void*) USB_MAC_MIU_CTRL_ADDR(hcd));
	} else {
		printk("usb2 port %d miu on\n", hcd->port_index);
		writew(readw((void*) USB_MAC_MIU_CTRL_ADDR(hcd)) &
				(u16)(~USB_MAC_MIU_CTRL_BIT(hcd)),
				(void*) USB_MAC_MIU_CTRL_ADDR(hcd));
	}
	//printk("[USB DBG] MIU PD Reg value 0x%x\n",
	//		readw((void*)USB_MAC_MIU_CTRL_ADDR(hcd)) &
	//			USB_MAC_MIU_CTRL_BIT(hcd));
}
EXPORT_SYMBOL_GPL(usb20mac_miu_gating);
#endif

#if defined(ENABLE_MONITOR_LINE_STATE_IN_STR)
void mstar_lib_clear_linestate_chg(struct usb_hcd *hcd)
{
	/* UTMI reg_en_reset 0x2d [bit9], clear line state change bit */
	writeb(readb((void*)(hcd->utmi_base+0x5B*2-1)) | BIT1, (void*) (hcd->utmi_base+0x5B*2-1));
	writeb(readb((void*)(hcd->utmi_base+0x5B*2-1)) & ~BIT1, (void*) (hcd->utmi_base+0x5B*2-1));
}

int mstar_lib_get_linestate_chg(struct usb_hcd *hcd)
{
	int ret = 0;

	/* UTMI reg_en_reset 0x2d [bit8], check line state change bit */
	return(readb((void*)(hcd->utmi_base+0x5B*2-1)) & BIT0);
}

EXPORT_SYMBOL_GPL(mstar_lib_clear_linestate_chg);
EXPORT_SYMBOL_GPL(mstar_lib_get_linestate_chg);
#endif
