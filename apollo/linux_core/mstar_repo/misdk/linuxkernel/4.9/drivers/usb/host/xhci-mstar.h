/*
 * xHCI host controller driver
 *
 * Copyright (C) 2013~2017 MStar Inc.
 *
 */

#ifndef _XHCI_MSTAR_H
#define _XHCI_MSTAR_H

#define XHCI_MSTAR_VERSION "20180823"

#include <xhci-mstar-40932.h>

struct u3phy_addr_base {
	uintptr_t	utmi_base;
	uintptr_t	bc_base;
	uintptr_t	u3top_base;
	uintptr_t	xhci_base;
	uintptr_t	u3dtop_base;
	uintptr_t	u3atop_base;
	uintptr_t	u3dtop1_base; /* XHCI 2 ports */
	uintptr_t	u3atop1_base;
};

/* ---- Mstar XHCI Flag ----- */
#define XHCFLAG_NONE			0x0
#define XHCFLAG_DEGRADATION		0x1000

#endif	/* _XHCI_MSTAR_H */
