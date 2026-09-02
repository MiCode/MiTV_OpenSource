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

#include <linux/platform_device.h>
#include "ehci.h"
#include "ehci-mstar.h"
#include "xhci-mstar.h"
#include "../core/mstar-lib.h"
#ifdef USB_MSTAR_BDMA
#include <mdrv_types.h>
#include <mdrv_bdma.h>
#endif

static struct hc_driver __read_mostly ehci_mstar_hc_driver;

void set_utmi_param(struct usb_hcd *hcd, struct device_node *node)
{
	struct device *dev;

	dev = hcd->self.controller;

	if (of_property_read_u8(node, "utmi,chirp", &hcd->utmi_param.chirp)) {
#if defined(UTMI_CHIRP_DCT_LEVEL_42)
		hcd->utmi_param.chirp = UTMI_CHIRP_DCT_LEVEL_42 & 0xf0;
#else
		hcd->utmi_param.chirp = 0x80;
#endif
	}

	if (of_property_read_u8(node, "utmi,squelch",
				&hcd->utmi_param.squelch)) {
		hcd->utmi_param.squelch = UTMI_DISCON_LEVEL_2A & 0x0f;
	}

	if (of_property_read_u8(node, "utmi,disconnect",
				&hcd->utmi_param.disconnect)) {
		hcd->utmi_param.disconnect = UTMI_DISCON_LEVEL_2A & 0xf0;
	}

	if (of_property_read_u8_array(node, "utmi,eye-param",
				hcd->utmi_param.eye_param, 4)) {
		hcd->utmi_param.eye_param[0] = UTMI_EYE_SETTING_2C;
		hcd->utmi_param.eye_param[1] = UTMI_EYE_SETTING_2D;
		hcd->utmi_param.eye_param[2] = UTMI_EYE_SETTING_2E;
		hcd->utmi_param.eye_param[3] = UTMI_EYE_SETTING_2F;
	}

	if (of_property_read_u32(node, "utmi,dpdm-swap",
				&hcd->utmi_param.dpdm_swap)) {
		hcd->utmi_param.dpdm_swap = -1;

		if (hcd->port_index == 1) {
#if _USB_UTMI_DPDM_SWAP_P0
			hcd->utmi_flag |= EHCFLAG_DPDM_SWAP;
#endif
		}
#if !defined(DISABLE_SECOND_EHC)
		else if (hcd->port_index == 2) {
#if _USB_UTMI_DPDM_SWAP_P1
			hcd->utmi_flag |= EHCFLAG_DPDM_SWAP;
#endif
		}
#endif
	} else if (hcd->utmi_param.dpdm_swap)
			hcd->utmi_flag |= EHCFLAG_DPDM_SWAP;

	if (of_property_read_u32(node, "utmi,otg-enable",
				&hcd->utmi_param.otg_enable)) {
		hcd->utmi_param.otg_enable = -1;

		if (hcd->port_index == 1) {
#ifdef _USB_ENABLE_OTG_P0
			hcd->utmi_flag |= EHCFLAG_ENABLE_OTG;
#endif
		}
#if !defined(DISABLE_SECOND_EHC)
		else if (hcd->port_index == 2) {
#ifdef _USB_ENABLE_OTG_P1
			hcd->utmi_flag |= EHCFLAG_ENABLE_OTG;
#endif
		}
#endif
#ifdef ENABLE_THIRD_EHC
		else if (hcd->port_index == 3) {
#ifdef _USB_ENABLE_OTG_P2
			hcd->utmi_flag |= EHCFLAG_ENABLE_OTG;
#endif
		}
#endif
#ifdef ENABLE_FOURTH_EHC
		else if (hcd->port_index == 4) {
#ifdef _USB_ENABLE_OTG_P3
			hcd->utmi_flag |= EHCFLAG_ENABLE_OTG;
#endif
		}
#endif
#ifdef ENABLE_FIFTH_EHC
		else if (hcd->port_index == 5) {
#ifdef _USB_ENABLE_OTG_P4
			hcd->utmi_flag |= EHCFLAG_ENABLE_OTG;
#endif
		}
#endif
	} else if (hcd->utmi_param.otg_enable)
			hcd->utmi_flag |= EHCFLAG_ENABLE_OTG;

	dev_info(dev, "utmi chirp: 0x%02x\n", hcd->utmi_param.chirp);
	dev_info(dev, "utmi squelch: 0x%02x\n", hcd->utmi_param.squelch);
	dev_info(dev, "utmi disconnect: 0x%02x\n", hcd->utmi_param.disconnect);
	dev_info(dev, "utmi eye: 0x%02x 0x%02x 0x%02x 0x%02x\n",
		hcd->utmi_param.eye_param[0], hcd->utmi_param.eye_param[1],
		hcd->utmi_param.eye_param[2], hcd->utmi_param.eye_param[3]);
	dev_info(dev, "utmi dpdm-swap: %x\n", hcd->utmi_param.dpdm_swap);
	dev_info(dev, "utmi otg-enable: %x\n", hcd->utmi_param.otg_enable);
	dev_info(dev, "utmi -- flag: 0x%08x\n", hcd->utmi_flag);
}

#if defined(ENABLE_USB_NEW_MIU_SLE)
void MIU_select_setting_ehc(uintptr_t USBC_base)
{
	printk("[USB] config miu select [%02x] [%02x] [%02x] [%02x]\n", USB_MIU_SEL0, USB_MIU_SEL1, USB_MIU_SEL2, USB_MIU_SEL3);
	writeb(USB_MIU_SEL0, (void*)(USBC_base+0x14*2));	//Setting MIU0 segment
	writeb(USB_MIU_SEL1, (void*)(USBC_base+0x16*2));	//Setting MIU1 segment
	writeb(USB_MIU_SEL2, (void*)(USBC_base+0x17*2-1));	//Setting MIU2 segment
	writeb(USB_MIU_SEL3, (void*)(USBC_base+0x18*2));	//Setting MIU3 segment
	writeb(readb((void*)(USBC_base+0x19*2-1)) | BIT0, (void*)(USBC_base+0x19*2-1));	//Enable miu partition mechanism
#if !defined(DISABLE_MIU_LOW_BOUND_ADDR_SUBTRACT_ECO)
	printk("[USB] enable miu lower bound address subtraction\n");
	writeb(readb((void*)(USBC_base+0x0F*2-1)) | BIT0, (void*)(USBC_base+0x0F*2-1));
#endif
}
#endif

