#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <asm/io.h>
#include <asm/string.h>
#include <linux/platform_device.h>
#ifdef MSTAR_STRMISC_CHIP
#include "mstar_strmisc_chip.h"
#else
#include "asm/mstar_strmisc.h"
#endif

#define XC_PATCH 1
#if XC_PATCH
static unsigned short XC_save[3][128] = {{0}};

static void XC_Save_Bank(int bkidx, unsigned short bank[])
{
	int i;
	unsigned short u16Bank = 0;

	u16Bank = XC_REG(0x00);
	XC_REG(0x00) = (unsigned short)(bkidx);
	for (i = 1; i < 128; i++)
		bank[i] = XC_REG(i);
	XC_REG(0x00) = u16Bank;
}

static void XC_Restore_Bank(int bkidx, unsigned short bank[])
{
	int i;
	unsigned short u16Bank = 0;

	u16Bank = XC_REG(0x00);
	XC_REG(0x00) = (unsigned short)(bkidx);
	for (i = 1; i < 128; i++)
		XC_REG(i) = bank[i];
	XC_REG(0x00) = u16Bank;
}

static int XC_RegSave(void)
{
	XC_Save_Bank(0x00, XC_save[0]);
	XC_Save_Bank(0x0f, XC_save[1]);
	XC_Save_Bank(0x10, XC_save[2]);

	return 0;
}

static int XC_RegRestore(void)
{
	XC_Restore_Bank(0x00, XC_save[0]);
	XC_Restore_Bank(0x0f, XC_save[1]);
	XC_Restore_Bank(0x10, XC_save[2]);

	return 0;
}
#if CONFIG_MP_MSTAR_STR_OF_ORDER
static struct dev_pm_ops xc_str_pm_ops;
static struct str_waitfor_dev waitfor;

static int of_xc_str_suspend_wrapper(struct device *dev)
{
	if (waitfor.stage1_s_wait)
        	wait_for_completion(&(waitfor.stage1_s_wait->power.completion));

	pr_info("Do %s\n", __func__);
	return XC_RegSave();
}

static int of_xc_str_resume_wrapper(struct device *dev)
{
	if (waitfor.stage1_r_wait)
        	wait_for_completion(&(waitfor.stage1_r_wait->power.completion));

	pr_info("Do %s\n", __func__);
	return XC_RegRestore();
}
#else
static int mstar_xc_str_suspend(struct platform_device *dev, pm_message_t state)
{
	pr_info("Do %s\n", __func__);
	return XC_RegSave();
}

static int mstar_xc_str_resume(struct platform_device *dev)
{
	pr_info("Do %s\n", __func__);
	return XC_RegRestore();
}
#endif

static int mstar_xc_str_probe(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
	of_mstar_str("mstar-xc", &pdev->dev,
		&xc_str_pm_ops, &waitfor,
		&of_xc_str_suspend_wrapper,
		&of_xc_str_resume_wrapper,
		NULL, NULL);
#endif
	return 0;
}

static int mstar_xc_str_remove(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);
	return 0;
}
#ifdef CONFIG_OF
static struct of_device_id mstarxc_of_device_ids[] = {
	{.compatible = "mstar-xc"},
	{},
};
#endif
static struct platform_driver Mstar_xc_str_driver = {
	.probe 		= mstar_xc_str_probe,
	.remove		= mstar_xc_str_remove,
#ifndef CONFIG_MP_MSTAR_STR_OF_ORDER
	.suspend	= mstar_xc_str_suspend,
	.resume		= mstar_xc_str_resume,
#endif

	.driver = {
#ifdef CONFIG_OF
		.of_match_table = mstarxc_of_device_ids,
#endif
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
		.pm     = &xc_str_pm_ops,
#endif
		.name	= "Mstar-xc-str",
		.owner  = THIS_MODULE,
	}
};
struct platform_device Mstar_xc_str_device = {
	.name	= "Mstar-xc-str",
	.id	= 0,
};
static int mstar_xc_str_modinit(void)
{
	platform_driver_register(&Mstar_xc_str_driver);
	return 0;
}
static void mstar_xc_str_modexit(void)
{
	platform_driver_unregister(&Mstar_xc_str_driver);
}

module_init(mstar_xc_str_modinit);
module_exit(mstar_xc_str_modexit);
#endif

