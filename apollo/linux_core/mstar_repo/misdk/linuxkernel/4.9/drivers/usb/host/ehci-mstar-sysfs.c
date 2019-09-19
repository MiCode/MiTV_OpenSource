////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2006-2008 MStar Semiconductor, Inc.
// All rights reserved.
//
// Unless otherwise stipulated in writing, any and all information contained
// herein regardless in any format shall remain the sole proprietary of
// MStar Semiconductor Inc. and be kept in strict confidence
// (MStar Confidential Information) by the recipient.
// Any unauthorized act including without limitation unauthorized disclosure,
// copying, use, reproduction, sale, distribution, modification, disassembling,
// reverse engineering and compiling of the contents of MStar Confidential
// Information is unlawful and strictly prohibited. MStar hereby reserves the
// rights to any and all damages, losses, costs and expenses resulting therefrom.
//
////////////////////////////////////////////////////////////////////////////////

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


#if (MP_USB_MSTAR==1) && defined(MSTAR_WIFI_FAST_CONNECT)

static ssize_t get_fast_connect(struct device *dev,
					struct device_attribute *attr,
					const char *buf )
{
	struct usb_hcd		*hcd;
	struct ehci_hcd		*ehci;
	int				config, count;
	ssize_t			ret;

	hcd = dev_get_drvdata(dev);
	ehci = hcd_to_ehci(hcd);

	if (hcd->ms_flag & MS_FLAG_FAST_CONNECT)
		config = 1;
	else
		config = 0;

	count = scnprintf(buf, 4, "%d\n", config);
	return count;

}

static ssize_t set_fast_connect(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct usb_hcd		*hcd;
	struct ehci_hcd		*ehci;
	int				config;
	ssize_t			ret;

	hcd = dev_get_drvdata(dev);
	ehci = hcd_to_ehci(hcd);

	if (sscanf(buf, "%d", &config) != 1)
		return -EINVAL;

	if (config < 0 || config > 1) {
		ehci_err(ehci, "\n[ERR] set_fast_connect wrong number: %d \n", config);
		return -EINVAL;
	}

	if (config) {
		hcd->ms_flag |= MS_FLAG_FAST_CONNECT;
		printk("\n[USB] !! port %d enable WiFi fast connect !! \n", hcd->port_index);
	}
	else {
		hcd->ms_flag &= ~MS_FLAG_FAST_CONNECT;
		printk("\n[USB] port %d disable WiFi fast connect \n", hcd->port_index);
	}

	return count;
}


static DEVICE_ATTR(fast_connect, 0644, get_fast_connect, set_fast_connect);
#endif