void Titania3_series_start_ehc(struct usb_hcd *hcd, uintptr_t UTMI_base,
		uintptr_t USBC_base, uintptr_t UHC_base, unsigned int flag)
{
	struct usb_utmi_param *u_param;
	u8 *eye_param;

	u_param = &hcd->utmi_param;
	eye_param = u_param->eye_param;

	printk("Titania3_series_start_ehc start\n");

#if defined(ENABLE_USB_NEW_MIU_SLE)
	MIU_select_setting_ehc(USBC_base);
#endif

	if (flag & EHCFLAG_TESTPKG)
	{
		writew(0x2084, (void*)(UTMI_base+0x2*2));
		writew(0x8051, (void*)(UTMI_base+0x20*2));
	}

#if _USB_HS_CUR_DRIVE_DM_ALLWAYS_HIGH_PATCH
	/*
	 * patch for DM always keep high issue
	 * init overwrite register
	 */
	writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~BIT3), (void*) (UTMI_base+0x0*2)); //DP_PUEN = 0
	writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~BIT4), (void*) (UTMI_base+0x0*2)); //DM_PUEN = 0

	writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~BIT5), (void*) (UTMI_base+0x0*2)); //R_PUMODE = 0

	writeb(readb((void*)(UTMI_base+0x0*2)) | BIT6, (void*) (UTMI_base+0x0*2)); //R_DP_PDEN = 1
	writeb(readb((void*)(UTMI_base+0x0*2)) | BIT7, (void*) (UTMI_base+0x0*2)); //R_DM_PDEN = 1

	writeb(readb((void*)(UTMI_base+0x10*2)) | BIT6, (void*) (UTMI_base+0x10*2)); //hs_txser_en_cb = 1
	writeb(readb((void*)(UTMI_base+0x10*2)) & (u8)(~BIT7), (void*) (UTMI_base+0x10*2)); //hs_se0_cb = 0

	/* turn on overwrite mode */
	writeb(readb((void*)(UTMI_base+0x0*2)) | BIT1, (void*) (UTMI_base+0x0*2)); //tern_ov = 1
	/* new HW term overwrite: on */
	writeb(readb((void*)(UTMI_base+0x52*2)) | (BIT5|BIT4|
		BIT3|BIT2|BIT1|BIT0), (void*) (UTMI_base+0x52*2));
#endif

	/* Turn on overwirte mode for D+/D- floating issue when UHC reset
	 * Before UHC reset, R_DP_PDEN = 1, R_DM_PDEN = 1, tern_ov = 1 */
	writeb(readb((void*)(UTMI_base+0x0*2)) | (BIT7|BIT6|BIT1), (void*) (UTMI_base+0x0*2));
	/* new HW term overwrite: on */
	writeb(readb((void*)(UTMI_base+0x52*2)) | (BIT5|BIT4|
		BIT3|BIT2|BIT1|BIT0), (void*) (UTMI_base+0x52*2));

#ifdef ENABLE_DOUBLE_DATARATE_SETTING
	writeb(readb((void*)(UTMI_base+0x0D*2-1)) | BIT0, (void*) (UTMI_base+0x0D*2-1)); // set reg_double_data_rate, To get better jitter performance
#endif
#ifdef ENABLE_UPLL_SETTING
	// sync code from eCos
	{
		u16 reg_t;

		reg_t = readw((void*)(UTMI_base+0x22*2));
		if ((reg_t & 0x10e0) != 0x10e0)
			writew(0x10e0, (void*)(UTMI_base+0x22*2));
		reg_t = readw((void*)(UTMI_base+0x24*2));
		if (reg_t != 0x1)
			writew(0x1, (void*)(UTMI_base+0x24*2));
	}
	//writeb(0, (void*) (UTMI_base+0x21*2-1));
	//writeb(0x10, (void*) (UTMI_base+0x23*2-1));
	//writeb(0x01, (void*) (UTMI_base+0x24*2));
#endif

	writeb(readb((void*)(USBC_base)) | (BIT1|BIT3), (void*) (USBC_base));  		// Disable MAC initial suspend, Reset UHC
	writeb((readb((void*)(USBC_base)) & ~(BIT1)) |BIT5, (void*) (USBC_base));	// Release UHC reset, enable UHC XIU

	if (flag & EHCFLAG_DOUBLE_DATARATE)
	{
		if ((flag & EHCFLAG_DDR_MASK) == EHCFLAG_DDR_x15)
		{
			// Set usb bus = 480MHz x 1.5
			writeb(readb((void*)(UTMI_base+0x20*2)) | 0x76, (void*)(UTMI_base+0x20*2));
		}
		else if ((flag & EHCFLAG_DDR_MASK) == EHCFLAG_DDR_x18)
		{
			// Set usb bus = 480MHz x 1.8
			writeb(readb((void*)(UTMI_base+0x20*2)) | 0x8e, (void*)(UTMI_base+0x20*2));
		}
#if 0
		else if ((flag & EHCFLAG_DDR_MASK) == EHCFLAG_DDR_x20)
		{
			// Set usb bus = 480MHz x2
			writeb(readb((void*)(UTMI_base+0xd*2-1)) | 0x01, (void*)(UTMI_base+0xd*2-1));
		}
#endif
		/* Set slew rate control for overspeed (or 960MHz) */
		writeb(readb((void*)(UTMI_base+0x2c*2)) | BIT0, (void*) (UTMI_base+0x2c*2));
	}

	/* Init UTMI squelch level setting before CA */
	if(u_param->squelch)
	{
		writeb(u_param->squelch, (void*)(UTMI_base+0x2a*2));
		printk("[USB] init squelch level 0x%x\n", readb((void*)(UTMI_base+0x2a*2)));
	}

	writeb(readb((void*)(UTMI_base+0x3c*2)) | BIT0, (void*)(UTMI_base+0x3c*2)); // set CA_START as 1
	mdelay(1);

	writeb(readb((void*)(UTMI_base+0x3c*2)) & (u8)(~BIT0), (void*)(UTMI_base+0x3c*2)); // release CA_START

	/* HAPS */
	//while ((readb((void*)(UTMI_base+0x3c*2)) & BIT1) == 0);	// polling bit <1> (CA_END)

	if ((0xFFF0 == (readw((void*)(UTMI_base+0x3C*2)) & 0xFFF0 )) ||
		(0x0000 == (readw((void*)(UTMI_base+0x3C*2)) & 0xFFF0 ))  )
		printk("WARNING: CA Fail !! \n");

	if (flag & EHCFLAG_DPDM_SWAP)
		writeb(readb((void*)(UTMI_base+0x0b*2-1)) | BIT5, (void*)(UTMI_base+0x0b*2-1)); // dp dm swap
#if defined(CONFIG_USB_MS_OTG) || defined(CONFIG_USB_MS_OTG_MODULE)
	if(flag & EHCFLAG_ENABLE_OTG)
	{
		// let OTG driver to handle the UTMI switch control
	}
	else
