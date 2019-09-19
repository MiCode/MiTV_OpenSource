///////////////////////////////////////////////////////////////////////////////////////////////////
//
// * Copyright (c) 2006 - 2017 MStar Semiconductor, Inc.
// This program is free software.
// You can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program;
// if not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

/*------------------------------------------------------------------------------
    PROJECT: MSB250x Linux BSP - PROC filesystem
    DESCRIPTION:
          Use /proc to monitor MSB250x dual role USB device controllers


    HISTORY:
         6/11/2010     Calvin Hung    First Revision

-------------------------------------------------------------------------------*/

/******************************************************************************
 * Include Files
 ******************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/semaphore.h>

#include <asm/byteorder.h>
#include <asm/io.h>

#include "msb250x_udc.h"
#include "msb250x_udc_reg.h"

#define MSB250X_UDC_DUMP_REG 0

/******************************************************************************
 * Constants
 ******************************************************************************/
/* the module parameter */
#define DRIVER_DESC "MSB250x USB Device Controller /proc system"
#define DRIVER_VERSION "15 May 2010"
#define DRIVER_AUTHOR "mstarsemi.com"

#define PROC_DIR_NAME       "msb250x_udc"
#define PROC_CONN_STATUS    "Connect"
#if MSB250X_UDC_DUMP_REG
#define PROC_COMM_REG       "comm_usb_reg"
#define PROC_ENDP0_REG      "endp0_ctrl_stat_reg"
#define PROC_ENDP1_REG      "endp1_ctrl_stat_reg"
#define PROC_ENDP2_REG      "endp2_ctrl_stat_reg"
#define PROC_ENDP3_REG      "endp3_ctrl_stat_reg"
#define PROC_FIFO_REG       "fifo_reg"
#define PROC_ADDI_REG       "addi_ctrl_reg"
#define PROC_NOINDEX_REG    "non_index_endp_reg"
#define PROC_DMA_REG        "dma_ctrl_reg"

static const char comm_reg_name[][10] = {"FAddr", "Power", "IntrTx",
                                   "IntrRx", "IntrTxE", "IntrRxE",
                                   "IntrUSB", "IntrUSBE", "Frame",
                                   "Index", "Testmode"};
static const char endp0_reg_name[][10] = {"CSR0", "Count0", "ConfData"};
static const char endp_reg_name[][10] = {"TxMaxP", "TxCSR", "RxMaxP",
                                         "RxCSR", "RxCount", "FIFOSize"};
#endif /* MSB250X_UDC_DUMP_REG */

/******************************************************************************
 * Variables
 ******************************************************************************/
static struct proc_dir_entry *proc_msb250x;
#if MSB250X_UDC_DUMP_REG
static DECLARE_MUTEX(msb250x_proc_sem);
#endif /* MSB250X_UDC_DUMP_REG */
static spinlock_t msb250x_proc_lock;
/******************************************************************************
 * Function definition
 ******************************************************************************/
