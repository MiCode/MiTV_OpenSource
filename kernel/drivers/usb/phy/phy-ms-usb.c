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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/platform_data/ms_usb.h>

#include "ms_otg.h"
#include "phy-ms-usb.h"
#include "../host/ehci-mstar.h"
#include "../gadget/udc/msb250x_udc.h"

#if defined(VBUS_SWITCH_GPIO)
#include "mdrv_gpio.h"
#include "mhal_gpio.h"
#include "mhal_gpio_reg.h"
#endif

#define	DRIVER_DESC	"MStar USB OTG transceiver driver"
#define	DRIVER_VERSION	"20190712"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

#define DEVM_KFREE(dp, mp) {if (mp) devm_kfree(dp, mp); }

static const char driver_name[] = "Mstar-otg";

static char *state_string[] = {
	"undefined",
	"b_idle",
	"b_srp_init",
	"b_peripheral",
	"b_wait_acon",
	"b_host",
	"a_idle",
	"a_wait_vrise",
	"a_wait_bcon",
	"a_host",
	"a_suspend",
	"a_peripheral",
	"a_wait_vfall",
	"a_vbus_err"
};

#if defined(CONFIG_MS_OTG_SOFTWARE_ID)

extern bool OTG_backup_bootArgu_on;
extern bool OTG_backup_id_host;
/*
OTG host/peripheral boot argument

*/
bool OTG_boot_Argument_on;
bool OTG_id_host = true;	/* default host mode */

static int __init OTG_id_setup(char *str)
{
	OTG_boot_Argument_on = true;

	if (!strncmp(str, "device", 6)) {
		OTG_id_host = false;
		pr_info("[OTG] bootarg=device\n");
	} else {
		OTG_id_host = true;
		pr_info("[OTG] bootarg=host\n");
	}

	return 1;
}

__setup("USB_OTG_SOFTWARE_ID=", OTG_id_setup);
#endif

static void otg_power_saving_enable(struct ms_otg *msotg, bool enable)
{
	if (enable) {
		//printk("utmi off\n");
		if (msotg->op_regs->utmi_regs != 0) {
			writeb(readb(msotg->op_regs->utmi_regs) | (BIT6 | BIT7),
				msotg->op_regs->utmi_regs);
			writeb(readb(msotg->op_regs->utmi_regs + 0x1 * 2 - 1) |
				(BIT2 | BIT3 | BIT6), msotg->op_regs->utmi_regs + 0x1 * 2 - 1);

			/* new HW term overwrite: on */
			writeb(readb(msotg->op_regs->utmi_regs+0x52*2) | (BIT5|BIT4|
				BIT3|BIT2|BIT1|BIT0), msotg->op_regs->utmi_regs+0x52*2);
			writeb(readb(msotg->op_regs->utmi_regs) | BIT1, msotg->op_regs->utmi_regs);
		}
	} else {
		if (msotg->op_regs->utmi_regs != 0) {
			writeb(readb(msotg->op_regs->utmi_regs) & (u8)(~(BIT1)),
				msotg->op_regs->utmi_regs);
			/* new HW term overwrite: off */
			writeb(readb(msotg->op_regs->utmi_regs+0x52*2) & (u8)(~(BIT5|BIT4|
				BIT3|BIT2|BIT1|BIT0)), msotg->op_regs->utmi_regs+0x52*2);

			writeb(readb(msotg->op_regs->utmi_regs + 0x1 * 2 - 1) &
				(u8)(~(BIT2 | BIT3 | BIT6)), msotg->op_regs->utmi_regs + 0x1 * 2 - 1);
			writeb(readb(msotg->op_regs->utmi_regs) & (u8)(~(BIT6 | BIT7)),
				msotg->op_regs->utmi_regs);
		}
	}
}

static void ms_dump_peripheral_reg(struct ms_otg *msotg)
{
	pr_info("[OTG] Dev reg power[%x] tx_int[%x] rx_int[%x] usb_int[%x]\n",
				readl(msotg->op_regs->motg_regs),
				readl(msotg->op_regs->motg_regs+M_REG_INTRTXE),
				readl(msotg->op_regs->motg_regs+M_REG_INTRRXE),
				readl(msotg->op_regs->motg_regs+M_REG_INTRUSB));
	return;
}

static void ms_dump_host_reg(struct ms_otg *msotg)
{
	pr_info("[OTG] Host reg cmd[%x][%x] status[%x] int[%x]\n",
			readl(msotg->op_regs->uhc_regs+EHC_CMD),
			readl(msotg->op_regs->uhc_regs+EHC_CMD_INT_THRC),
			readl(msotg->op_regs->uhc_regs+EHC_STATUS),
			readl(msotg->op_regs->uhc_regs+EHC_INTR));
	return;
}

static void ms_dump_usbc_reg(struct ms_otg *msotg)
{
	pr_info("[OTG] USBC reg rst[%x] port[%x]\n",
			readl(&msotg->op_regs->usbc_regs->rst_ctrl),
			readl(&msotg->op_regs->usbc_regs->port_ctrl));
	return;
}

#if defined(CONFIG_OF)

static int ms_otg_initPwrCtrlGpio(struct ms_otg *msotg)
{

	struct device_node *dn;
	unsigned int uTmp;

	msotg->pwrctl_out = 0;
	msotg->pwrctl_pin = 0;

	for_each_compatible_node(dn, NULL, msotg->phy.label) {

		if (!of_property_read_u32(dn, "pwrctl_out", &uTmp)) {
			msotg->pwrctl_out = uTmp;
			pr_info("[OTG] DTS pwrctrl o %08x\n", msotg->pwrctl_out);
		}

		if (!of_property_read_u32(dn, "pwrctl_pin", &uTmp)) {
			msotg->pwrctl_pin = uTmp;
			pr_info("[OTG] DTS pwr ctrl p %08x\n", msotg->pwrctl_pin);
		}
	}

	if (msotg->pwrctl_out == 0 && msotg->pwrctl_pin == 0) {
		pr_debug("[OTG] by defualt no pwrctrl p\n");
		return 0;
	}

	return 1;
}


static void ms_otg_SetGpio(u32 GpioCfg, bool on)
{
	uintptr_t dwReg;
	u16 wTmp;
	u8 bitoffset;
	u8 bTmp;

	wTmp = (GpioCfg >> 16) & 0xFFFF;
	/* bank + offset */
	dwReg = (uintptr_t)(((GpioCfg & 0x0FFFF) << 8) + (wTmp & 0xFF));
	/* base shift with even/odd byte*/
	dwReg = (uintptr_t)MSTAR_PM_BASE + ((dwReg << 1) - (wTmp & BIT0));
	/* pwrctl config */
	wTmp >>= 8;
	bitoffset = wTmp & GPIO_BIT_OFFSET_MASK;

	pr_debug("[OTG] Set %08X %02X\n", (unsigned int)dwReg, bitoffset);

	bTmp = readb((void *)dwReg);

	if ((on && (wTmp & GPIO_LOW_ACTIVE)) ||
				((on == 0) && ((wTmp & GPIO_LOW_ACTIVE) == 0))) {

		bTmp &= ~(BIT0 << bitoffset);

	} else {

		bTmp |= (BIT0 << bitoffset);

	}
	writeb(bTmp, (void *)dwReg);		/* set GPIO */
}

static int ms_otg_PowerControlGpioOut(struct ms_otg *msotg, bool on)
{
	if (msotg->pwrctl_out)
		ms_otg_SetGpio(msotg->pwrctl_out, on);

	return on;
}