#endif
		writeb((u8)((readb((void*)(USBC_base+0x02*2)) & ~BIT1) | BIT0), (void*)(USBC_base+0x02*2)); // UHC select enable

	writeb(readb((void*)(UHC_base+0x40*2)) & (u8)(~BIT4), (void*)(UHC_base+0x40*2)); // 0: VBUS On.
	udelay(1); // delay 1us

	writeb(readb((void*)(UHC_base+0x40*2)) | BIT3, (void*)(UHC_base+0x40*2)); // Active HIGH

	/* Turn on overwirte mode for D+/D- floating issue when UHC reset
	 * After UHC reset, disable overwrite bits */
	writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~(BIT7|BIT6|BIT1)), (void*) (UTMI_base+0x0*2));
	/* new HW term overwrite: off */
	writeb(readb((void*)(UTMI_base+0x52*2)) & (u8)(~(BIT5|BIT4|
		BIT3|BIT2|BIT1|BIT0)), (void*) (UTMI_base+0x52*2));

	/* improve the efficiency of USB access MIU when system is busy */
	writeb(readb((void*)(UHC_base+0x81*2-1)) | (BIT0 | BIT1 | BIT2 | BIT3 | BIT7), (void*)(UHC_base+0x81*2-1));

	writeb((u8)((readb((void*)(UTMI_base+0x06*2)) & ~BIT5) | BIT6), (void*)(UTMI_base+0x06*2)); // reg_tx_force_hs_current_enable

	writeb((u8)((readb((void*)(UTMI_base+0x03*2-1)) & ~BIT4) | (BIT3 | BIT5)), (void*)(UTMI_base+0x03*2-1)); // Disconnect window select

	writeb(readb((void*)(UTMI_base+0x07*2-1)) & (u8)(~BIT1), (void*)(UTMI_base+0x07*2-1)); // Disable improved CDR

#if defined(ENABLE_UTMI_240_AS_120_PHASE_ECO)
	#if defined(UTMI_240_AS_120_PHASE_ECO_INV)
	writeb(readb((void*)(UTMI_base+0x08*2)) & (u8)(~BIT3), (void*)(UTMI_base+0x08*2)); //Set sprcial value for Eiffel USB analog LIB issue
	#else
	/* bit<3> for 240's phase as 120's clock set 1, bit<4> for 240Mhz in mac 0 for faraday 1 for etron */
	writeb(readb((void*)(UTMI_base+0x08*2)) | BIT3, (void*)(UTMI_base+0x08*2));
	#endif
#endif

	writeb(readb((void*)(UTMI_base+0x09*2-1)) | (BIT0 | BIT7), (void*)(UTMI_base+0x09*2-1)); // UTMI RX anti-dead-loc, ISI effect improvement

	if ((flag & EHCFLAG_DOUBLE_DATARATE)==0)
	    writeb(readb((void*)(UTMI_base+0x0b*2-1)) | BIT7, (void*)(UTMI_base+0x0b*2-1)); // TX timing select latch path

	writeb(readb((void*)(UTMI_base+0x15*2-1)) | BIT5, (void*)(UTMI_base+0x15*2-1)); // Chirp signal source select

#if defined(ENABLE_UTMI_55_INTERFACE)
	writeb(readb((void*)(UTMI_base+0x15*2-1)) | BIT6, (void*)(UTMI_base+0x15*2-1)); // change to 55 interface
#endif

	/* new HW chrip design, defualt overwrite to reg_2A */
	writeb(readb((void*)(UTMI_base+0x40*2)) & (u8)(~BIT4), (void*)(UTMI_base+0x40*2));

	/* Init UTMI disconnect level setting */
	writeb((u_param->disconnect | u_param->squelch), (void*)(UTMI_base+0x2a*2));

#if defined(ENABLE_NEW_HW_CHRIP_PATCH)
	/* Init chrip detect level setting */
	writeb(u_param->chirp, (void*)(UTMI_base+0x42*2));
	/* enable HW control chrip/disconnect level */
	writeb(readb((void*)(UTMI_base+0x40*2)) & (u8)(~BIT3), (void*)(UTMI_base+0x40*2));
#endif

	/* Init UTMI eye diagram parameter setting */
	writeb(readb((void*)(UTMI_base+0x2c*2)) | eye_param[0], (void*)(UTMI_base+0x2c*2));
	writeb(readb((void*)(UTMI_base+0x2d*2-1)) | eye_param[1], (void*)(UTMI_base+0x2d*2-1));
	writeb(readb((void*)(UTMI_base+0x2e*2)) | eye_param[2], (void*)(UTMI_base+0x2e*2));
	writeb(readb((void*)(UTMI_base+0x2f*2-1)) | eye_param[3], (void*)(UTMI_base+0x2f*2-1));

#if defined(ENABLE_LS_CROSS_POINT_ECO)
	/* Enable deglitch SE0 (low-speed cross point) */
	writeb(readb((void*)(UTMI_base+LS_CROSS_POINT_ECO_OFFSET)) | LS_CROSS_POINT_ECO_BITSET, (void*)(UTMI_base+LS_CROSS_POINT_ECO_OFFSET));
#endif

#if defined(ENABLE_PWR_NOISE_ECO)
	/* Enable use eof2 to reset state machine (power noise) */
	writeb(readb((void*)(USBC_base+0x02*2)) | BIT6, (void*)(USBC_base+0x02*2));
#endif

#if defined(ENABLE_TX_RX_RESET_CLK_GATING_ECO)
	/* Enable hw auto deassert sw reset(tx/rx reset) */
	writeb(readb((void*)(UTMI_base+TX_RX_RESET_CLK_GATING_ECO_OFFSET)) | TX_RX_RESET_CLK_GATING_ECO_BITSET, (void*)(UTMI_base+TX_RX_RESET_CLK_GATING_ECO_OFFSET));
#endif

#if defined(ENABLE_LOSS_SHORT_PACKET_INTR_ECO)
	/* Enable patch for the assertion of interrupt(Lose short packet interrupt) */
	#if defined(LOSS_SHORT_PACKET_INTR_ECO_OPOR)
	writeb(readb((void*)(USBC_base+LOSS_SHORT_PACKET_INTR_ECO_OFFSET)) | LOSS_SHORT_PACKET_INTR_ECO_BITSET, (void*)(USBC_base+LOSS_SHORT_PACKET_INTR_ECO_OFFSET));
	#else
	writeb(readb((void*)(USBC_base+0x04*2)) & (u8)(~BIT7), (void*)(USBC_base+0x04*2));
	#endif
#endif