static int get_conntion_status(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;
    u8 linestate;
    u8 soft_conn;
    unsigned long flags;

    if (off != 0)
    {
        return 0;
    }

    spin_lock_irqsave(&msb250x_proc_lock, flags);

    linestate = (udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;
    soft_conn = udc_read8(MSB250X_UDC_PWR_REG)&(MSB250X_UDC_PWR_SOFT_CONN);

    if ((linestate == 0) && (soft_conn != 0))
    {
        len += sprintf(buf+len, "1"); /* Connect */
    }
    else
    {
        len += sprintf(buf+len, "0"); /* Disconnect */
    }


    spin_unlock_irqrestore(&msb250x_proc_lock, flags);

    *eof = 1;

    return len;
}

#if MSB250X_UDC_DUMP_REG
static int comm_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;

    if (off != 0)
    {
        return 0;
    }

    if (down_interruptible(&msb250x_proc_sem))
    {
        return -ERESTARTSYS;
    }

    len += sprintf(buf+len, "Common USB Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_FADDR_REG, comm_reg_name[0], udc_read8(MSB250X_UDC_FADDR_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_PWR_REG, comm_reg_name[1], udc_read8(MSB250X_UDC_PWR_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_INTRTX_REG, comm_reg_name[2], udc_read16(MSB250X_UDC_INTRTX_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_INTRRX_REG, comm_reg_name[3], udc_read16(MSB250X_UDC_INTRRX_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_INTRTXE_REG, comm_reg_name[4], udc_read16(MSB250X_UDC_INTRTXE_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_INTRRXE_REG, comm_reg_name[5], udc_read16(MSB250X_UDC_INTRRXE_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_INTRUSB_REG, comm_reg_name[6], udc_read8(MSB250X_UDC_INTRUSB_REG));
    len += sprintf(buf+len, "%08xh\t%s\t%04xh\n", MSB250X_UDC_INTRUSBE_REG, comm_reg_name[7], udc_read8(MSB250X_UDC_INTRUSBE_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_FRAME_L_REG, comm_reg_name[8], udc_read16(MSB250X_UDC_FRAME_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_INDEX_REG, comm_reg_name[9], udc_read8(MSB250X_UDC_INDEX_REG));
    len += sprintf(buf+len, "%08xh\t%s\t%04xh\n", MSB250X_UDC_TESTMODE_REG, comm_reg_name[10], udc_read8(MSB250X_UDC_TESTMODE_REG));

    up(&msb250x_proc_sem);

    *eof = 1;

    return len;
}

static int endp0_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;
    u16 index;
    unsigned long flags;

    if (off != 0)
    {
        return 0;
    }

    spin_lock_irqsave(&msb250x_proc_lock, flags);

    index = udc_read8(MSB250X_UDC_INDEX_REG) & 0x000f;
    udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);

    len += sprintf(buf+len, "Indexed Endpoint0 Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_CSR0_REG, endp0_reg_name[0], udc_read16(MSB250X_UDC_CSR0_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_COUNT0_REG, endp0_reg_name[1], udc_read8(MSB250X_UDC_COUNT0_REG));
    len += sprintf(buf+len, "%08xh\t%s\t%04xh\n", MSB250X_UDC_CONFDATA_REG, endp0_reg_name[2], udc_read8(MSB250X_UDC_CONFDATA_REG));

    udc_write8(index, MSB250X_UDC_INDEX_REG);

    spin_unlock_irqrestore(&msb250x_proc_lock, flags);

    *eof = 1;

    return len;
}

static int endp1_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;
    u16 index;
    unsigned long flags;

    if (off != 0)
    {
        return 0;
    }

    spin_lock_irqsave(&msb250x_proc_lock, flags);

    index = udc_read8(MSB250X_UDC_INDEX_REG) & 0x000f;
    udc_write8(0x01, MSB250X_UDC_INDEX_REG);

    len += sprintf(buf+len, "Indexed Endpoint1 Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_TXMAP_L_REG, endp_reg_name[0], udc_read16(MSB250X_UDC_TXMAP_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_TXCSR1_REG, endp_reg_name[1], udc_read16(MSB250X_UDC_TXCSR1_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXMAP_L_REG, endp_reg_name[2], udc_read16(MSB250X_UDC_RXMAP_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXCSR1_REG, endp_reg_name[3], udc_read16(MSB250X_UDC_RXCSR1_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXCOUNT_L_REG, endp_reg_name[4], udc_read16(MSB250X_UDC_RXCOUNT_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t%04xh\n", MSB250X_UDC_FIFOSIZE_REG, endp_reg_name[5], udc_read16(MSB250X_UDC_FIFOSIZE_REG));

    udc_write8(index, MSB250X_UDC_INDEX_REG);

    spin_unlock_irqrestore(&msb250x_proc_lock, flags);

    *eof = 1;

    return len;
}

static int endp2_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;
    u16 index;
    unsigned long flags;

    if (off != 0)
    {
        return 0;
    }

    spin_lock_irqsave(&msb250x_proc_lock, flags);

    index = udc_read8(MSB250X_UDC_INDEX_REG) & 0x000f;
    udc_write8(0x02, MSB250X_UDC_INDEX_REG);

    len += sprintf(buf+len, "Indexed Endpoint2 Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_TXMAP_L_REG, endp_reg_name[0], udc_read16(MSB250X_UDC_TXMAP_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_TXCSR1_REG, endp_reg_name[1], udc_read16(MSB250X_UDC_TXCSR1_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXMAP_L_REG, endp_reg_name[2], udc_read16(MSB250X_UDC_RXMAP_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXCSR1_REG, endp_reg_name[3], udc_read16(MSB250X_UDC_RXCSR1_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXCOUNT_L_REG, endp_reg_name[4], udc_read16(MSB250X_UDC_RXCOUNT_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t%04xh\n", MSB250X_UDC_FIFOSIZE_REG, endp_reg_name[5], udc_read16(MSB250X_UDC_FIFOSIZE_REG));

    udc_write8(index, MSB250X_UDC_INDEX_REG);

    spin_unlock_irqrestore(&msb250x_proc_lock, flags);

    *eof = 1;

    return len;
}

