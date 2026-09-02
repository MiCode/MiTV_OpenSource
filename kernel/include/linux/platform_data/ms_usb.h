/*
 * Copyright (C) 2011 Mstar Semiconductor Inc. All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MS_PLATFORM_USB_H
#define __MS_PLATFORM_USB_H

enum {
	MS_USB_MODE_OTG,
	MS_USB_MODE_HOST,
};

enum {
	VBUS_LOW	= 0,
	VBUS_HIGH	= 1 << 0,
};

struct ms_usb_addon_irq {
	unsigned int	irq;
	int		(*poll)(void);
};

struct ms_usb_platform_data {
	unsigned int		clknum;
	char			**clkname;
	struct ms_usb_addon_irq	*id;	/* Only valid for OTG. ID pin change*/
	struct ms_usb_addon_irq	*vbus;	/* valid for OTG/UDC. VBUS change*/

	/* only valid for HCD. OTG or Host only*/
	unsigned int		mode;

	/* This flag is used for that needs id pin checked by otg */
	unsigned int    disable_otg_clock_gating:1;
	/* Force a_bus_req to be asserted */
	 unsigned int    otg_force_a_bus_req:1;

	int	(*phy_init)(void __iomem *regbase);
	void	(*phy_deinit)(void __iomem *regbase);
	int	(*set_vbus)(unsigned int vbus);
	int     (*private_init)(void __iomem *opregs, void __iomem *phyregs);
};

#ifndef CONFIG_HAVE_CLK
/* Dummy stub for clk framework */
#define clk_get(dev, id)       NULL
#define clk_put(clock)         do {} while (0)
#define clk_enable(clock)      do {} while (0)
#define clk_disable(clock)     do {} while (0)
#endif

#endif