#if defined(ENABLE_BABBLE_ECO)
	/* Enable add patch to Period_EOF1(babble problem) */
	writeb(readb((void*)(USBC_base+0x04*2)) | BIT6, (void*)(USBC_base+0x04*2));
#endif

#if defined(ENABLE_MDATA_ECO)
	/* Enable short packet MDATA in Split transaction clears ACT bit (LS dev under a HS hub) */
	writeb(readb((void*)(USBC_base+MDATA_ECO_OFFSET)) | MDATA_ECO_BITSET, (void*) (USBC_base+MDATA_ECO_OFFSET));
#endif

#if defined(ENABLE_HS_DM_KEEP_HIGH_ECO)
	/* Change override to hs_txser_en.  Dm always keep high issue */
	writeb(readb((void*)(UTMI_base+0x10*2)) | BIT6, (void*) (UTMI_base+0x10*2));
#endif

#if defined(ENABLE_HS_CONNECTION_FAIL_INTO_VFALL_ECO)
	/* HS connection fail problem (Gate into VFALL state) */
	writeb(readb((void*)(USBC_base+0x11*2-1)) | BIT1, (void*)(USBC_base+0x11*2-1));
#endif

#if _USB_HS_CUR_DRIVE_DM_ALLWAYS_HIGH_PATCH
	/*
	 * patch for DM always keep high issue
	 * init overwrite register
	 */
	writeb(readb((void*)(UTMI_base+0x0*2)) | BIT6, (void*) (UTMI_base+0x0*2)); //R_DP_PDEN = 1
	writeb(readb((void*)(UTMI_base+0x0*2)) | BIT7, (void*) (UTMI_base+0x0*2)); //R_DM_PDEN = 1

	/* turn on overwrite mode */
	writeb(readb((void*)(UTMI_base+0x0*2)) | BIT1, (void*) (UTMI_base+0x0*2)); //tern_ov = 1
	/* new HW term overwrite: on */
	writeb(readb((void*)(UTMI_base+0x52*2)) | (BIT5|BIT4|
		BIT3|BIT2|BIT1|BIT0), (void*) (UTMI_base+0x52*2));
#endif

#if defined (ENABLE_PV2MI_BRIDGE_ECO)
	writeb(readb((void*)(USBC_base+0x0a*2)) | BIT6, (void*)(USBC_base+0x0a*2));
#endif

#if _USB_ANALOG_RX_SQUELCH_PATCH
	/* squelch level adjust by calibration value */
	{
	unsigned int ca_da_ov, ca_db_ov, ca_tmp;

	ca_tmp = readw((void*)(UTMI_base+0x3c*2));
	ca_da_ov = (((ca_tmp >> 4) & 0x3f) - 5) + 0x40;
	ca_db_ov = (((ca_tmp >> 10) & 0x3f) - 5) + 0x40;
	printk("[%x]-5 -> (ca_da_ov, ca_db_ov)=(%x,%x)\n", ca_tmp, ca_da_ov, ca_db_ov);
	writeb(ca_da_ov ,(void*)(UTMI_base+0x3B*2-1));
	writeb(ca_db_ov ,(void*)(UTMI_base+0x24*2));
	}
#endif

#if _USB_MINI_PV2MI_BURST_SIZE
	writeb(readb((void*)(USBC_base+0x0b*2-1)) & ~(BIT1|BIT2|BIT3|BIT4), (void*)(USBC_base+0x0b*2-1));
#endif

#if defined(ENABLE_UHC_PREAMBLE_ECO)
	/* [7]: reg_etron_en, to enable utmi Preamble function */
	writeb(readb((void*)(UTMI_base+0x3f*2-1)) | BIT7, (void*) (UTMI_base+0x3f*2-1));

	/* [3:]: reg_preamble_en, to enable Faraday Preamble */
	writeb(readb((void*)(USBC_base+0x0f*2-1)) | BIT3, (void*)(USBC_base+0x0f*2-1));

	/* [0]: reg_preamble_babble_fix, to patch Babble occurs in Preamble */
	writeb(readb((void*)(USBC_base+0x10*2)) | BIT0, (void*)(USBC_base+0x10*2));

	/* [1]: reg_preamble_fs_within_pre_en, to patch FS crash problem */
	writeb(readb((void*)(USBC_base+0x10*2)) | BIT1, (void*)(USBC_base+0x10*2));

	/* [2]: reg_fl_sel_override, to override utmi to have FS drive strength */
	writeb(readb((void*)(UTMI_base+0x03*2-1)) | BIT2, (void*) (UTMI_base+0x03*2-1));
#endif

#if defined(ENABLE_UHC_RUN_BIT_ALWAYS_ON_ECO)
	/* Don't close RUN bit when device disconnect */
	writeb(readb((void*)(UHC_base+0x34*2)) | BIT7, (void*)(UHC_base+0x34*2));
#endif

#if _USB_MIU_WRITE_WAIT_LAST_DONE_Z_PATCH
	/* Enabe PVCI i_miwcplt wait for mi2uh_last_done_z */
	writeb(readb((void*)(UHC_base+0x83*2-1)) | BIT4, (void*)(UHC_base+0x83*2-1));
#endif

#if defined(ENABLE_UHC_EXTRA_HS_SOF_ECO)
	/* Extra HS SOF after bus reset */
	writeb(readb((void*)(UHC_base+0x8C*2)) | BIT0, (void*)(UHC_base+0x8C*2));
#endif

	/* Enable HS ISO IN Camera Cornor case ECO function */
#if defined(HS_ISO_IN_ECO_OFFSET)
		writeb(readb((void*)(USBC_base+HS_ISO_IN_ECO_OFFSET)) | HS_ISO_IN_ECO_BITSET, (void*) (USBC_base+HS_ISO_IN_ECO_OFFSET));
#else
	writeb(readb((void*)(USBC_base+0x13*2-1)) | BIT0, (void*)(USBC_base+0x13*2-1));
#endif

#if defined(ENABLE_DISCONNECT_SPEED_REPORT_RESET_ECO)
	/* UHC speed type report should be reset by device disconnection */
	writeb(readb((void*)(USBC_base+0x20*2)) | BIT0, (void*)(USBC_base+0x20*2));
#endif

#if defined(ENABLE_BABBLE_PCD_ONE_PULSE_TRIGGER_ECO)
	/* Port Change Detect (PCD) is triggered by babble.
	 * Pulse trigger will not hang this condition.
	 */
	writeb(readb((void*)(USBC_base+0x20*2)) | BIT1, (void*)(USBC_base+0x20*2));
#endif

#if defined(ENABLE_HC_RESET_FAIL_ECO)
	/* generation of hhc_reset_u */
	writeb(readb((void*)(USBC_base+0x20*2)) | BIT2, (void*)(USBC_base+0x20*2));
