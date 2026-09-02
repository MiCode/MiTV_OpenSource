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

#include "xhci.h"
#include "xhci-mstar.h"

static int bP0VbusSetting=0;
#ifdef XHCI_2PORTS
static int bP1VbusSetting=0;
#endif

/* Display the SS port0 status  */
static ssize_t show_ss0vbus(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	int		count;
	char	*ptr = buf;

	count = scnprintf(ptr, 4, "%d\n", bP0VbusSetting);
	return count;
}

/* Turn the SS port0 on or off  */
static ssize_t store_ss0vbus(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int	bOn;
	struct usb_hcd *hcd;
	u16	addr_w, bit_num;
	uintptr_t	addr, gpio_addr;
	u8  value, low_active;

	if (sscanf(buf, "%d", &bOn) != 1)
		return -EINVAL;

	if (bOn)
		bOn = 1;
	bP0VbusSetting = bOn;

	hcd = bus_to_hcd(dev_get_drvdata(dev));
	addr_w = readw((void*)(hcd->u3top_base+0xFC*2));
	addr = (uintptr_t)addr_w << 8;
	addr_w = readw((void*)(hcd->u3top_base+0xFE*2));
	addr |= addr_w & 0xFF;
	bit_num = (addr_w >> 8) & 0x7;
	low_active = (u8)((addr_w >> 11) & 0x1);

	if (addr)
	{
		printk("ss0vbus: %d\n", bOn);
		printk("Addr: 0x%lx bit_num: %d low_active:%d\n", addr, bit_num, low_active);

		value = 1 << bit_num;

		if (addr & 0x1)
			gpio_addr = _MSTAR_PM_BASE+addr*2-1;
		else
			gpio_addr = _MSTAR_PM_BASE+addr*2;

		if (low_active^bOn)
		{
			writeb(readb((void*)gpio_addr) | value, (void*)gpio_addr);
		}
		else
		{
			writeb(readb((void*)gpio_addr) & (u8)(~value), (void*)gpio_addr);
		}
	}
	else {
		printk("\n\n!!!! ERROR : xhci: no GPIO information for vbus port0 power control  !!!! \n\n");
		return -EIO;
	}

	return count;
}

static DEVICE_ATTR(ss0vbus, 0644, show_ss0vbus, store_ss0vbus);


#ifdef XHCI_2PORTS

/* Display the SS port0 status  */
static ssize_t show_ss1vbus(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	int		count;
	char	*ptr = buf;

	count = scnprintf(ptr, 4, "%d\n", bP1VbusSetting);
	return count;
}

/* Turn the SS port0 on or off  */
static ssize_t store_ss1vbus(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int	bOn;
	struct usb_hcd *hcd;
	u16	addr_w, bit_num;
	uintptr_t	addr, gpio_addr;
	u8  value, low_active;

	if (sscanf(buf, "%d", &bOn) != 1)
		return -EINVAL;

	if (bOn)
		bOn = 1;
	bP1VbusSetting = bOn;

	hcd = bus_to_hcd(dev_get_drvdata(dev));
	addr_w = readw((void*)(hcd->u3top_base+0xE6*2));
	addr = (uintptr_t)addr_w << 8;
	addr_w = readw((void*)(hcd->u3top_base+0xE8*2));
	addr |= addr_w & 0xFF;
	bit_num = (addr_w >> 8) & 0x7;
	low_active = (u8)((addr_w >> 8) & 0x8);

	if (addr)
	{
		printk("ss1vbus: %d\n", bOn);
		printk("Addr: 0x%x bit_num: %d low_active:%d\n", addr, bit_num, low_active);

		value = 1 << bit_num;

		if (addr & 0x1)
			gpio_addr = _MSTAR_PM_BASE+addr*2-1;
		else
			gpio_addr = _MSTAR_PM_BASE+addr*2;

		if (low_active^bOn)
		{
			writeb(readb((void*)gpio_addr) | value, (void*)gpio_addr);
		}
		else
		{
			writeb(readb((void*)gpio_addr) & (u8)(~value), (void*)gpio_addr);
		}
	}
	else {
		printk("\n\n!!!! ERROR : xhci: no GPIO information for vbus port1 power control  !!!! \n\n");
		return -EIO;
	}

	return count;
}

static DEVICE_ATTR(ss1vbus, 0644, show_ss1vbus, store_ss1vbus);

#endif

static ssize_t xhci_ssport_set_state(struct xhci_hcd *xhci, int portnum, int bOn)
{
	u32 temp;
	struct xhci_hub *rhub;

	rhub = &xhci->usb3_rhub;
	if (portnum >= rhub->num_ports)
		return -EINVAL;

	temp = readl(rhub->ports[portnum]->addr);
	if (bOn) {
		if ((temp & PORT_PLS_MASK) == USB_SS_PORT_LS_SS_DISABLED) {

			temp = xhci_port_state_to_neutral(temp);
			temp &= ~PORT_PLS_MASK;
			temp |= PORT_LINK_STROBE | USB_SS_PORT_LS_RX_DETECT;

			writel(temp, rhub->ports[portnum]->addr);
		}
	} else {
		if ((temp & PORT_PLS_MASK) != USB_SS_PORT_LS_SS_DISABLED) {

			temp = xhci_port_state_to_neutral(temp);
			writel(temp | PORT_PE, rhub->ports[portnum]->addr);
		}
	}

	return 0;
}