static int ms_otg_PowerControlGpio(struct ms_otg *msotg, bool on)
{
	if (msotg->pwrctl_pin)
		ms_otg_SetGpio(msotg->pwrctl_pin, on);

	return on;
}

#if defined(VBUS_SWITCH_GPIO)
#define INVALID_USB_GPIO 512
static int ms_otg_initVbusSwitchGpio(struct ms_otg *msotg)
{
	struct device_node *dn;
	unsigned int uTmp;

	msotg->vbus_switch_gpio = -1;
	msotg->vbus_switch_active = 1;  /* default active high */

	for_each_compatible_node(dn, NULL, "mstar,gpio_otg_vbus_ctl"/*msotg->phy.label*/) {
		if (!of_property_read_u32(dn, "vbus-switch-gpio", &uTmp)) {
			if (uTmp < INVALID_USB_GPIO)	
				msotg->vbus_switch_gpio = uTmp;
			pr_info("[OTG] vbus_switch_gpio %x\n", msotg->vbus_switch_gpio);
		}

		if (!of_property_read_u32(dn, "vbus-switch-active", &uTmp)) {
			if (uTmp == 0)
				msotg->vbus_switch_active = 0;
			pr_info("[OTG] vbus_switch_active %x\n", msotg->vbus_switch_active);
		}
	}

	if (msotg->vbus_switch_gpio == -1)
		return 0;

	return 1;
}

static int ms_otg_VbusSwitchGpio(struct ms_otg *msotg, bool on)
{

	if (msotg->vbus_switch_gpio == -1)
		return 0;

	pr_info("[OTG] vbus_switch gpio %d\n", on);

	if (on ^ (bool)(!!msotg->vbus_switch_active)) {
		MDrv_GPIO_Set_Low(msotg->vbus_switch_gpio);
	} else {
		MDrv_GPIO_Set_High(msotg->vbus_switch_gpio);
	}

	return 1;
}
#endif

#endif

static int ms_otg_set_vbus(struct usb_otg *otg, bool on)
{
	struct ms_otg *msotg = container_of((struct usb_phy *)otg->phy, struct ms_otg, phy);

	pr_info("[OTG] %s\n", __func__);

#if defined(CONFIG_OF)
	if (msotg->pwrctl_pin)
		ms_otg_PowerControlGpio(msotg, on);

#if defined(VBUS_SWITCH_GPIO)
	ms_otg_VbusSwitchGpio(msotg, on);
#endif
#endif

	if (msotg->pdata == NULL)
		return on;
	else
	if (msotg->pdata->set_vbus == NULL)
		return -ENODEV;

	return on;//msotg->pdata->set_vbus(on);
}

static int ms_otg_set_host(struct usb_otg *otg,
			   struct usb_bus *host)
{
	pr_info("[OTG] %s\n", __func__);
	return 0;
}

static int ms_otg_set_peripheral(struct usb_otg *otg,
				 struct usb_gadget *gadget)
{
	pr_info("[OTG] %s\n", __func__);
	return 0;
}

static void ms_otg_run_state_machine(struct ms_otg *msotg,
				     unsigned long delay)
{
	dev_dbg(&msotg->pdev->dev, "[OTG] transceiver is updated\n");
	if (!msotg->qwork)
		return;

	queue_delayed_work(msotg->qwork, &msotg->work, delay);
	return;
}

static void ms_otg_timer_await_bcon(struct timer_list *data)
{
	struct ms_otg *msotg = from_timer(msotg, data, otg_ctrl.timer[A_WAIT_BCON_TIMER]);

	msotg->otg_ctrl.a_wait_bcon_timeout = 1;

	dev_info(&msotg->pdev->dev, "[OTG] B Device No Response!\n");

	if (spin_trylock(&msotg->wq_lock)) {
		ms_otg_run_state_machine(msotg, 0);
		spin_unlock(&msotg->wq_lock);
	}
	return;
}

static void ms_otg_timer_await_vrise(struct timer_list *data)
{
	struct ms_otg *msotg = from_timer(msotg, data, otg_ctrl.timer[A_WAIT_VRISE_TIMER]);

	msotg->otg_ctrl.a_vbus_vld = 1;

	pr_info("[OTG] V rise time up!\n");

	if (spin_trylock(&msotg->wq_lock)) {
		ms_otg_run_state_machine(msotg, 0);
		spin_unlock(&msotg->wq_lock);
	}
	return;
}

typedef void (*MS_TIMER_FN)(struct timer_list *);

MS_TIMER_FN ms_otg_timer_func[] = {
	ms_otg_timer_await_bcon,
	ms_otg_timer_await_vrise
};


static int ms_otg_cancel_timer(struct ms_otg *msotg, unsigned int id)
{
	struct timer_list *timer;

	if (id >= OTG_TIMER_NUM)
		return -EINVAL;

	timer = &msotg->otg_ctrl.timer[id];

	if (timer_pending(timer))
		del_timer(timer);

	return 0;
}

static int ms_otg_set_timer(struct ms_otg *msotg, unsigned int id,
			    unsigned long interval,
			    void (*callback) (struct timer_list *))
{
	struct timer_list *timer;

	if (id >= OTG_TIMER_NUM)
		return -EINVAL;

	timer = &msotg->otg_ctrl.timer[id];
	if (timer_pending(timer)) {
		dev_err(&msotg->pdev->dev, "[OTG] Timer%d is already running\n", id);
		return -EBUSY;
	}

	//init_timer(timer);
	//timer->data = (unsigned long) msotg;
	//timer->function = callback;
	timer->expires = jiffies + msecs_to_jiffies(interval);
	add_timer(timer);

	return 0;
}

static int ms_otg_reset(struct ms_otg *msotg)
{
	pr_info("[OTG] %s\n", __func__);
	return 0;
}

static void ms_otg_init_irq(struct ms_otg *msotg)
{
	u32 reg_t;

#if defined(CID_ENABLE)
	//switch to CID pad enable bit
	writeb(readb((msotg->op_regs->utmi_regs+(0x25*2-1))) | (BIT5), msotg->op_regs->utmi_regs+(0x25*2-1));
#endif

	/* Mstar OTG only uses ID_Change_Interrupt */
	msotg->irq_en = ID_CHG_INTEN;
	msotg->irq_status = ID_CHG_STS;

	/* clear outstanding initial interrupts */
	reg_t = readl(&msotg->op_regs->usbc_regs->intr_status);
	pr_info("[OTG] Clr USBC int sts %x\n", reg_t);
	writel(reg_t, &msotg->op_regs->usbc_regs->intr_status);

	/* enable OTG interrupts */
	writel(msotg->irq_en, &msotg->op_regs->usbc_regs->intr_en);

	/* OTG ID pin pull-up */
	reg_t = readl(&msotg->op_regs->usbc_regs->port_ctrl);
	reg_t |= IDPULLUP_CTRL;
	writel(reg_t, &msotg->op_regs->usbc_regs->port_ctrl);

	return;
}

extern void ms_ehc_reg_init(uintptr_t uhc_base);