static int endp3_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;
    u16 index;
    unsigned long flags;

    if (off != 0)
    {
        return 0;
    }

    spin_lock_irqsave(&msb250x_proc_lock, flags);

    index = udc_read8(MSB250X_UDC_INDEX_REG) & 0x000f;
    udc_write8(0x03, MSB250X_UDC_INDEX_REG);

    len += sprintf(buf+len, "Indexed Endpoint3 Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_TXMAP_L_REG, endp_reg_name[0], udc_read16(MSB250X_UDC_TXMAP_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_TXCSR1_REG, endp_reg_name[1], udc_read16(MSB250X_UDC_TXCSR1_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXMAP_L_REG, endp_reg_name[2], udc_read16(MSB250X_UDC_RXMAP_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXCSR1_REG, endp_reg_name[3], udc_read16(MSB250X_UDC_RXCSR1_REG));
    len += sprintf(buf+len, "%08xh\t%s\t\t%04xh\n", MSB250X_UDC_RXCOUNT_L_REG, endp_reg_name[4], udc_read16(MSB250X_UDC_RXCOUNT_L_REG));
    len += sprintf(buf+len, "%08xh\t%s\t%04xh\n", MSB250X_UDC_FIFOSIZE_REG, endp_reg_name[5], udc_read16(MSB250X_UDC_FIFOSIZE_REG));

    udc_write8(index, MSB250X_UDC_INDEX_REG);

    spin_unlock_irqrestore(&msb250x_proc_lock, flags);

    *eof = 1;

    return len;
}

static int fifo_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;

    if (off != 0)
    {
        return 0;
    }

    if (down_interruptible(&msb250x_proc_sem))
    {
        return -ERESTARTSYS;
    }

    len += sprintf(buf+len, "FIFOs Registers\n");

    up(&msb250x_proc_sem);

    *eof = 1;

    return len;
}

static int addi_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;

    if (off != 0)
    {
        return 0;
    }

    if (down_interruptible(&msb250x_proc_sem))
    {
        return -ERESTARTSYS;
    }

    len += sprintf(buf+len, "Additional Control Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\tDEVCTL\t\t%04xh\n", MSB250X_UDC_DEVCTL_REG, udc_read8(MSB250X_UDC_DEVCTL_REG));

    up(&msb250x_proc_sem);

    *eof = 1;

    return len;
}

static int noindex_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;

    if (off != 0)
    {
        return 0;
    }

    if (down_interruptible(&msb250x_proc_sem))
    {
        return -ERESTARTSYS;
    }

    len += sprintf(buf+len, "Non-Indexed Endpoint Registers\n");

    up(&msb250x_proc_sem);

    *eof = 1;

    return len;
}

