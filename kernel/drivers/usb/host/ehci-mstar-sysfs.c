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

// Not verified functions, just test

#if (MP_USB_MSTAR==1) && defined(CONFIG_USB_EHCI_SUSPEND_PORT)
static ssize_t show_port_suspend(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ehci_hcd		*ehci;
	u32 __iomem	*reg;
	u32		status;
	unsigned isSuspend;

	ehci = hcd_to_ehci(dev_get_drvdata(dev));
	reg = &ehci->regs->port_status[0];
	status = ehci_readl(ehci, reg);
	if (status & 0x80)
		isSuspend = 1;
	else
		isSuspend = 0;

	return sprintf(buf, "%d\n", isSuspend);;
}

static ssize_t set_port_suspend(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct ehci_hcd	*ehci;
	ssize_t			ret;
	int				config;
	u32 __iomem		*reg;
	u32				status;

	if ( sscanf(buf, "%d", &config) != 1 )
		return -EINVAL;

	ehci = hcd_to_ehci(dev_get_drvdata(dev));
	reg = &ehci->regs->port_status[0];
	status = ehci_readl(ehci, reg);

	if (config == 1)
	{
		if ( !(status & PORT_SUSPEND) && (status & PORT_CONNECT) )
		{
			//printk("ehci suspend\n");
			ehci_writel(ehci, status | PORT_SUSPEND, reg);
		}
	}
	else
	{
		if ( status & PORT_SUSPEND )
		{
			//printk("ehci port reset\n");
			ehci_writel(ehci, status | (PORT_RESET |PORT_RESUME), reg);
			msleep(70);
			ehci_writel(ehci, status & ~(PORT_RESET|PORT_RESUME), reg);
		}
	}

	ret = count;
	return ret;
}
static DEVICE_ATTR(port_suspend, 0644, show_port_suspend, set_port_suspend);
#endif