static void ms_otg_usbc_switch(struct ms_otg *msotg, int host)
{
	u32 reg_t;
	u32 ii;

	pr_info("[OTG] %s++\n", __func__);
	mutex_lock(&otg_reset_mutex);
	/* switch between UHC/OTG IPs */
	if (host) {
		pr_info("[OTG] start host regs setting...\n");

		if ((readl(&msotg->op_regs->usbc_regs->port_ctrl) & PORTCTRL_UHC) &&
					(readl(&msotg->op_regs->usbc_regs->rst_ctrl)&UHC_XIU)) {
			pr_info("[OTG] host already\n");
			goto done;
		}

		/* disable all USBC interrupt */
		writel(0, &msotg->op_regs->usbc_regs->intr_en);

		/* OTG register accessable */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t |= (REG_SUSPEND|OTG_XIU);
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		ms_dump_peripheral_reg(msotg);

		/* backup OTG interrupt enables */
		msotg->motg_IntrTx = readl(msotg->op_regs->motg_regs+M_REG_INTRTXE);
		msotg->motg_IntrRx = readl(msotg->op_regs->motg_regs+M_REG_INTRRXE);
		msotg->motg_IntrUSB = readl(msotg->op_regs->motg_regs+M_REG_INTRUSB);

		/* clear OTG interrupt enables */
		writel(0, (msotg->op_regs->motg_regs+M_REG_INTRTXE));
		writel(0, (msotg->op_regs->motg_regs+M_REG_INTRRXE));
		writel(0, (msotg->op_regs->motg_regs+M_REG_INTRUSB));

		/* disable both UHC & OTG UTMI */
		reg_t = readl(&msotg->op_regs->usbc_regs->port_ctrl);
		reg_t &= (~(PORTCTRL_OTG|PORTCTRL_UHC));
		writel(reg_t, &msotg->op_regs->usbc_regs->port_ctrl);

#if _USB_HS_CUR_DRIVE_DM_ALLWAYS_HIGH_PATCH
		/* enable monkey test term overwrite */
		writel(readl((msotg->op_regs->utmi_regs+0x00)) | BIT1, msotg->op_regs->utmi_regs+0x00);
		/* new HW term overwrite: on */
		writeb(readb((msotg->op_regs->utmi_regs+0x52*2)) | (BIT5|BIT4|
			BIT3|BIT2|BIT1|BIT0), msotg->op_regs->utmi_regs+0x52*2);
		pr_info("[OTG] enable monkey test term overwrite 0x%X\n", readl((msotg->op_regs->utmi_regs+0x00)));
#endif
#if _UTMI_PWR_SAV_MODE_ENABLE
		otg_power_saving_enable(msotg, 1);
#endif
#if defined(ENABLE_UTMI_DYNAMIC_POWER_SAVING)
		writel(readl((msotg->op_regs->utmi_regs+0x4C*2)) | BIT2, msotg->op_regs->utmi_regs+0x4C*2);
#endif
#if defined(_DISABLE_UDC_SUBTRACT_PATCH)
		/* enable SUBTRACT ECO */
		reg_t = readl(&msotg->op_regs->usbc_regs->reserved);
		reg_t = (reg_t | (0x0100));
		writel(reg_t, &msotg->op_regs->usbc_regs->reserved);
#endif
		/* UHC register accessable */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t |= (REG_SUSPEND|UHC_XIU);
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		/* disable UHC system interrupt, reset UHC will reinit UHC
		 * interupt to "active low" which cause XIU timeout in ISR
		 */
		disable_irq(msotg->irq_uhc);

		 /* backup UHC interrupt mask */
		reg_t = readl(((msotg->op_regs->uhc_regs)+EHC_INTR));
		if (reg_t)
			msotg->uhc_intr_status = reg_t;

		/* clear UHC interrupt mask */
		writel(0, ((msotg->op_regs->uhc_regs)+EHC_INTR));

		/* backup UHC command register */
		msotg->uhc_usbcmd_status = readl((msotg->op_regs->uhc_regs+EHC_CMD));

		/* backup UHC command interrupt threshold register */
		msotg->uhc_usbcmd_int_thrc_status = readl((msotg->op_regs->uhc_regs+EHC_CMD_INT_THRC));

		/* stop UHC */
		reg_t = msotg->uhc_usbcmd_status & (~RUN_STOP);
		writel(reg_t, (msotg->op_regs->uhc_regs+EHC_CMD));

		/* until UHC halted */
		ii = 0;
		do {
			if (ii > 10)
				break; // 10ms timeout
			mdelay(1);
			reg_t = readl((msotg->op_regs->uhc_regs+EHC_STATUS));
			ii++;
		} while (!(reg_t & HC_HALTED));

		/* disable both UHC & OTG UTMI */
		reg_t = readl(&msotg->op_regs->usbc_regs->port_ctrl); // dummy?
		reg_t &= (~(PORTCTRL_OTG|PORTCTRL_UHC)); // dummy?
		writel(reg_t, &msotg->op_regs->usbc_regs->port_ctrl); // dummy?

		/* disable UHC reg accessable, read all 0 but not XIU timeout */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t &= ~UHC_XIU;
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		/* start UHC reset ... */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t |= (REG_SUSPEND|UHC_RST);
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		/* ... end UHC reset */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t &= ~UHC_RST;
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		/* enable UHC register accessable */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t |= UHC_XIU;
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		/* enable UHC UTMI */
		reg_t = readl(&msotg->op_regs->usbc_regs->port_ctrl);
		reg_t = (reg_t & (~(PORTCTRL_OTG|PORTCTRL_UHC))) | PORTCTRL_UHC;
		writel(reg_t, &msotg->op_regs->usbc_regs->port_ctrl);

		reg_t = readl((msotg->op_regs->uhc_regs+EHC_BMCS));
		reg_t &= (~VBUS_OFF);
		writel(reg_t, (msotg->op_regs->uhc_regs+EHC_BMCS));

		reg_t = readl((msotg->op_regs->uhc_regs+EHC_BMCS));
		reg_t |= INT_POLARITY;
		writel(reg_t, (msotg->op_regs->uhc_regs+EHC_BMCS));

		/* re-init extral UHC regs after UHC_RST of USBC. */
		ms_ehc_reg_init(msotg->op_regs->uhc_regs);

		/* enable ID change interrupt */
		writel(ID_CHG_INTEN, &msotg->op_regs->usbc_regs->intr_en);

		if (msotg->uhc_intr_status == 0)
			msotg->uhc_intr_status = 0x3F;

		/* restore UHC interrupt enables and command registers */
		writel(msotg->uhc_intr_status, ((msotg->op_regs->uhc_regs)+EHC_INTR));
		writel(msotg->uhc_usbcmd_int_thrc_status, ((msotg->op_regs->uhc_regs)+EHC_CMD_INT_THRC));
		writel(msotg->uhc_usbcmd_status, ((msotg->op_regs->uhc_regs)+EHC_CMD));

		ms_dump_usbc_reg(msotg);
		ms_dump_host_reg(msotg);

		/* enable UHC system interrupt */
		enable_irq(msotg->irq_uhc);
		/* mdelay(1000); */
	} else {	/* peripheral */
		pr_info("[OTG] start periphral regs setting...\n");

		if ((readl(&msotg->op_regs->usbc_regs->port_ctrl) & PORTCTRL_OTG) &&
					(readl(&msotg->op_regs->usbc_regs->rst_ctrl) & OTG_XIU)) {
			pr_info("[OTG] periphral already\n");
			goto done;
		}

		/* UHC register accessable */
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t |= (REG_SUSPEND|UHC_XIU);
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl);

		ms_dump_host_reg(msotg);

		/* backup UHC interrupt enable */
		msotg->uhc_intr_status = readl(((msotg->op_regs->uhc_regs)+EHC_INTR));

		/* clear UHC interrupt enable */
		writel(0, (msotg->op_regs->uhc_regs+EHC_INTR));

		/* backup UHC command register */
		msotg->uhc_usbcmd_status = readl((msotg->op_regs->uhc_regs+EHC_CMD));

		/* backup UHC command interrupt threshold register */
		msotg->uhc_usbcmd_int_thrc_status = readl((msotg->op_regs->uhc_regs+EHC_CMD_INT_THRC));

		/* stop UHC */
		reg_t = msotg->uhc_usbcmd_status & (~RUN_STOP);
		writel(reg_t, (msotg->op_regs->uhc_regs+EHC_CMD));

		/* until UHC halted */
		ii = 0;
		do {
			if (ii > 10)
				break; // 10ms timeout
			mdelay(1);
			reg_t = readl((msotg->op_regs->uhc_regs+EHC_STATUS));
		} while (!(reg_t & HC_HALTED));

		reg_t = readl(&msotg->op_regs->usbc_regs->port_ctrl);
		reg_t &= (~(PORTCTRL_OTG|PORTCTRL_UHC));
		writel(reg_t, &msotg->op_regs->usbc_regs->port_ctrl); // disable both

#if _USB_HS_CUR_DRIVE_DM_ALLWAYS_HIGH_PATCH
		/* disable monkey test term overwrite */
		writel(readl((msotg->op_regs->utmi_regs+0x00)) & (~BIT1), msotg->op_regs->utmi_regs+0x00);
		/* new HW term overwrite: off */
		writeb(readb((msotg->op_regs->utmi_regs+0x52*2)) & (~(BIT5|BIT4|
			BIT3|BIT2|BIT1|BIT0)), msotg->op_regs->utmi_regs+0x52*2);
		pr_info("[OTG] disable monkey test term overwrite 0x%X\n", readl((msotg->op_regs->utmi_regs+0x00)));
#endif
#if _UTMI_PWR_SAV_MODE_ENABLE
		otg_power_saving_enable(msotg, 0);
#endif
#if defined(ENABLE_UTMI_DYNAMIC_POWER_SAVING)
		writel(readl((msotg->op_regs->utmi_regs+0x4C*2)) | BIT1, msotg->op_regs->utmi_regs+0x4C*2);
#endif
#if defined(_DISABLE_UDC_SUBTRACT_PATCH)
		reg_t = readl(&msotg->op_regs->usbc_regs->reserved);
		reg_t = (reg_t & (~0x0100));
		writel(reg_t, &msotg->op_regs->usbc_regs->reserved);
#endif
		reg_t = readl(&msotg->op_regs->usbc_regs->rst_ctrl);
		reg_t |= (REG_SUSPEND|OTG_XIU);
		writel(reg_t, &msotg->op_regs->usbc_regs->rst_ctrl); // OTG register accessable

		/* restore otg interrupt enables */
		writel(msotg->motg_IntrTx, (msotg->op_regs->motg_regs+M_REG_INTRTXE));
		writel(msotg->motg_IntrRx, (msotg->op_regs->motg_regs+M_REG_INTRRXE));
		writel(msotg->motg_IntrUSB & 0xff00, (msotg->op_regs->motg_regs+M_REG_INTRUSB));

		/* enable OTG UTMI */
		reg_t = readl(&msotg->op_regs->usbc_regs->port_ctrl);
		reg_t = (reg_t & (~(PORTCTRL_OTG|PORTCTRL_UHC))) | PORTCTRL_OTG;
		writel(reg_t, &msotg->op_regs->usbc_regs->port_ctrl);

		ms_dump_usbc_reg(msotg);
		ms_dump_peripheral_reg(msotg);
	}