#endif

#if defined(ENABLE_INT_AFTER_WRITE_DMA_ECO)
	/* DMA interrupt after the write back of qTD */
	writeb(readb((void*)(USBC_base+0x20*2)) | BIT3, (void*)(USBC_base+0x20*2));
#endif

#if defined(ENABLE_DISCONNECT_HC_KEEP_RUNNING_ECO)
	/* EHCI keeps running when device is disconnected */
	writeb(readb((void*)(USBC_base+0x19*2-1)) | BIT3, (void*)(USBC_base+0x19*2-1));
#endif

#if !defined(_EHC_SINGLE_SOF_TO_CHK_DISCONN)
	writeb(0x05, (void*)(USBC_base+0x03*2-1)); //Use 2 SOFs to check disconnection
#endif

#if defined(ENABLE_SRAM_CLK_GATING_ECO)
	/* do SRAM clock gating automatically to save power */
	writeb(readb((void*)(USBC_base+0x20*2)) & (u8)(~BIT4), (void*)(USBC_base+0x20*2));
#endif

#if defined (ENABLE_INTR_SITD_CS_IN_ZERO_ECO)
	/* Enable interrupt in sitd cs in zero packet */
	writeb(readb((void*)(USBC_base+0x11*2-1)) | BIT7, (void*)(USBC_base+0x11*2-1));
#endif

#if !defined (DISABLE_TIMING_TO_RESET_SPEED_TYPE_ECO)
	/* enable timing to reset speed type eco
	 * {reg_spf_typ_rst_opt1, reg_spf_typ_rst_opt} =
	 * 2'b00 : speed type is reset when device disconnects
	 * 2'b01 : speed type is reset when device connects
	 * 2'b10 : speed type is reset when bus reset
	 * 2'b11 : speed type is reset when device connects or bus reset
	 */

	/* reg_spf_typ_rst_opt1 = usbc+0x22 [bit0] */
	writeb(readb((void*)(USBC_base+0x22*2)) | BIT0, (void*)(USBC_base+0x22*2));
	/* reg_spf_typ_rst_opt = usbc+0x20 [bit0] */
	writeb(readb((void*)(USBC_base+0x20*2)) | BIT0, (void*)(USBC_base+0x20*2));
#endif

#if defined(ENABLE_EOF1_WINDOW_FOR_ISO_INTR_ECO)
	/* [5]: int1024_eof1_en
	    [4]: iso_eof1_en
	    [3:2]:  2'b00=20us, 2'b01=22us, 2'b10=25us, 2'b11=30us
	*/
	writeb(readb((void*)(USBC_base+0x21*2-1)) & ~(BIT2 | BIT3), (void*)(USBC_base+0x21*2-1));
	writeb(readb((void*)(USBC_base+0x21*2-1)) | (BIT5 | BIT4), (void*)(USBC_base+0x21*2-1));
#endif

	if (flag & EHCFLAG_TESTPKG)
	{
		writew(0x0600, (void*) (UTMI_base+0x14*2));
		writew(0x0078, (void*) (UTMI_base+0x10*2));
		writew(0x0bfe, (void*) (UTMI_base+0x32*2));
	}
}

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

#if defined(CONFIG_OF)
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);
#endif

void ehci_mstar_hw_init(struct usb_hcd *hcd)
{
	Titania3_series_start_ehc(hcd, hcd->utmi_base,
			hcd->usbc_base, hcd->ehc_base, hcd->utmi_flag);
}