static int dma_reg_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *buf = page;
    int len = 0;

    if (off != 0)
    {
        return 0;
    }

    if (down_interruptible(&msb250x_proc_sem))
    {
        return -ERESTARTSYS;
    }

    len += sprintf(buf+len, "DMA Control Registers\n");
    len += sprintf(buf+len, "Addr\t\tName\t\tValue\n");
    len += sprintf(buf+len, "%08xh\tINTR\t\t%04xh\n", MSB250X_UDC_DMA_INTR_REG, udc_read8(MSB250X_UDC_DMA_INTR_REG));
    len += sprintf(buf+len, "%08xh\tCTRL1\t\t%04xh\n", MSB250X_UDC_DMA_CTRL1_REG, udc_read16(MSB250X_UDC_DMA_CTRL1_REG));
    len += sprintf(buf+len, "%08xh\tADDR1_L\t\t%04xh\n", MSB250X_UDC_DMA_ADDR1_L_REG, udc_read16(MSB250X_UDC_DMA_ADDR1_L_REG));
    len += sprintf(buf+len, "%08xh\tADDR1_H\t\t%04xh\n", MSB250X_UDC_DMA_ADDR1_H_REG, udc_read16(MSB250X_UDC_DMA_ADDR1_H_REG));
    len += sprintf(buf+len, "%08xh\tCOUNT1_L\t%04xh\n", MSB250X_UDC_DMA_COUNT1_L_REG, udc_read8(MSB250X_UDC_DMA_COUNT1_L_REG));
    len += sprintf(buf+len, "%08xh\tCOUNT1_H\t%04xh\n", MSB250X_UDC_DMA_COUNT1_H_REG, udc_read8(MSB250X_UDC_DMA_COUNT1_H_REG));

    up(&msb250x_proc_sem);

    *eof = 1;

    return len;
}
#endif /* MSB250X_UDC_DUMP_REG */
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_proc_init
+------------------------------------------------------------------------------
| DESCRIPTION : The generic driver interface function for register this driver
|               to Linux Kernel.
|
| RETURN      : 0 when success, error code in other case.
|
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static int __init msb250x_udc_proc_init(void)
{
    struct proc_dir_entry *pde;

    proc_msb250x = proc_mkdir(PROC_DIR_NAME, NULL);

    if (!proc_msb250x)
    {
        goto err0;
    }

    pde = create_proc_read_entry(PROC_CONN_STATUS, 0, proc_msb250x, get_conntion_status, NULL);
    if (!pde)
    {
        goto err1;
    }

#if MSB250X_UDC_DUMP_REG
    pde = create_proc_read_entry(PROC_COMM_REG, 0, proc_msb250x, comm_reg_read, NULL);

    if (!pde)
    {
        goto err2;
    }

    pde = create_proc_read_entry(PROC_ENDP0_REG, 0, proc_msb250x, endp0_reg_read, NULL);

    if (!pde)
    {
        goto err3;
    }

    pde = create_proc_read_entry(PROC_ENDP1_REG, 0, proc_msb250x, endp1_reg_read, NULL);

    if (!pde)
    {
        goto err4;
    }

    pde = create_proc_read_entry(PROC_ENDP2_REG, 0, proc_msb250x, endp2_reg_read, NULL);

    if (!pde)
    {
        goto err5;
    }

    pde = create_proc_read_entry(PROC_ENDP3_REG, 0, proc_msb250x, endp3_reg_read, NULL);

    if (!pde)
    {
        goto err6;
    }

    pde = create_proc_read_entry(PROC_FIFO_REG, 0, proc_msb250x, fifo_reg_read, NULL);

    if (!pde)
    {
        goto err7;
    }

    pde = create_proc_read_entry(PROC_ADDI_REG, 0, proc_msb250x, addi_reg_read, NULL);

    if (!pde)
    {
        goto err8;
    }

    pde = create_proc_read_entry(PROC_NOINDEX_REG, 0, proc_msb250x, noindex_reg_read, NULL);

    if (!pde)
    {
        goto err9;
    }

    pde = create_proc_read_entry(PROC_DMA_REG, 0, proc_msb250x, dma_reg_read, NULL);

    if (!pde)
    {
        goto err10;
    }
#endif /* MSB250X_UDC_DUMP_REG */

    return 0;

#if MSB250X_UDC_DUMP_REG
err10:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_NOINDEX_REG, NULL);
err9:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ADDI_REG, NULL);
err8:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_FIFO_REG, NULL);
err7:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP3_REG, NULL);
err6:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP2_REG, NULL);
err5:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP1_REG, NULL);
err4:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP0_REG, NULL);
err3:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_COMM_REG, NULL);
err2:
    remove_proc_entry(PROC_DIR_NAME"/"PROC_CONN_STATUS, NULL);
#endif /* MSB250X_UDC_DUMP_REG */

err1:
    remove_proc_entry(PROC_DIR_NAME, NULL);
err0:

    return -ENOMEM;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_proc_exit
+------------------------------------------------------------------------------
| DESCRIPTION : The generic driver interface function for unregister this driver
|               from Linux Kernel.
|
| RETURN      : none.
|
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static void __exit msb250x_udc_proc_exit(void)
{
#if MSB250X_UDC_DUMP_REG
    remove_proc_entry(PROC_DIR_NAME"/"PROC_DMA_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_NOINDEX_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ADDI_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_FIFO_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP3_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP2_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP1_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_ENDP0_REG, NULL);
    remove_proc_entry(PROC_DIR_NAME"/"PROC_COMM_REG, NULL);
#endif /* MSB250X_UDC_DUMP_REG */

    remove_proc_entry(PROC_DIR_NAME"/"PROC_CONN_STATUS, NULL);
    remove_proc_entry(PROC_DIR_NAME, NULL);

    return;
}

fs_initcall(msb250x_udc_proc_init); //use subsys_initcall due to this should be earlier than ADB module_init
module_exit(msb250x_udc_proc_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