done:
	pr_info("[OTG] %s--\n", __func__);
	mutex_unlock(&otg_reset_mutex);
	return;
}

static void ms_otg_start_host(struct ms_otg *msotg, int on)
{
	pr_info("[OTG] %s++\n", __func__);
#ifdef MS_OTG_DUMP_REGISTER
	ms_dump_host_reg(msotg);
#endif
	return;
}

static void ms_otg_start_periphrals(struct ms_otg *msotg, int on)
{
	pr_info("[OTG] %s++\n", __func__);
#ifdef MS_OTG_DUMP_REGISTER
	ms_dump_peripheral_reg(msotg);
#endif
	return;
}

static void otg_clock_enable(struct ms_otg *msotg)
{
	/*pr_info("[OTG] %s++\n", __func__);*/
	return;
}

static void otg_clock_disable(struct ms_otg *msotg)
{
	/*pr_info("[OTG] %s++\n", __func__);*/
	return;
}

static int ms_otg_enable_internal(struct ms_otg *msotg)
{
	int retval = 0;

	if (msotg->active)
		return 0;

	pr_info("[OTG] otg enabled\n");

	otg_clock_enable(msotg);

	msotg->active = 1;

	if (msotg->pdata == NULL) {
		return 0;
	}
	if (msotg->pdata->phy_init) {
		retval = msotg->pdata->phy_init(msotg->phy_regs);
		if (retval) {
			dev_err(&msotg->pdev->dev,
				"[OTG] init phy error %d\n", retval);
			otg_clock_disable(msotg);
			return retval;
		}
	}

	return 0;

}

static int ms_otg_enable(struct ms_otg *msotg)
{
	if (msotg->clock_gating)
		return ms_otg_enable_internal(msotg);

	return 0;
}

static void ms_otg_disable_internal(struct ms_otg *msotg)
{
	if (msotg->active) {
		dev_dbg(&msotg->pdev->dev, "[OTG] otg disabled\n");
		otg_clock_disable(msotg);
		msotg->active = 0;
	if (msotg->pdata == NULL) {
		return;
	}
		if (msotg->pdata->phy_deinit)
			msotg->pdata->phy_deinit(msotg->phy_regs);
	}
	return;
}

static void ms_otg_disable(struct ms_otg *msotg)
{
	if (msotg->clock_gating)
		ms_otg_disable_internal(msotg);
	return;
}

static void ms_otg_update_inputs(struct ms_otg *msotg)
{
#if !defined(CONFIG_MS_OTG_SOFTWARE_ID)
	struct ms_otg_ctrl *otg_ctrl = &msotg->otg_ctrl;
#endif
	u32 reg_t;

	reg_t = readl(&msotg->op_regs->usbc_regs->utmi_signal);
#if defined(CONFIG_MS_FORCE_DEVICE)
	/* force device mode */
	otg_ctrl->id = 1;

#elif defined(CONFIG_MS_OTG_SOFTWARE_ID)
	/* by software_ID */
	/* printk("[OTG] id %d(%s)\n", otg_ctrl->id, otg_ctrl->id ? "Device" : "Host"); */

#elif defined(_UDC_FAKE_CID_HIGH)
	/* by chip setting, force CID pin as 1, device mode */
	otg_ctrl->id = 1;

#else
	if (msotg->pdata == NULL) {
		otg_ctrl->id = !!(reg_t & IDDIG);
	} else {
		if (msotg->pdata->id)
			otg_ctrl->id = !!msotg->pdata->id->poll();
		else
			otg_ctrl->id = !!(reg_t & IDDIG);
	}
#endif
#ifdef MS_OTG_DUMP_REGISTER
	pr_info("[OTG] update inputs, utmi_sig %x, id %d\n", reg_t, otg_ctrl->id);
#endif
	return;
}