static ssize_t xhci_ssport_get_state(struct xhci_hcd *xhci, int portnum, int *state)
{
	u32 temp;
	struct xhci_hub *rhub;

	rhub = &xhci->usb3_rhub;
	if (portnum >= rhub->num_ports)
		return -EINVAL;

	temp = readl(rhub->ports[portnum]->addr);
	if ((temp & PORT_PLS_MASK) == USB_SS_PORT_LS_SS_DISABLED)
		*state = 0;
	else
		*state = 1;

	return 0;
}

/* Display the SS port0 status  */
static ssize_t show_ss0control(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct xhci_hcd		*xhci;
	int		status, count;
	ssize_t ret;
	char	*ptr = buf;

	xhci = hcd_to_xhci(bus_to_hcd(dev_get_drvdata(dev)));

	ret = xhci_ssport_get_state(xhci, 0, &status);
	if (ret < 0)
		return ret;

	count = scnprintf(ptr, 4, "%d\n", status);
	return count;
}

/* Turn the SS port0 on or off  */
static ssize_t store_ss0control(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int		bOn;
	ssize_t ret;

	hcd = bus_to_hcd(dev_get_drvdata(dev));
	xhci = hcd_to_xhci(hcd);
	if (sscanf(buf, "%d", &bOn) != 1)
		return -EINVAL;

	ret = xhci_ssport_set_state(xhci, 0, bOn);
	if (ret < 0)
		return ret;

	//Record the setting to register
	if (bOn)
		writeb(readb((void*)(hcd->u3top_base+0xFF*2-1)) & (~0x10), (void*)(hcd->u3top_base+0xFF*2-1));
	else
		writeb(readb((void*)(hcd->u3top_base+0xFF*2-1)) | 0x10, (void*)(hcd->u3top_base+0xFF*2-1));

	return count;
}

static DEVICE_ATTR(ss0control, 0644, show_ss0control, store_ss0control);


#ifdef XHCI_2PORTS

/* Display the SS port1 status  */
static ssize_t show_ss1control(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct xhci_hcd		*xhci;
	int		status, count;
	ssize_t ret;
	char	*ptr = buf;

	xhci = hcd_to_xhci(bus_to_hcd(dev_get_drvdata(dev)));

	ret = xhci_ssport_get_state(xhci, 1, &status);
	if (ret < 0)
		return ret;

	count = scnprintf(ptr, 4, "%d\n", status);
	return count;
}

/* Turn the SS port1 on or off  */
static ssize_t store_ss1control(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_hcd		*hcd;
	struct xhci_hcd		*xhci;
	int		bOn;
	ssize_t ret;

	hcd = bus_to_hcd(dev_get_drvdata(dev));
	xhci = hcd_to_xhci(hcd);
	if (sscanf(buf, "%d", &bOn) != 1)
		return -EINVAL;

	ret = xhci_ssport_set_state(xhci, 1, bOn);
	if (ret < 0)
		return ret;

	//Record the setting to register
	if (bOn)
		writeb(readb((void*)(hcd->u3top_base+0xE9*2-1)) & (~0x10), (void*)(hcd->u3top_base+0xE9*2-1));
	else
		writeb(readb((void*)(hcd->u3top_base+0xE9*2-1)) | 0x10, (void*)(hcd->u3top_base+0xE9*2-1));

	return count;
}

static DEVICE_ATTR(ss1control, 0644, show_ss1control, store_ss1control);

#endif

int create_xhci_sysfs_files(struct xhci_hcd *xhci)
{
	struct device	*controller = xhci->shared_hcd->self.controller;
	int	i, ret = 0;
	struct xhci_hub *rhub;

	i = device_create_file(controller, &dev_attr_ss0control);
	if (i) {
		printk("\n\n!!!! xhci device_create_file dev_attr_ss0control fail %d !!!! \n\n", i);
		ret=1;
	}

	i = device_create_file(controller, &dev_attr_ss0vbus);
	if (i) {
		printk("\n\n!!!! xhci device_create_file dev_attr_ss0vbus fail %d !!!! \n\n", i);
		ret=1;
	}

	rhub = &xhci->usb3_rhub;
	if (rhub->num_ports == 1)
		return ret;

#ifdef XHCI_2PORTS
	i = device_create_file(controller, &dev_attr_ss1control);
	if (i) {
		printk("\n\n!!!! xhci device_create_file dev_attr_ss1control fail %d !!!! \n\n", i);
		ret=1;
	}

	i = device_create_file(controller, &dev_attr_ss1vbus);
	if (i) {
		printk("\n\n!!!! xhci device_create_file dev_attr_ss1vbus fail %d !!!! \n\n", i);
		ret=1;
	}
#endif

	return ret;
}

void remove_xhci_sysfs_files(struct xhci_hcd *xhci)
{
	struct device	*controller = xhci->shared_hcd->self.controller;
#ifdef XHCI_2PORTS
	struct xhci_hub *rhub;
#endif

	device_remove_file(controller, &dev_attr_ss0control);
	device_remove_file(controller, &dev_attr_ss0vbus);

#ifdef XHCI_2PORTS
	rhub = &xhci->usb3_rhub;
	if (rhub->num_ports == 1)
		return;

	device_remove_file(controller, &dev_attr_ss1control);
	device_remove_file(controller, &dev_attr_ss1vbus);
#endif
}