/**
 * usb_ehci_au1xxx_probe - initialize Au1xxx-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
static int ehci_hcd_mstar_drv_probe(struct platform_device *dev)
{
	int retval=0;
	int irq = -1;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	u64 dma_mask;
#ifdef ENABLE_SECOND_XHC
	#ifdef _MSTAR_EHC0_COMP_PORT
	struct comp_port ehc0_comp = {_MSTAR_EHC0_COMP_PORT, _MSTAR_EHC0_COMP_U3TOP_BASE, _MSTAR_EHC0_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC1_COMP_PORT
	struct comp_port ehc1_comp = {_MSTAR_EHC1_COMP_PORT, _MSTAR_EHC1_COMP_U3TOP_BASE, _MSTAR_EHC1_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC2_COMP_PORT
	struct comp_port ehc2_comp = {_MSTAR_EHC2_COMP_PORT, _MSTAR_EHC2_COMP_U3TOP_BASE, _MSTAR_EHC2_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC3_COMP_PORT
	struct comp_port ehc3_comp = {_MSTAR_EHC3_COMP_PORT, _MSTAR_EHC3_COMP_U3TOP_BASE, _MSTAR_EHC3_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC4_COMP_PORT
	struct comp_port ehc4_comp = {_MSTAR_EHC4_COMP_PORT, _MSTAR_EHC4_COMP_U3TOP_BASE, _MSTAR_EHC4_COMP_PORT_INDEX};
	#endif
#else /* one root case */
	#ifdef _MSTAR_EHC0_COMP_PORT
	struct comp_port ehc0_comp = {_MSTAR_EHC0_COMP_PORT, _MSTAR_U3TOP_BASE, _MSTAR_EHC0_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC1_COMP_PORT
	struct comp_port ehc1_comp = {_MSTAR_EHC1_COMP_PORT, _MSTAR_U3TOP_BASE, _MSTAR_EHC1_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC2_COMP_PORT
	struct comp_port ehc2_comp = {_MSTAR_EHC2_COMP_PORT, _MSTAR_U3TOP_BASE, _MSTAR_EHC2_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC3_COMP_PORT
	struct comp_port ehc3_comp = {_MSTAR_EHC3_COMP_PORT, _MSTAR_U3TOP_BASE, _MSTAR_EHC3_COMP_PORT_INDEX};
	#endif
	#ifdef _MSTAR_EHC4_COMP_PORT
	struct comp_port ehc4_comp = {_MSTAR_EHC4_COMP_PORT, _MSTAR_U3TOP_BASE, _MSTAR_EHC4_COMP_PORT_INDEX};
	#endif
#endif

	if (usb_disabled())
		return -ENODEV;

#ifdef ENABLE_CHIPTOP_PERFORMANCE_SETTING
	int chipVER = readw((void *)(MSTAR_CHIP_TOP_BASE+0xCE*2));
	/* chip top performance tuning [11:9] = 0xe00 */
	if (chipVER == 0x101) // U02
		writew(readw((void*)(MSTAR_CHIP_TOP_BASE+0x46*2)) | 0xe00,
						(void*) (MSTAR_CHIP_TOP_BASE+0x46*2));
#endif

	printk("%s H.W init\n", dev->name);
	#define NAME_LEN (12)

#if defined(CONFIG_OF)
	if (!dev->dev.platform_data)
	{
		printk(KERN_WARNING "[USB] no platform_data, device tree coming\n");
	}

	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;

	if(IS_ENABLED(CONFIG_ARM64) && (IS_ENABLED(CONFIG_ZONE_DMA) || IS_ENABLED(CONFIG_ZONE_DMA32)))
	{
#if defined(EHC_DMA_BIT_MASK)
		dma_mask = EHC_DMA_BIT_MASK;
#else
		/* default: 32bit to mask lowest 4G address */
		dma_mask = DMA_BIT_MASK(32);
#endif
	} else
		dma_mask = DMA_BIT_MASK(64);

	if(dma_set_mask(&dev->dev, dma_mask) ||
		dma_set_coherent_mask(&dev->dev, dma_mask))
	{
		printk(KERN_ERR "[USB][EHC] cannot accept dma mask 0x%llx\n", dma_mask);
		return -EOPNOTSUPP;
	}

	printk(KERN_NOTICE "[USB][EHC] dma coherent_mask 0x%llx mask 0x%llx\n",
		dev->dev.coherent_dma_mask, *dev->dev.dma_mask);

	/* try to get irq from device tree */
	irq = irq_of_parse_and_map(dev->dev.of_node, 0);
#else
	if (dev->resource[2].flags != IORESOURCE_IRQ) {
		printk(KERN_WARNING "resource[2] is not IORESOURCE_IRQ");
	}
	else
	{
		irq =  dev->resource[2].start;
	}
#endif

#if !defined(ENABLE_IRQ_REMAP)
	if(irq <= 0)
	{
		printk(KERN_ERR "[USB] can not get irq for %s\n", dev->name);
		return -ENODEV;
	}
#endif

	hcd = usb_create_hcd(&ehci_mstar_hc_driver, &dev->dev, "mstar");
	if (!hcd)
		return -ENOMEM;

	/* ehci_hcd_init(hcd_to_ehci(hcd)); */
	if( 0==strncmp(dev->name, "Mstar-ehci-haps-1", NAME_LEN) )
	{
		hcd->port_index = 1;
		hcd->utmi_base = _MSTAR_UTMI0_BASE;
		hcd->ehc_base = _MSTAR_UHC0_BASE;
		hcd->usbc_base = _MSTAR_USBC0_BASE;
		hcd->bc_base = _MSTAR_BC0_BASE;
		#ifdef _MSTAR_EHC0_COMP_PORT
		hcd->companion = ehc0_comp;
		#endif
		#ifdef ENABLE_IRQ_REMAP
		irq = MSTAR_EHC1_IRQ;
		#endif
		/* HAPS */
		printk("switch to EMI, addr:%llx\n", _MSTAR_USB_BASEADR);
		writeb(readb((void*)(_MSTAR_USB_BASEADR+0xb18*2)) | 0x1, (void*)(_MSTAR_USB_BASEADR+0xb18*2));
		//writeb(readb((void*)(_MSTAR_USB_BASEADR+0xb18*2)) & ~0x1, (void*)(_MSTAR_USB_BASEADR+0xb18*2));
		writeb(readb((void*)(_MSTAR_USB_BASEADR+0x1e5b*2-1)) & ~0x1, (void*)(_MSTAR_USB_BASEADR+0x1e5b*2-1));
	}
#if !defined(DISABLE_SECOND_EHC)
	else if( 0==strncmp(dev->name, "Mstar-ehci-2", NAME_LEN) )
	{
		hcd->port_index = 2;
		hcd->utmi_base = _MSTAR_UTMI1_BASE;
		hcd->ehc_base = _MSTAR_UHC1_BASE;
		hcd->usbc_base = _MSTAR_USBC1_BASE;
		hcd->bc_base = _MSTAR_BC1_BASE;
		#ifdef _MSTAR_EHC1_COMP_PORT
		hcd->companion = ehc1_comp;
		#endif
		#ifdef ENABLE_IRQ_REMAP
		irq = MSTAR_EHC2_IRQ;
		#endif
	}
#endif
#ifdef ENABLE_THIRD_EHC
	else if( 0==strncmp(dev->name, "Mstar-ehci-3", NAME_LEN) )
	{
		hcd->port_index = 3;
		hcd->utmi_base = _MSTAR_UTMI2_BASE;
		hcd->ehc_base = _MSTAR_UHC2_BASE;
		hcd->usbc_base = _MSTAR_USBC2_BASE;
		hcd->bc_base = _MSTAR_BC2_BASE;
		#ifdef _MSTAR_EHC2_COMP_PORT
		hcd->companion = ehc2_comp;
		#endif
		#ifdef ENABLE_IRQ_REMAP
		irq = MSTAR_EHC3_IRQ;
		#endif
	}
#endif
#ifdef ENABLE_FOURTH_EHC
	else if( 0==strncmp(dev->name, "Mstar-ehci-4", NAME_LEN) )
	{
		hcd->port_index = 4;
		hcd->utmi_base = _MSTAR_UTMI3_BASE;
		hcd->ehc_base = _MSTAR_UHC3_BASE;
		hcd->usbc_base = _MSTAR_USBC3_BASE;
		hcd->bc_base = _MSTAR_BC3_BASE;
		#ifdef _MSTAR_EHC3_COMP_PORT
		hcd->companion = ehc3_comp;
		#endif
		#ifdef ENABLE_IRQ_REMAP
		irq = MSTAR_EHC4_IRQ;
		#endif
	}
#endif
#ifdef ENABLE_FIFTH_EHC
	else if( 0==strncmp(dev->name, "Mstar-ehci-5", NAME_LEN) )
	{
		hcd->port_index = 5;
		hcd->utmi_base = _MSTAR_UTMI4_BASE;
		hcd->ehc_base = _MSTAR_UHC4_BASE;
		hcd->usbc_base = _MSTAR_USBC4_BASE;
		hcd->bc_base = _MSTAR_BC4_BASE;
		#ifdef _MSTAR_EHC4_COMP_PORT
		hcd->companion = ehc4_comp;
		#endif
		#ifdef ENABLE_IRQ_REMAP
		irq = MSTAR_EHC5_IRQ;
		#endif
	}
#endif

	/* utmi parameter from dts or by default */
	set_utmi_param(hcd, dev->dev.of_node);

	ehci_mstar_hw_init(hcd);

	hcd->rsrc_start = hcd->ehc_base;
	hcd->rsrc_len = (0xfe<<1);
	hcd->has_tt = 1;

	hcd->regs = (void *)hcd->rsrc_start; // tony
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err1;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = (struct ehci_regs *)((uintptr_t)hcd->regs + HC_LENGTH(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase)));

	//printk("\nDean: [%s] ehci->regs: 0x%x\n", __FILE__, (unsigned int)ehci->regs);
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

#if _UTMI_PWR_SAV_MODE_ENABLE
	usb_power_saving_enable(hcd, true);
#endif

#ifdef USB_MAC_SRAM_POWER_DOWN_ENABLE
	usb20mac_sram_power_saving(hcd, true);
#endif

	printk(KERN_INFO "[USB] %s irq --> %d\n", dev->name, irq);

    /* IRQF_DISABLED was removed from kernel 4.1
       commit d8bf368d0631d4bc2612d8bf2e4e8e74e620d0cc. */
	retval = usb_add_hcd(hcd, irq, 0);

	hcd->root_port_devnum=0;
	hcd->enum_port_flag=0;
	hcd->enum_dbreset_flag=0;

	//usb_add_hcd(hcd, dev->resource[2].start, IRQF_DISABLED | IRQF_SHARED);
	if (retval == 0)
		return retval;
err1:
	usb_put_hcd(hcd);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_ehci_hcd_au1xxx_remove - shutdown processing for Au1xxx-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_ehci_hcd_au1xxx_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
static int ehci_hcd_mstar_drv_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	return 0;
}

#ifdef CONFIG_PM
static int ehci_hcd_mstar_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	printk("ehci_hcd_mstar_platform_suspend...port %d\n", hcd->port_index);
	#if defined(ENABLE_MONITOR_LINE_STATE_IN_STR)
	mstar_lib_clear_linestate_chg(hcd);
	#endif
	return ehci_suspend(hcd, false);
}