static void ms_otg_update_state(struct ms_otg *msotg)
{
	struct ms_otg_ctrl *otg_ctrl = &msotg->otg_ctrl;
	struct usb_phy *phy = &msotg->phy;
	struct usb_otg *otg = phy->otg;
	int old_state = otg->state;

	switch (old_state) {
	case OTG_STATE_UNDEFINED:
		otg->state = OTG_STATE_B_IDLE;
		/* FALL THROUGH */
	case OTG_STATE_B_IDLE:
		if (otg_ctrl->id == 0)
			otg->state = OTG_STATE_A_IDLE;
		else
			otg->state = OTG_STATE_B_PERIPHERAL;
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (otg_ctrl->id == 0)
			otg->state = OTG_STATE_B_IDLE;
		break;
	case OTG_STATE_A_IDLE:
		if (otg_ctrl->id)
			otg->state = OTG_STATE_B_IDLE;
		else
			otg->state = OTG_STATE_A_WAIT_VRISE;
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (otg_ctrl->a_vbus_vld)
			otg->state = OTG_STATE_A_WAIT_BCON;
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (otg_ctrl->id) {
			ms_otg_cancel_timer(msotg, A_WAIT_BCON_TIMER);
			msotg->otg_ctrl.a_wait_bcon_timeout = 0;
			otg->state = OTG_STATE_A_WAIT_VFALL;
			otg_ctrl->a_bus_req = 0;
		} else
		if (otg_ctrl->b_conn) {
			ms_otg_cancel_timer(msotg, A_WAIT_BCON_TIMER);
			msotg->otg_ctrl.a_wait_bcon_timeout = 0;
			otg->state = OTG_STATE_A_HOST;
		}
		break;
	case OTG_STATE_A_HOST:
		if (otg_ctrl->id || !otg_ctrl->b_conn)
			otg->state = OTG_STATE_A_WAIT_BCON;
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (otg_ctrl->id)
			otg->state = OTG_STATE_A_IDLE;
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (otg_ctrl->id) {
			otg->state = OTG_STATE_A_WAIT_VFALL;
		}
		break;
	default:
		break;
	}
	return;
}

static void ms_otg_work(struct work_struct *work)
{
	struct ms_otg *msotg;
	struct usb_phy *phy;
	struct usb_otg *otg;
	int old_state, i;

	msotg = container_of(to_delayed_work(work), struct ms_otg, work);
	pr_info("[OTG] %s++, active = %d\n", __func__, msotg->active);
run:
	/* work queue is single thread, or we need spin_lock to protect */
	phy = &msotg->phy;
	otg = phy->otg;
	old_state = otg->state;

	if (!msotg->active)
		return;

	ms_otg_update_inputs(msotg);
	ms_otg_update_state(msotg);

	if (old_state != otg->state) {
		pr_info("[OTG] change from state %s to %s\n",
			 state_string[old_state],
			 state_string[otg->state]);

		switch (otg->state) {
		case OTG_STATE_B_IDLE:
			otg->default_a = 0;
			if (old_state == OTG_STATE_B_PERIPHERAL)
				ms_otg_start_periphrals(msotg, 0);
			ms_otg_reset(msotg);
			ms_otg_disable(msotg);
			break;
		case OTG_STATE_B_PERIPHERAL:
			pr_info("[OTG] @ OTG_STATE_B_PERIPHERAL\n");
			ms_otg_set_vbus(otg, 0);
			ms_otg_enable(msotg);
			ms_otg_usbc_switch(msotg, 0);
			ms_otg_start_periphrals(msotg, 1);
			break;
		case OTG_STATE_A_IDLE:
			otg->default_a = 1;
			ms_otg_enable(msotg);
			if (old_state == OTG_STATE_A_WAIT_VFALL)
				ms_otg_start_host(msotg, 0);
			ms_otg_reset(msotg);
			break;
		case OTG_STATE_A_WAIT_VRISE:
			i = ms_otg_set_vbus(otg, 1);
			pr_info("[OTG] vbus:%d\n", i);
			ms_otg_set_timer(msotg, A_WAIT_VRISE_TIMER,
					 T_A_WAIT_VRISE,
					 ms_otg_timer_await_vrise);
			break;
		case OTG_STATE_A_WAIT_BCON:
			if (old_state != OTG_STATE_A_HOST)
				ms_otg_start_host(msotg, 1);
			ms_otg_set_timer(msotg, A_WAIT_BCON_TIMER,
					 T_A_WAIT_BCON,
					 ms_otg_timer_await_bcon);
			/*
			 * Now, we directly enter A_HOST. So set b_conn = 1
			 * here. In fact, it need host driver to notify us.
			 */
			msotg->otg_ctrl.b_conn = 1;
			break;
		case OTG_STATE_A_HOST:
			pr_info("[OTG] @ OTG_STATE_A_HOST\n");
			ms_otg_usbc_switch(msotg, 1);
			break;
		case OTG_STATE_A_WAIT_VFALL:
			/*
			 * Now, we has exited A_HOST. So set b_conn = 0
			 * here. In fact, it need host driver to notify us.
			 */
			msotg->otg_ctrl.b_conn = 0;
			msotg->otg_ctrl.a_vbus_vld = 0;
			i = ms_otg_set_vbus(otg, 0);
			printk("[OTG] vbus:%d\n", i);
			break;
		case OTG_STATE_A_VBUS_ERR:
			break;
		default:
			break;
		}
		goto run;
	}
	return;
}

static irqreturn_t ms_otg_irq(int irq, void *dev)
{
	struct ms_otg *msotg = dev;
	u32 reg_t;

	/* preserve and clear interrupt status */
	reg_t = readl(&msotg->op_regs->usbc_regs->intr_status);
	writel(reg_t, &msotg->op_regs->usbc_regs->intr_status);
	msotg->irq_status = reg_t;
	pr_info("[OTG] %s, int_status = %x\n", __func__, reg_t);

	if ((reg_t & msotg->irq_status) == 0)
		return IRQ_NONE;

	ms_otg_run_state_machine(msotg, HZ/4); // 250ms debounce

	return IRQ_HANDLED;
}

static irqreturn_t ms_otg_inputs_irq(int irq, void *dev)
{
	pr_info("[OTG] %s++\n", __func__);
	return IRQ_HANDLED;
}

static ssize_t
get_a_bus_req(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 msotg->otg_ctrl.a_bus_req);
}

static ssize_t
set_a_bus_req(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);

	if (count > 2)
		return -1;

	/* We will use this interface to change to A device */
	if (msotg->phy.otg->state != OTG_STATE_B_IDLE
	    && msotg->phy.otg->state != OTG_STATE_A_IDLE)
		return -1;

	/* The clock may disabled and we need to set irq for ID detected */
	ms_otg_enable(msotg);
	ms_otg_init_irq(msotg);

	if (buf[0] == '1') {
		msotg->otg_ctrl.a_bus_req = 1;
		msotg->otg_ctrl.a_bus_drop = 0;
		dev_dbg(&msotg->pdev->dev,
			"[OTG] User request: a_bus_req = 1\n");

		if (spin_trylock(&msotg->wq_lock)) {
			ms_otg_run_state_machine(msotg, 0);
			spin_unlock(&msotg->wq_lock);
		}
	}

	return count;
}

static DEVICE_ATTR(a_bus_req, S_IRUGO | S_IWUSR, get_a_bus_req,
		   set_a_bus_req);

