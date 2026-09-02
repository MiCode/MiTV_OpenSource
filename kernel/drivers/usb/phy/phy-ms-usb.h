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

#ifndef	__MS_USB_OTG_CONTROLLER__
#define	__MS_USB_OTG_CONTROLLER__

#include <linux/types.h>

/* Command Register Bit Masks */
#define USBCMD_RUN_STOP		(0x00000001)
#define USBCMD_CTRL_RESET	(0x00000002)

#define CAPLENGTH_MASK		(0xff)

/* Timer's interval, unit ms */
#define T_A_WAIT_VRISE		100
#define T_A_WAIT_BCON		20000
#define T_A_AIDL_BDIS		1000
#define T_A_BIDL_ADIS		200
#define T_B_ASE0_BRST		4000
#define T_B_SE0_SRP		3000
#define T_B_SRP_FAIL		20000
#define T_B_DATA_PLS		100
#define T_B_SRP_INIT		1000
#define T_A_SRP_RSPNS		100
#define T_A_DRV_RSM		50

/* GPIO information */
#define GPIO_LOW_ACTIVE			0x08
#define GPIO_BIT_OFFSET_MASK	0x07

enum otg_function {
	OTG_B_DEVICE = 0,
	OTG_A_DEVICE
};

enum ms_otg_timer {
	A_WAIT_BCON_TIMER = 0,
	A_WAIT_VRISE_TIMER,
	OTG_TIMER_NUM
};

/* PXA OTG state machine */
struct ms_otg_ctrl {
	/* internal variables */
	u8 a_set_b_hnp_en; /* A-Device set b_hnp_en */
	u8 b_srp_done;
	u8 b_hnp_en;

	/* OTG inputs */
	u8 a_bus_drop;
	u8 a_bus_req;
	u8 a_clr_err;
	u8 a_bus_resume;
	u8 a_bus_suspend;
	u8 a_conn;
	u8 a_sess_vld;
	u8 a_srp_det;
	u8 a_vbus_vld;
	u8 b_bus_req; /* B-Device Require Bus */
	u8 b_bus_resume;
	u8 b_bus_suspend;
	u8 b_conn;
	u8 b_se0_srp;
	u8 b_sess_end;
	u8 b_sess_vld;
	u8 id;
	u8 a_suspend_req;

	/* Timer event */
	u8 a_aidl_bdis_timeout;
	u8 b_ase0_brst_timeout;
	u8 a_bidl_adis_timeout;
	u8 a_wait_bcon_timeout;

	struct timer_list timer[OTG_TIMER_NUM];
};

struct ms_usbc_regs {
	/* Mstar USBC register, only the LSW available */
	u32	rst_ctrl;    // 0x0
	u32	port_ctrl;   // 0x2
	u32	intr_en;     // 0x4
	u32	intr_status; // 0x6
	u32	utmi_signal; // 0x8
	u32	pwr_en;      // 0xA
	u32	pwr_status;  // 0xC
	u32	reserved;    // 0xE
};

 /* to combine USBC, UHC, and OTG */
struct ms_otg_regs {
	struct ms_usbc_regs __iomem *usbc_regs;
	void __iomem *uhc_regs;
	void __iomem *motg_regs;
	void __iomem *utmi_regs;
};

//--[USBC Reg]----------------
/* RST_CTRL */
#define  OTG_XIU         (1<<6)
#define  UHC_XIU         (1<<5)
#define  REG_SUSPEND     (1<<3)
#define  OTG_RST         (1<<2)
#define  UHC_RST         (1<<1)
#define  USB_RST         (1<<0)

/* PORT_CTRL */
#define  IDPULLUP_CTRL   (1<<4)
#define  PORTCTRL_OTG    (1<<1)
#define  PORTCTRL_UHC    (1<<0)

#define  ID_CHG_INTEN    (1<<3)
#define  ID_CHG_STS      (1<<3)
#define  IDDIG           (1<<3)

//--[EHCI Reg]----------------
#define EHC_CMD        (0x10<<1)
#define ASY_EN         (1<<5)
#define PSCH_EN        (1<<4)
#define RUN_STOP       (1<<0)
#define EHC_CMD_INT_THRC    (0x12<<1)
#define EHC_STATUS     (0x14<<1)

#define HC_HALTED      (1<<12)
#define EHC_INTR       (0x18<<1)
#define EHC_BMCS       (0x40<<1)
#define VBUS_OFF       (1<<4)
#define INT_POLARITY   (1<<3)

//--[OTG Reg]-----------------
#define OffShift 1

#define M_REG_FADDR        (0x00) /* 8 bit */
#define M_REG_POWER        (0x01) /* 8 bit */
#define M_REG_INTRTX       (0x02<<OffShift)
#define M_REG_INTRRX       (0x04<<OffShift)
#define M_REG_INTRTXE      (0x06<<OffShift)
#define M_REG_INTRRXE      (0x08<<OffShift)
#define M_REG_INTRUSB      (0x0A<<OffShift) /* 8 bit */
#define M_REG_INTRUSBE     ((0x0A<<OffShift)+1) /* 8 bit */

#define	M_POWER_SOFTCONN    0x4000 // at address 1

struct ms_otg {
	struct usb_phy phy;
	struct ms_otg_ctrl otg_ctrl;

	/* base address */
	void __iomem *phy_regs;
	void __iomem *cap_regs;
	struct ms_otg_regs __iomem *op_regs;

	struct platform_device *pdev;
	int irq; // usb int
	int irq_uhc; // uhc int
	u32 irq_status;
	u32 irq_en;

	/* state backup */
	u32 uhc_intr_status;
	u32 uhc_usbcmd_status;
	u32 uhc_usbcmd_int_thrc_status;
	u32 motg_IntrTx;
	u32 motg_IntrRx;
	u32 motg_IntrUSB;
	u32 motg_Power;

	struct delayed_work work;
	struct workqueue_struct *qwork;

	spinlock_t wq_lock;

	struct ms_usb_platform_data *pdata;

	unsigned int active;
	unsigned int clock_gating;
	struct clk *clk;

#if defined(CONFIG_OF)
	u32 pwrctl_out;
	u32 pwrctl_pin;
#if defined(VBUS_SWITCH_GPIO)
	u32 vbus_switch_gpio;
	u32 vbus_switch_active;
#endif
#endif
};
#endif