static int ehci_hcd_mstar_drv_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	printk("ehci_hcd_mstar_platform_resume...port %d\n", hcd->port_index);

	if (ehci_readl(ehci, &ehci->regs->configured_flag) != FLAG_CF)
		ehci_mstar_hw_init(hcd);

	ehci_resume(hcd, false);
	return 0;
}
#endif

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_OF)
static struct of_device_id mstar_ehci_1_of_device_ids[] = {
	{.compatible = "rm-Mstar-ehci-1"},
	{},
};

#if !defined(DISABLE_SECOND_EHC)
static struct of_device_id mstar_ehci_2_of_device_ids[] = {
	{.compatible = "rm-Mstar-ehci-2"},
	{},
};
#endif

#ifdef ENABLE_THIRD_EHC
static struct of_device_id mstar_ehci_3_of_device_ids[] = {
	{.compatible = "rm-Mstar-ehci-3"},
	{},
};
#endif

#ifdef ENABLE_FOURTH_EHC
static struct of_device_id mstar_ehci_4_of_device_ids[] = {
	{.compatible = "rm-Mstar-ehci-4"},
	{},
};
#endif

#ifdef ENABLE_FIFTH_EHC
static struct of_device_id mstar_ehci_5_of_device_ids[] = {
	{.compatible = "rm-Mstar-ehci-5"},
	{},
};
#endif
#endif

static struct platform_driver ehci_hcd_mstar_haps_driver = {
	.probe 		= ehci_hcd_mstar_drv_probe,
	.remove 	= ehci_hcd_mstar_drv_remove,
#ifdef CONFIG_PM
	.suspend	= ehci_hcd_mstar_drv_suspend,
	.resume		= ehci_hcd_mstar_drv_resume,
#endif
	.driver = {
		.name	= "Mstar-ehci-haps-1",
#if defined(CONFIG_OF)
		.of_match_table = mstar_ehci_1_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
	}
};

static struct platform_driver ehci_hcd_mstar_driver = {
	.probe 		= ehci_hcd_mstar_drv_probe,
	.remove 	= ehci_hcd_mstar_drv_remove,
#ifdef CONFIG_PM
	.suspend	= ehci_hcd_mstar_drv_suspend,
	.resume		= ehci_hcd_mstar_drv_resume,
#endif
	.driver = {
		.name	= "rm-Mstar-ehci-1",
#if defined(CONFIG_OF)
		.of_match_table = mstar_ehci_1_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
	}
};
#if !defined(DISABLE_SECOND_EHC)
static struct platform_driver second_ehci_hcd_mstar_driver = {
	.probe 		= ehci_hcd_mstar_drv_probe,
	.remove 	= ehci_hcd_mstar_drv_remove,
#ifdef CONFIG_PM
	.suspend	= ehci_hcd_mstar_drv_suspend,
	.resume		= ehci_hcd_mstar_drv_resume,
#endif
	.driver = {
		.name 	= "rm-Mstar-ehci-2",
#if defined(CONFIG_OF)
		.of_match_table = mstar_ehci_2_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
	}
};
#endif
#ifdef ENABLE_THIRD_EHC
static struct platform_driver third_ehci_hcd_mstar_driver = {
	.probe 		= ehci_hcd_mstar_drv_probe,
	.remove 	= ehci_hcd_mstar_drv_remove,
#ifdef CONFIG_PM
	.suspend	= ehci_hcd_mstar_drv_suspend,
	.resume		= ehci_hcd_mstar_drv_resume,
#endif
	.driver = {
		.name 	= "rm-Mstar-ehci-3",
#if defined(CONFIG_OF)
		.of_match_table = mstar_ehci_3_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
	}
};
#endif
#ifdef ENABLE_FOURTH_EHC
static struct platform_driver fourth_ehci_hcd_mstar_driver = {
	.probe 		= ehci_hcd_mstar_drv_probe,
	.remove 	= ehci_hcd_mstar_drv_remove,
#ifdef CONFIG_PM
	.suspend	= ehci_hcd_mstar_drv_suspend,
	.resume		= ehci_hcd_mstar_drv_resume,
#endif
	.driver = {
		.name 	= "rm-Mstar-ehci-4",
#if defined(CONFIG_OF)
		.of_match_table = mstar_ehci_4_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
	}
};
#endif
#ifdef ENABLE_FIFTH_EHC
static struct platform_driver fifth_ehci_hcd_mstar_driver = {
	.probe 		= ehci_hcd_mstar_drv_probe,
	.remove 	= ehci_hcd_mstar_drv_remove,
#ifdef CONFIG_PM
	.suspend	= ehci_hcd_mstar_drv_suspend,
	.resume		= ehci_hcd_mstar_drv_resume,
#endif
	.driver = {
		.name 	= "rm-Mstar-ehci-5",
#if defined(CONFIG_OF)
		.of_match_table = mstar_ehci_5_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
	}
};
#endif