static ssize_t
set_a_clr_err(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);
	if (!msotg->phy.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '1') {
		msotg->otg_ctrl.a_clr_err = 1;
		dev_dbg(&msotg->pdev->dev,
			"[OTG] User request: a_clr_err = 1\n");
	}

	if (spin_trylock(&msotg->wq_lock)) {
		ms_otg_run_state_machine(msotg, 0);
		spin_unlock(&msotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(a_clr_err, S_IWUSR, NULL, set_a_clr_err);

static ssize_t
get_a_bus_drop(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 msotg->otg_ctrl.a_bus_drop);
}

static ssize_t
set_a_bus_drop(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);
	if (!msotg->phy.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		msotg->otg_ctrl.a_bus_drop = 0;
		dev_dbg(&msotg->pdev->dev,
			"[OTG] User request: a_bus_drop = 0\n");
	} else if (buf[0] == '1') {
		msotg->otg_ctrl.a_bus_drop = 1;
		msotg->otg_ctrl.a_bus_req = 0;
		dev_dbg(&msotg->pdev->dev,
			"[OTG] User request: a_bus_drop = 1\n");
		dev_dbg(&msotg->pdev->dev,
			"[OTG] User request: and a_bus_req = 0\n");
	}

	if (spin_trylock(&msotg->wq_lock)) {
		ms_otg_run_state_machine(msotg, 0);
		spin_unlock(&msotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(a_bus_drop, S_IRUGO | S_IWUSR,
		   get_a_bus_drop, set_a_bus_drop);

static struct attribute *inputs_attrs[] = {
	&dev_attr_a_bus_req.attr,
	&dev_attr_a_clr_err.attr,
	&dev_attr_a_bus_drop.attr,
	NULL,
};

static struct attribute_group inputs_attr_group = {
	.name = "inputs",
	.attrs = inputs_attrs,
};

#if defined(CONFIG_MS_OTG_SOFTWARE_ID)
static ssize_t
get_software_id(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 msotg->otg_ctrl.id);
}

static ssize_t
set_software_id(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);

	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		msotg->otg_ctrl.id = 0;
		pr_info("[OTG] User request: set ID = 0 (host)\n");
	} else if (buf[0] == '1') {
		msotg->otg_ctrl.id = 1;
		pr_info("[OTG] User request: set ID = 1 (device)\n");
	} else {
		return -1;
	}

	if (spin_trylock(&msotg->wq_lock)) {
		ms_otg_run_state_machine(msotg, 0);
		spin_unlock(&msotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(software_id, S_IRUGO | S_IWUSR, get_software_id, set_software_id);
#endif

static ssize_t
get_id(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct ms_otg *msotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 msotg->otg_ctrl.id);
}

static DEVICE_ATTR(id, S_IRUGO, get_id, NULL);

void ms_otg_release_resource(struct platform_device *pdev)
{
	struct ms_otg *msotg = platform_get_drvdata(pdev);

	/* release resource */
	if (msotg->irq)
		devm_free_irq(&pdev->dev, msotg->irq, msotg);

	cancel_delayed_work_sync(&msotg->work);
	ms_otg_cancel_timer(msotg, A_WAIT_BCON_TIMER);
	ms_otg_cancel_timer(msotg, A_WAIT_VRISE_TIMER);

	if (msotg->qwork) {
		flush_workqueue(msotg->qwork);
		destroy_workqueue(msotg->qwork);
	}

	/* msotg->op_regs = ms_ipreg in probe function*/
	DEVM_KFREE(&pdev->dev, msotg->op_regs);
	/* msotg->phy.otg = otg in probe function*/
	DEVM_KFREE(&pdev->dev, msotg->phy.otg);

	DEVM_KFREE(&pdev->dev, msotg);
}

int ms_otg_remove(struct platform_device *pdev)
{
	struct ms_otg *msotg = platform_get_drvdata(pdev);

	device_remove_file(&msotg->pdev->dev, &dev_attr_id);

#if defined(CONFIG_MS_OTG_SOFTWARE_ID)
	device_remove_file(&msotg->pdev->dev, &dev_attr_software_id);
#endif

	sysfs_remove_group(&msotg->pdev->dev.kobj, &inputs_attr_group);

	usb_remove_phy(&msotg->phy);

	ms_otg_disable(msotg);

	ms_otg_release_resource(pdev);

	/* disable Power control GPIO */
#if defined(CONFIG_OF)
	if (msotg->pwrctl_pin)
		ms_otg_PowerControlGpio(msotg, 0);		/* drive GPIO off*/
	if (msotg->pwrctl_out)
		ms_otg_PowerControlGpioOut(msotg, 0);	/* set GPIO input mode */

#if defined(VBUS_SWITCH_GPIO)
	if (0 ^ (bool)(!!msotg->vbus_switch_active))	/* on =  0 (off) */
		MDrv_GPIO_Set_Low(msotg->vbus_switch_gpio);
	else
		MDrv_GPIO_Set_High(msotg->vbus_switch_gpio);
#endif
#endif

	return 0;
}

#if defined(CONFIG_OF)
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);
#endif
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
static struct dev_pm_ops ms_otg_pm_ops;
static struct str_waitfor_dev waitfor;
static int ms_otg_suspend_wrap(struct device *dev);
static int ms_otg_resume_wrap(struct device *dev);
#endif
#ifdef CONFIG_MS_OTG_SOFTWARE_ID
int msb_dev_en;
module_param(msb_dev_en, int, 0);
#endif

static int ms_otg_probe(struct platform_device *pdev)
{
	struct ms_usb_platform_data *pdata = pdev->dev.platform_data;
	struct ms_otg *msotg;
	struct usb_otg *otg;
	struct ms_otg_regs *ms_ipreg;
	/* struct resource *r; */
	int retval = 0, i, irq = -1;

	if (pdata == NULL) {
		pr_info("[OTG] warning.... no platform_data\n");
		//return -ENODEV;
	}

	msotg = devm_kzalloc(&pdev->dev, sizeof(*msotg), GFP_KERNEL);
	if (!msotg) {
		dev_err(&pdev->dev, "[OTG] failed to allocate memory!\n");
		return -ENOMEM;
	}

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg) {
		dev_err(&pdev->dev, "[OTG] 1 out of memory!\n");

		/* free allocated memory */
		DEVM_KFREE(&pdev->dev, msotg);

		return -ENOMEM;
	}

	ms_ipreg = devm_kzalloc(&pdev->dev, sizeof(*ms_ipreg), GFP_KERNEL);
	if (!ms_ipreg) {
		dev_err(&pdev->dev, "[OTG] 2 out of memory!\n");

		/* free allocated memory */
		DEVM_KFREE(&pdev->dev, otg);

		DEVM_KFREE(&pdev->dev, msotg);

		return -ENOMEM;
	}

	platform_set_drvdata(pdev, msotg);

	msotg->pdev = pdev;
	msotg->pdata = pdata;

	//msotg->clk = devm_clk_get(&pdev->dev, NULL);
	//if (IS_ERR(msotg->clk))
	//	return PTR_ERR(msotg->clk);

	msotg->qwork = create_singlethread_workqueue("ms_otg_queue");

	if (!msotg->qwork) {
		dev_dbg(&pdev->dev, "[OTG] cannot create workqueue for OTG\n");
		/* free allocted memory */

		DEVM_KFREE(&pdev->dev, ms_ipreg);

		DEVM_KFREE(&pdev->dev, otg);

		DEVM_KFREE(&pdev->dev, msotg);

		return -ENOMEM;
	}


#if defined(CONFIG_MS_FORCE_DEVICE)
	pr_info("[OTG] Force device mode\n");
#elif defined(CONFIG_MS_OTG_SOFTWARE_ID)
	pr_info("[OTG] software id mode\n");
	/* default is Host mode (customize for ADB) */
	msotg->otg_ctrl.id = 0;

	/* if boot argument exists !!!*/
	if (OTG_boot_Argument_on) {

		if (OTG_id_host == false) {
			msotg->otg_ctrl.id = 1;		/* device */
		}

		pr_info("[OTG] boot arguments on, id=%x\n", msotg->otg_ctrl.id);
	} else if ((!OTG_boot_Argument_on) && (OTG_backup_bootArgu_on)) {
		if (OTG_backup_id_host == false)
			msotg->otg_ctrl.id = 1;		/* device */

		pr_info("[OTG] boot_backup arguments on, id=%x\n", msotg->otg_ctrl.id);
	}

	if (msb_dev_en == 1)
		msotg->otg_ctrl.id = 1;    //device mode

	pr_info("[OTG] msb_dev_en, %x\n",msb_dev_en);
#endif

	INIT_DELAYED_WORK(&msotg->work, ms_otg_work);

	/* OTG common part */
	msotg->op_regs = ms_ipreg;
	//msotg->pdev = pdev;
	msotg->phy.dev = &pdev->dev;
	msotg->phy.otg = otg;
	msotg->phy.label = driver_name;
	msotg->phy.otg->state = OTG_STATE_UNDEFINED;

	otg->phy = (struct phy *)&msotg->phy;
	otg->set_host = ms_otg_set_host;
	otg->set_peripheral = ms_otg_set_peripheral;
	otg->set_vbus = ms_otg_set_vbus;

	for (i = 0; i < OTG_TIMER_NUM; i++)  {
		timer_setup(&msotg->otg_ctrl.timer[i], (void (*))ms_otg_timer_func[i], 0);
	}

#if defined(CONFIG_OF)

	msotg->op_regs->usbc_regs = (void __iomem *)_MSTAR_USBC0_BASE;
	msotg->op_regs->uhc_regs = (void __iomem *)_MSTAR_UHC0_BASE;
	msotg->op_regs->motg_regs = (void __iomem *)OTG0_BASE_ADDR;
	msotg->op_regs->utmi_regs = (void __iomem *)_MSTAR_UTMI0_BASE;
	pr_info("[OTG] USBC %x,UHC %x, OTG %x, UTMI %x\n", (unsigned)(msotg->op_regs->usbc_regs),
		(unsigned)(msotg->op_regs->uhc_regs), (unsigned)(msotg->op_regs->motg_regs),
		(unsigned int)(msotg->op_regs->utmi_regs));

	/* check DTS of OTG power control GPIO */
	if (ms_otg_initPwrCtrlGpio(msotg)) {
		if (msotg->pwrctl_out)
			ms_otg_PowerControlGpioOut(msotg, 1);	/* set GPIO output mode */
		if (msotg->pwrctl_pin)
			ms_otg_PowerControlGpio(msotg, 0);  /* drive GPIO */
	}

#if defined(VBUS_SWITCH_GPIO)
	ms_otg_initVbusSwitchGpio(msotg);
#endif

#else
	/* start of retrieve MStar IP register resource */
	r = platform_get_resource_byname(msotg->pdev,
					 IORESOURCE_MEM, "usbc-base");
	if (r == NULL) {
		dev_err(&pdev->dev, "[OTG] no USBC I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	pr_info("[OTG] USBC base = %x\n", r->start);
	msotg->op_regs->usbc_regs = (void *)(u32)(r->start);
	if (msotg->op_regs->usbc_regs == NULL) {
		dev_err(&pdev->dev, "[OTG] failed to map USBC I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}

	r = platform_get_resource_byname(msotg->pdev,
					 IORESOURCE_MEM, "uhc-base");
	if (r == NULL) {
		dev_err(&pdev->dev, "[OTG] no UHC I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	pr_info("[OTG] UHC base = %x\n", r->start);
	msotg->op_regs->uhc_regs = (void *)(u32)(r->start);
	if (msotg->op_regs->uhc_regs == NULL) {
		dev_err(&pdev->dev, "[OTG] failed to map UHC I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}

	r = platform_get_resource_byname(msotg->pdev,
					 IORESOURCE_MEM, "motg-base");
	if (r == NULL) {
		dev_err(&pdev->dev, "[OTG] no OTG I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	pr_info("[OTG] OTG base = %x\n", r->start);
	msotg->op_regs->motg_regs = (void *)(u32)(r->start);
	if (msotg->op_regs->motg_regs == NULL) {
		dev_err(&pdev->dev, "[OTG] failed to map OTG I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}

	r = platform_get_resource_byname(msotg->pdev,
					 IORESOURCE_MEM, "utmi-base");
	if (r == NULL) {
		dev_err(&pdev->dev, "[OTG] no UTMI I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	pr_info("[OTG] UTMI base = %x\n", r->start);
	msotg->op_regs->utmi_regs = (void *)(u32)(r->start);
	if (msotg->op_regs->utmi_regs == NULL) {
		dev_err(&pdev->dev, "[OTG] failed to map UTMI I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}
#endif
	ms_dump_usbc_reg(msotg);
	ms_dump_peripheral_reg(msotg);

	/* initial backup OTG interrupt enables */
	msotg->motg_IntrTx = readl(msotg->op_regs->motg_regs+M_REG_INTRTXE);
	msotg->motg_IntrRx = readl(msotg->op_regs->motg_regs+M_REG_INTRRXE);
	msotg->motg_IntrUSB = readl(msotg->op_regs->motg_regs+M_REG_INTRUSB);

	/* initial backup OTG connectable */
	msotg->motg_Power = readl(msotg->op_regs->motg_regs);

	ms_dump_host_reg(msotg);
	/* initial backup UHC interrupt enable/command regs */
	msotg->uhc_intr_status = readl(((msotg->op_regs->uhc_regs)+EHC_INTR));
	msotg->uhc_usbcmd_status = readl((msotg->op_regs->uhc_regs+EHC_CMD));
	msotg->uhc_usbcmd_int_thrc_status = readl((msotg->op_regs->uhc_regs+EHC_CMD_INT_THRC));
	/* end of retrieve MStar IP register resource */

	/* we will acces controller register, so enable the udc controller */
	retval = ms_otg_enable_internal(msotg);

	if (retval) {
		dev_err(&pdev->dev, "[OTG] ms otg enable error %d\n", retval);
		goto err_destroy_workqueue;
	}

	if (pdata == NULL) {
		//do nothing
	} else
	if (pdata->id) {
		retval = devm_request_threaded_irq(&pdev->dev, pdata->id->irq,
						NULL, ms_otg_inputs_irq,
						IRQF_ONESHOT, "id", msotg);
		if (retval) {
			dev_info(&pdev->dev,
				 "[OTG] Failed to request irq for ID\n");
			pdata->id = NULL;
		}
	}

	if (pdata == NULL) {
		//do nothing
	} else
	if (pdata->vbus) {
		msotg->clock_gating = 1;
		retval = devm_request_threaded_irq(&pdev->dev, pdata->vbus->irq,
						NULL, ms_otg_inputs_irq,
						IRQF_ONESHOT, "vbus", msotg);
		if (retval) {
			dev_info(&pdev->dev,
				 "[OTG] Failed to request irq for VBUS, "
				 "disable clock gating\n");
			msotg->clock_gating = 0;
			pdata->vbus = NULL;
		}
	}

	if (pdata == NULL) {
		msotg->clock_gating = 0;
	} else {
		if (pdata->disable_otg_clock_gating)
			msotg->clock_gating = 0;
	}
	ms_otg_reset(msotg);
	ms_otg_init_irq(msotg);
#if defined(CONFIG_OF)
#ifdef ENABLE_IRQ_REMAP
	irq = USB_IRQ_OTG;
	msotg->irq = irq;
#else
	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	msotg->irq = irq;
#endif
	pr_info("[OTG] USB Irq Num = %d\n", msotg->irq);
	if (devm_request_irq(&pdev->dev, msotg->irq, ms_otg_irq, IRQF_SHARED,
			driver_name, msotg)) {
		dev_err(&pdev->dev, "[OTG] Request irq %d for OTG failed\n",
			msotg->irq);
		msotg->irq = 0;
		retval = -ENODEV;
		goto err_disable_clk;
	}
#ifdef ENABLE_IRQ_REMAP
	irq = OTG_IRQ_UHC;
	msotg->irq_uhc = irq;
#else
	irq = irq_of_parse_and_map(pdev->dev.of_node, 1);
	msotg->irq_uhc = irq;
#endif
	pr_info("[OTG] UHC Irq Num = %d\n", msotg->irq_uhc);
#else
	r = platform_get_resource_byname(msotg->pdev,
					IORESOURCE_IRQ, "usb-int");

	if (r == NULL) {
		dev_err(&pdev->dev, "[OTG] no USB IRQ resource defined\n");
		retval = -ENODEV;
		goto err_disable_clk;
	}

	pr_info("[OTG] USB Irq Num = %x\n", r->start);
	msotg->irq = r->start;
	if (devm_request_irq(&pdev->dev, msotg->irq, ms_otg_irq, IRQF_SHARED,
			driver_name, msotg)) {
		dev_err(&pdev->dev, "[OTG] Request irq %d for OTG failed\n",
			msotg->irq);
		msotg->irq = 0;
		retval = -ENODEV;
		goto err_disable_clk;
	}

	r = platform_get_resource_byname(msotg->pdev,
					IORESOURCE_IRQ, "uhc-int");

	if (r == NULL) {
		dev_err(&pdev->dev, "[OTG] no UHC IRQ resource defined\n");
		retval = -ENODEV;
		goto err_disable_clk;
	}

	pr_info("[OTG] UHC Irq Num = %x\n", r->start);
	msotg->irq_uhc = r->start;
#endif
	retval = usb_add_phy(&msotg->phy, USB_PHY_TYPE_USB2);
	if (retval < 0) {
		dev_err(&pdev->dev, "[OTG] can't register transceiver, %d\n",
			retval);
		goto err_disable_clk;
	}

	retval = sysfs_create_group(&pdev->dev.kobj, &inputs_attr_group);
	if (retval < 0) {
		dev_dbg(&pdev->dev,
			"[OTG] Can't register sysfs attr group: %d\n", retval);
		goto err_remove_phy;
	}

#if defined(CONFIG_MS_OTG_SOFTWARE_ID)
	retval = device_create_file(&pdev->dev, &dev_attr_software_id);
	if (retval < 0) {
		dev_dbg(&pdev->dev,
			"[OTG] Can't register sysfs attr file - software_id: %d\n", retval);
		goto err_remove_phy;
	}
#endif

	retval = device_create_file(&pdev->dev, &dev_attr_id);
	if (retval < 0) {
		dev_dbg(&pdev->dev,
			"[OTG] Can't register sysfs attr file - id: %d\n", retval);
		goto err_remove_phy;
	}

	spin_lock_init(&msotg->wq_lock);
	if (spin_trylock(&msotg->wq_lock)) {
		ms_otg_run_state_machine(msotg, HZ/10);
		spin_unlock(&msotg->wq_lock);
	}

#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
	of_mstar_str(driver_name, &pdev->dev, &ms_otg_pm_ops, &waitfor,
			&ms_otg_suspend_wrap, &ms_otg_resume_wrap,
			NULL, NULL);
#endif

	pr_info("[OTG] successful probe OTG device %s clock gating.\n",
		 msotg->clock_gating ? "with" : "without");

	return 0;

err_remove_phy:
	usb_remove_phy(&msotg->phy);
err_disable_clk:
	ms_otg_disable_internal(msotg);
err_destroy_workqueue:

	/* release resource */
	ms_otg_release_resource(pdev);

	return retval;
}

#ifdef CONFIG_PM
static int ms_otg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ms_otg *msotg = platform_get_drvdata(pdev);
	struct usb_phy *phy = &msotg->phy;

	/* disable all USBC interrupt */
	writel(0, &msotg->op_regs->usbc_regs->intr_en);

	msotg->motg_Power = readl(msotg->op_regs->motg_regs);
	pr_info("[OTG] OTG power = 0x%x\n", msotg->motg_Power);

	if (!msotg->clock_gating)
		ms_otg_disable_internal(msotg);

	/* force to Unkonw State*/
	phy->otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int ms_otg_resume(struct platform_device *pdev)
{
	struct ms_otg *msotg = platform_get_drvdata(pdev);
	struct usb_phy *phy = &msotg->phy;

	pr_info("[OTG] %s, from state %s\n", __func__, state_string[phy->otg->state]);

	if (!msotg->clock_gating) {
		ms_otg_enable_internal(msotg);

		ms_otg_reset(msotg);
		ms_otg_init_irq(msotg);

		if (spin_trylock(&msotg->wq_lock)) {
			ms_otg_run_state_machine(msotg, HZ/2); // 500ms delay
			spin_unlock(&msotg->wq_lock);
		}
	}
	return 0;
}

#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
static int ms_otg_suspend_wrap(struct device *dev)
{
	struct platform_device *pdev;

	if (WARN_ON(!dev))
		return -ENODEV;

	pdev = to_platform_device(dev);

	if (waitfor.stage1_s_wait)
		wait_for_completion(&(waitfor.stage1_s_wait->power.completion));

	return ms_otg_suspend(pdev, dev->power.power_state);
}

static int ms_otg_resume_wrap(struct device *dev)
{
	struct platform_device *pdev;

	if (WARN_ON(!dev))
		return -ENODEV;

	pdev = to_platform_device(dev);

	if (waitfor.stage1_r_wait)
		wait_for_completion(&(waitfor.stage1_r_wait->power.completion));

	return ms_otg_resume(pdev);
}
#endif	/* CONFIG_MP_MSTAR_STR_OF_ORDER */
#endif

#define	PLATFORM_DRIVER	ms_otg_driver

#if defined(CONFIG_OF)

static struct of_device_id mstar_otg_of_device_ids[] = {
	{.compatible = "Mstar-otg"},
	{},
};
#endif

static struct platform_driver PLATFORM_DRIVER = {
	.probe 		= ms_otg_probe,
	.remove 	= ms_otg_remove,
#if defined(CONFIG_PM) && !defined(CONFIG_MP_MSTAR_STR_OF_ORDER)
	.suspend	= ms_otg_suspend,
	.resume		= ms_otg_resume,
#endif
	.driver = {
		.owner = THIS_MODULE,
		.name = driver_name,
#if defined(CONFIG_OF)
		.of_match_table = mstar_otg_of_device_ids,
#endif
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
		.pm = &ms_otg_pm_ops,
#endif
	}
};

#if defined(CONFIG_OF)
static int __init phy_otg_init(void)
{
	int retval = 0;

	retval = platform_driver_register(&PLATFORM_DRIVER);
	if (retval < 0)
		pr_err("OTG fail\n");
	return 0;
}
#endif

static int __init ms_otg_init(void)
{
	int retval = 0;
	pr_info("[OTG] Init %s\n", DRIVER_VERSION);
#if defined(CONFIG_OF)
	retval = phy_otg_init();
#else
	retval = platform_driver_register(&PLATFORM_DRIVER);
	if (retval < 0)
		pr_err("[OTG] r: %s register fail!!!\n", __func__);
#endif
	return retval;

}

static void __exit ms_otg_exit(void)
{
	platform_driver_unregister(&PLATFORM_DRIVER);
	return;
}

module_exit(ms_otg_exit);
/* init after host and gadget driver */
late_initcall(ms_otg_init);
