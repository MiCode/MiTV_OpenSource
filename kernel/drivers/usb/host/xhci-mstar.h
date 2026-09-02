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

#ifndef _XHCI_MSTAR_H
#define _XHCI_MSTAR_H

#define XHCI_MSTAR_VERSION "20200811"

#include <xhci-mstar-41929.h>

struct u3phy_addr_base {
	uintptr_t	utmi_base;
	uintptr_t	bc_base;
	uintptr_t	u3top_base;
	uintptr_t	xhci_base;
	uintptr_t	u3dtop_base;
	uintptr_t	u3atop_base;
	uintptr_t	u3dtop1_base; /* XHCI 2 ports */
	uintptr_t	u3atop1_base;
#ifdef _MSTAR_U3PHY_DTOP_EXT_BASE
	uintptr_t	u3dtop_ext_base;
#endif
};

struct u3phy_ssc {
	__le16 ssc_tx_synth_c0;
	__le16 ssc_tx_synth_c2;
	__le16 ssc_tx_synth_c4;
	__le16 ssc_tx_synth_c6;
};

struct u3phy_tx_current {
	__le16 tx_curr_60;
	__le16 tx_curr_62;
	__le16 tx_curr_64;
	__le16 tx_curr_66;
};

/* ---- Mstar XHCI Flag ----- */
#define XHCFLAG_NONE			0x0
#define XHCFLAG_DEGRADATION		0x1000
#define XHCFLAG_DISABLE_COMPLIANCE	0x2000

struct xhci_hcd_mstar {
	struct device			*dev;
	struct usb_hcd			*hcd;
	struct u3phy_addr_base		*u3phy;
	struct u3phy_ssc		*u3_ssc;
	struct u3phy_tx_current		*u3_tx_current;
	unsigned int			xhc_flag;
};

static inline struct xhci_hcd_mstar *hcd_to_mstar(struct usb_hcd *hcd)
{
	return dev_get_drvdata(hcd->self.controller);
}

#endif	/* _XHCI_MSTAR_H */