#ifdef USB_MSTAR_BDMA
void m_BDMA_write(unsigned int s, unsigned int t)
{
	int s_miu1, t_miu1;

        //printk("[BDMA] s %x, t %x\n", s, t);
	/* decide which miu and calculate physical address */
	s_miu1 = (s >= MIU1_PHY_BASE_ADDR) ? 1 : 0;
	t_miu1 = (t >= MIU1_PHY_BASE_ADDR) ? 1 : 0;
	s = s_miu1 ? (s - MIU1_PHY_BASE_ADDR) : s;
	t = t_miu1 ? (t - MIU1_PHY_BASE_ADDR) : t;

	if (s_miu1)
		if (t_miu1)
			MDrv_BDMA_CH1_MemCopy_MIU1toMIU1(s, t, 4);
		else
			MDrv_BDMA_CH1_MemCopy_MIU1toMIU0(s, t, 4);
	else
		if (t_miu1)
			MDrv_BDMA_CH1_MemCopy_MIU0toMIU1(s, t, 4);
		else
			MDrv_BDMA_CH1_MemCopy_MIU0toMIU0(s, t, 4);

	//printk("[BDMA] copy end!\n");
}

void set_64bit_OBF_cipher(void)
{
	int retv = 0;
	unsigned int tmp_t, tmp1_t;

	tmp_t = readl((void*)(MIU0_RIU_BASE+MIU_DRAMOBF_READY_OFFSET));
	tmp1_t = readl((void*)(MIU0_RIU_BASE+MIU_64BIT_CIPHER_OFFSET));
	//printk("[MIU0] offset(2A) = %x\n", tmp_t);
	//printk("[MIU0] offset(D8) = %x\n", tmp1_t);
	if ((tmp_t & MIU_DRAMOBF_READY_BIT) != 0 &&
		(tmp1_t & MIU_64BIT_CIPHER_BIT) != 0)
		retv = 1;
	else
		retv = 0;

	#if defined(EHCI_CHECK_MIU1) && (EHCI_CHECK_MIU1 == 1)
	tmp_t = readl((void*)(MIU1_RIU_BASE+MIU_DRAMOBF_READY_OFFSET));
	tmp1_t = readl((void*)(MIU1_RIU_BASE+MIU_64BIT_CIPHER_OFFSET));
	//printk("[MIU1] offset(2A) = %x\n", tmp_t);
	//printk("[MIU1] offset(D8) = %x\n", tmp1_t);
	if ((tmp_t & MIU_DRAMOBF_READY_BIT) != 0 &&
		(tmp1_t & MIU_64BIT_CIPHER_BIT) != 0)
		retv = retv ? 1 : 0;
	else
		retv = 0;
	#endif

	en_64bit_OBF_cipher = retv;
	printk("[MIU] 64-bit OBF cipher enabled!\n");

	#if defined(EHCI_CHECK_ECO_VER) && (EHCI_CHECK_ECO_VER == 1)
	tmp_t = readl((void*)(CHIP_VER_TOP+CHIP_VER_OFFSET));
	tmp_t = (tmp_t >> CHIP_VER_SHIFT) & CHIP_VER_MASK;
	if (tmp_t >= CHIP_BDMA_ECO_VER)
	;
	else {
		printk("[BDMA] Chip ECO version %d NOT correct!\n", tmp_t);
		BUG();
	}
	#endif
}

int get_64bit_OBF_cipher(void)
{
	return en_64bit_OBF_cipher;
}
#endif

#define HAPS_PLATFORM_DRIVER ehci_hcd_mstar_haps_driver
#define usb_dma_limit ((phys_addr_t)~0)
static u64 ehci_dmamask = usb_dma_limit;

static struct resource Mstar_usb_ehci_resources[] =
{
	[2] =
	{
		.start	= E_IRQ_UHC,
		.end	= E_IRQ_UHC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device Mstar_usb_ehci_device =
{
	.name           = "Mstar-ehci-haps-1",
	.id             = 0,
	.dev =
	{
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= usb_dma_limit, //add for limit DMA range
	},
	.num_resources	= ARRAY_SIZE(Mstar_usb_ehci_resources),
	.resource	= Mstar_usb_ehci_resources,
};

static struct platform_device *Mstar_usb_platform_devices[] = {
	&Mstar_usb_ehci_device,
};

static int __init ehci_hcd_haps_init(void)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

#if (MP_USB_MSTAR==1)
	ehci_init_driver(&ehci_mstar_hc_driver, NULL);
#endif
	printk(KERN_INFO "%s: " DRIVER_DESC "\n", hcd_name);
	set_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
	if (test_bit(USB_UHCI_LOADED, &usb_hcds_loaded) ||
			test_bit(USB_OHCI_LOADED, &usb_hcds_loaded))
		printk(KERN_WARNING "Warning! ehci_hcd should always be loaded"
				" before uhci_hcd and ohci_hcd, not after\n");

	pr_debug("%s: block sizes: qh %Zd qtd %Zd itd %Zd sitd %Zd\n",
		 hcd_name,
		 sizeof(struct ehci_qh), sizeof(struct ehci_qtd),
		 sizeof(struct ehci_itd), sizeof(struct ehci_sitd));

#ifdef CONFIG_DYNAMIC_DEBUG
	ehci_debug_root = debugfs_create_dir("ehci", usb_debug_root);
	if (!ehci_debug_root) {
		retval = -ENOENT;
		goto err_debug;
	}
#endif

#ifdef HAPS_PLATFORM_DRIVER
	printk("Mstar_ehc_init version:%s\n", EHCI_MSTAR_VERSION);

#ifdef USB_MSTAR_BDMA
	set_64bit_OBF_cipher();
#endif

	/* HAPS */
	printk("USB platform_add_devices\n");
	retval = platform_add_devices(Mstar_usb_platform_devices, ARRAY_SIZE(Mstar_usb_platform_devices));
	if (retval) {
		pr_info("[USB] Fail to add the platform device %d\n", retval);
		return retval;
	}

	retval = platform_driver_register(&HAPS_PLATFORM_DRIVER);
	if (retval < 0)
		goto clean0;
#endif

	return retval;

#ifdef HAPS_PLATFORM_DRIVER
	platform_driver_unregister(&HAPS_PLATFORM_DRIVER);
clean0:
#endif
#ifdef CONFIG_DYNAMIC_DEBUG
	debugfs_remove(ehci_debug_root);
	ehci_debug_root = NULL;
err_debug:
#endif
	clear_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
	return retval;
}
module_init(ehci_hcd_haps_init);

static void __exit ehci_hcd_haps_cleanup(void)
{
#ifdef HAPS_PLATFORM_DRIVER
	platform_driver_unregister(&HAPS_PLATFORM_DRIVER);
#endif
#ifdef CONFIG_DYNAMIC_DEBUG
	debugfs_remove(ehci_debug_root);
#endif
	clear_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
}
module_exit(ehci_hcd_haps_cleanup);
