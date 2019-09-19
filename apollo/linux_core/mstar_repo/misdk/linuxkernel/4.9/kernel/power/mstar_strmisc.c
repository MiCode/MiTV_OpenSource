#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
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
static unsigned short XC_save[3][128]={{0}};
void XC_Save_Bank(int bkidx, unsigned short bank[])
{
    int i;
    unsigned short u16Bank = 0;
    u16Bank = XC_REG( 0x00 );
    XC_REG( 0x00 ) = (unsigned short)(bkidx);
    for(i=1;i<128;i++){
        bank[i]=XC_REG(i);
    }
    XC_REG( 0x00 ) = u16Bank;
}
void XC_Restore_Bank(int bkidx, unsigned short bank[])
{
    int i;
    unsigned short u16Bank = 0;
    u16Bank = XC_REG( 0x00 );
    XC_REG( 0x00 ) = (unsigned short)(bkidx);
    for(i=1;i<128;i++){
        XC_REG(i)=bank[i];
    }
    XC_REG( 0x00 ) = u16Bank;
}
void XC_RegSave(void)
{
    XC_Save_Bank(0x00,XC_save[0]);
    XC_Save_Bank(0x0f,XC_save[1]);
    XC_Save_Bank(0x10,XC_save[2]);
}
void XC_RegRestore(void)
{
    XC_Restore_Bank(0x00,XC_save[0]);
    XC_Restore_Bank(0x0f,XC_save[1]);
    XC_Restore_Bank(0x10,XC_save[2]);
}

static int mstar_xc_str_suspend(struct platform_device *dev, pm_message_t state)
{
    XC_RegSave();
    return 0;
}
static int mstar_xc_str_resume(struct platform_device *dev)
{
    XC_RegRestore();
    return 0;
}

static int mstar_xc_str_probe(struct platform_device *pdev)
{
	return 0;
}

static int mstar_xc_str_remove(struct platform_device *pdev)
{
    return 0;
}
#if defined (CONFIG_ARM64)
static struct of_device_id mstarxc_of_device_ids[] = {
         {.compatible = "mstar-xc"},
         {},
};
#endif
static struct platform_driver Mstar_xc_str_driver = {
	.probe 		= mstar_xc_str_probe,
	.remove 	= mstar_xc_str_remove,
    .suspend    = mstar_xc_str_suspend,
    .resume     = mstar_xc_str_resume,

	.driver = {
#if defined(CONFIG_ARM64)
	    .of_match_table = mstarxc_of_device_ids,
#endif
		.name	= "Mstar-xc-str",
        .owner  = THIS_MODULE,
	}
};
struct platform_device Mstar_xc_str_device = {
    .name  = "Mstar-xc-str",
    .id    = 0,
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

