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
    PROJECT: MSB250x Linux BSP
    DESCRIPTION:
          MSB250x dual role USB device controllers


    HISTORY:
         6/11/2010     Calvin Hung    First Revision

-------------------------------------------------------------------------------*/


/******************************************************************************
 * Include Files
 ******************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
//#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/clk.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h> // Mstar OTG operation
#include <linux/cdev.h>
#include <asm/uaccess.h>        /* copy_*_user */

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
//#include <asm/system.h>
#include <asm/unaligned.h>
#include <mach/irqs.h>

#include <mach/hardware.h>
#if defined(CONFIG_ARM64)
#else
#include <asm/mach-types.h>
#endif
#include "msb250x_udc.h"
#include "msb250x_udc_reg.h"
#include "msb250x_udc_ioctl.h"
#include "ms_usbmain.h"
#include "ms_otg.h"
#include "ms_gvar.h"
#include "ms_udc.h"
#if defined(CONFIG_USB_MSB250X_DMA) || defined(CONFIG_USB_MSB250X_DMA_MODULE)
#include "ms_dma.h"
#include <linux/mm.h>
#endif


/******************************************************************************
 * Constants
 ******************************************************************************/
//#define MSB250X_UDC_DEBUG 0
#if 1//def MSB250X_UDC_DEBUG
//static const char *ep0states[]=
//{
//    "EP0_IDLE",
//    "EP0_IN_DATA_PHASE",
//    "EP0_OUT_DATA_PHASE",
//    "EP0_END_XFER",
//    "EP0_STALL",
//};
#define DBG(x...)
#else
#define DBG(x...)	printk(KERN_INFO x)
#endif

#define UDC_DEBUG_TX_RX 0
#if UDC_DEBUG_TX_RX
#define DBG_TR(x...) printk(KERN_INFO x)
#else
#define DBG_TR(x...)
#endif
/* the module parameter */
#define DRIVER_DESC "MSB250x USB Device Controller Gadget"
#define DRIVER_VERSION "5 June 2010"
#define DRIVER_AUTHOR "mstarsemi.com"


//int tmp_buff;
/******************************************************************************
 * Variables
 ******************************************************************************/
static const char sg_gadget_name[] = "msb250x_udc";
struct msb250x_udc *sg_udc_controller=NULL;

#if defined(CONFIG_USB_MSB250X_DMA) || defined(CONFIG_USB_MSB250X_DMA_MODULE)
static int using_dma = 0;
#endif

static int msb250x_udc_major =   MSB250X_UDC_MAJOR;
static int msb250x_udc_minor =   0;
static struct class *msb250x_udc_class;
static u16 old_linestate;
static u8 old_soft_conn;

//#define UDC_FUNCTION_LOG

//extern u32 g_charger_flag; // 0: USB cable; 1: Adapter

void msb250x_udc_done(struct msb250x_ep *ep,
        struct msb250x_request *req, int status);

module_param(msb250x_udc_major, int, S_IRUGO);
module_param(msb250x_udc_minor, int, S_IRUGO);


#define init_MUTEX(sem)		sema_init(sem, 1)
#define init_MUTEX_LOCKED(sem)	sema_init(sem, 0)

extern void msw8533x_clear_irq(int irq);
extern u32 Get32BitsReg(u32 volatile* Reg);

static struct work_struct usb_bh;
#define msb250x_udc_kick_intr_bh() schedule_work(&usb_bh)
//static DEFINE_SPINLOCK(dev_lock);
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_clear_ep0_opr
+------------------------------------------------------------------------------
| DESCRIPTION : to clear the RxPktRdy bit
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/

static inline void msb250x_udc_clear_ep0_opr(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_clear_ep0_opr\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8(MSB250X_UDC_CSR0_SRXPKTRDY, MSB250X_UDC_CSR0_REG);
    udc_write8(MSB250X_UDC_CSR0_SRXPKTRDY,MSB250X_USBCREG(0x102));//remove index
}


/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_clear_ep0_sst
+------------------------------------------------------------------------------
| DESCRIPTION : to clear SENT_STALL
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_clear_ep0_sst(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_clear_ep0_sst\n");
#endif

    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8(0x00, MSB250X_UDC_CSR0_REG);
    udc_write8(~MSB250X_UDC_CSR0_SENTSTALL & udc_read8(MSB250X_USBCREG(0x102)), MSB250X_USBCREG(0x102));//remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_clear_ep0_se
+------------------------------------------------------------------------------
| DESCRIPTION : to clear the SetupEnd bit
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_clear_ep0_se(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_clear_ep0_se\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8(MSB250X_UDC_CSR0_SSETUPEND, MSB250X_UDC_CSR0_REG);
    udc_write8(MSB250X_UDC_CSR0_SSETUPEND,MSB250X_USBCREG(0x102));//remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_ep0_ipr
+------------------------------------------------------------------------------
| DESCRIPTION : to set the TxPktRdy bit affer loading a data packet into the FIFO
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_set_ep0_ipr(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_ep0_ipr\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8(MSB250X_UDC_CSR0_TXPKTRDY, MSB250X_UDC_CSR0_REG);
    udc_write8(MSB250X_UDC_CSR0_TXPKTRDY,MSB250X_USBCREG(0x102));//remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_ep0_de
+------------------------------------------------------------------------------
| DESCRIPTION : to set the DataEnd bit
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_set_ep0_de(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_ep0_de\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8(MSB250X_UDC_CSR0_DATAEND, MSB250X_UDC_CSR0_REG);
    udc_write8(MSB250X_UDC_CSR0_DATAEND,MSB250X_USBCREG(0x102));//remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_ep0_ss
+------------------------------------------------------------------------------
| DESCRIPTION : to set the SendStall bit to terminate the current transaction
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_set_ep0_ss(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_ep0_ss\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8(MSB250X_UDC_CSR0_SENDSTALL, MSB250X_UDC_CSR0_REG);
    udc_write8(MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_SENDSTALL,MSB250X_USBCREG(0x102));//remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_ep0_de_out
+------------------------------------------------------------------------------
| DESCRIPTION : to clear the ServiceRxPktRdy bit and set the DataEnd bit
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_set_ep0_de_out(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_ep0_de_out\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8((MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_DATAEND),
    //        MSB250X_UDC_CSR0_REG);
    udc_write8((MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_DATAEND),
            MSB250X_USBCREG(0x102));    //remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_ep0_sse_out
+------------------------------------------------------------------------------
| DESCRIPTION : to clear the ServiceRxPktRdy bit and clear the ServiceSetupEnd bit
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_set_ep0_sse_out(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_ep0_sse_out\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8((MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_SSETUPEND),
    //        MSB250X_UDC_CSR0_REG);
    udc_write8((MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_SSETUPEND),
            MSB250X_USBCREG(0x102));    //remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_ep0_de_in
+------------------------------------------------------------------------------
| DESCRIPTION : to set the TxPktRdy bit and set the DataEnd bit
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
static inline void msb250x_udc_set_ep0_de_in(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_ep0_de_in\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //udc_write8((MSB250X_UDC_CSR0_TXPKTRDY | MSB250X_UDC_CSR0_DATAEND),
    //        MSB250X_UDC_CSR0_REG);
    udc_write8((MSB250X_UDC_CSR0_TXPKTRDY | MSB250X_UDC_CSR0_DATAEND),
            MSB250X_USBCREG(0x102));    //remove index
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_nuke
+------------------------------------------------------------------------------
| DESCRIPTION : dequeue ALL requests
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| ep                        | x  |      | msb250x_ep struct point
|--------------------+---+---+-------------------------------------------------
| req                       | x  |      | msb250x_request struct point
|--------------------+---+---+-------------------------------------------------
| status                   | x  |      | reports completion code, zero or a negative errno
+--------------------+---+---+-------------------------------------------------
*/
static void msb250x_udc_nuke(struct msb250x_udc *udc,
        struct msb250x_ep *ep, int status)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_nuke\n");
#endif

    /* Sanity check */
    if (&ep->queue == NULL)
        return;

    while (!list_empty (&ep->queue))
    {
        struct msb250x_request *req;
         req = list_entry (ep->queue.next, struct msb250x_request, queue);
        msb250x_udc_done(ep, req, status);
    }
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_fifo_ctl_count
+------------------------------------------------------------------------------
| DESCRIPTION : get the endpoint 0 RX FIFO count
|
| RETURN      : count number
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                       |  |   |
+--------------------+---+---+-------------------------------------------------
*/
int msb250x_udc_fifo_ctl_count(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_ctl_count\n");
#endif
    tmp = udc_read8(MSB250X_UDC_COUNT0_REG);
    return tmp;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_fifo_ctl_count
+------------------------------------------------------------------------------
| DESCRIPTION : get the endpoint RX FIFO count except the endpoint 0
|
| RETURN      : count number
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                       |  |   |
+--------------------+---+---+-------------------------------------------------
*/
int msb250x_udc_fifo_count(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count\n");
#endif

    tmp = udc_read16(MSB250X_UDC_RXCOUNT_L_REG);
    return tmp;
}

int msb250x_udc_fifo_count_ep1(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep1\n");
#endif
    tmp = udc_read16(MSB250X_USBCREG(0x118));
    return tmp;
}

int msb250x_udc_fifo_count_ep2(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep2\n");
#endif

    tmp = udc_read16(MSB250X_USBCREG(0x128));
    return tmp;
}

int msb250x_udc_fifo_count_ep3(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep3\n");
#endif
    tmp = udc_read16(MSB250X_USBCREG(0x138));
    return tmp;
}

int msb250x_udc_fifo_count_ep4(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep4\n");
#endif

    tmp = udc_read16(MSB250X_USBCREG(0x148));
    return tmp;
}

int msb250x_udc_fifo_count_ep5(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep5\n");
#endif

    tmp = udc_read16(MSB250X_USBCREG(0x158));
    return tmp;
}

int msb250x_udc_fifo_count_ep6(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep6\n");
#endif

    tmp = udc_read16(MSB250X_USBCREG(0x168));
    return tmp;
}

int msb250x_udc_fifo_count_ep7(void)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_fifo_count_ep7\n");
#endif

    tmp = udc_read16(MSB250X_USBCREG(0x178));
    return tmp;
}

/*
-------------------------------------
		AUTO NAK SETTING
-------------------------------------
*/

void ms_NAKEnable(u8 bEndpointAddress)
{
	if(bEndpointAddress == udc_read8(MSB250X_UDC_EP_BULKOUT))
	{
		udc_write16((udc_read16(MSB250X_UDC_DMA_MODE_CTL)|(M_Mode1_P_NAK_Enable)), MSB250X_UDC_DMA_MODE_CTL);
	}

	if(bEndpointAddress == (udc_read8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f))
	{
		udc_write8((udc_read8(MSB250X_UDC_DMA_MODE_CTL1)|(M_Mode1_P_NAK_Enable_1)), MSB250X_UDC_DMA_MODE_CTL1);
	}
}

void ms_AllowAck(u8 bEndpointAddress)
{
	if(bEndpointAddress == udc_read8(MSB250X_UDC_EP_BULKOUT))
	{
		udc_write16(udc_read16(MSB250X_UDC_DMA_MODE_CTL)|M_Mode1_P_AllowAck|M_Mode1_P_NAK_Enable, MSB250X_UDC_DMA_MODE_CTL);
	}

	if(bEndpointAddress == (udc_read8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f))
	{
		udc_write8(udc_read8(MSB250X_UDC_DMA_MODE_CTL1)|M_Mode1_P_AllowAck_1|M_Mode1_P_NAK_Enable_1, MSB250X_UDC_DMA_MODE_CTL1);
	}
}

void ms_autonak_clear(u8 bEndpointAddress)
{
	if(bEndpointAddress == udc_read8(MSB250X_UDC_EP_BULKOUT))
	{
		udc_write16((udc_read16(MSB250X_UDC_EP_BULKOUT)&0xfff0), MSB250X_UDC_EP_BULKOUT); //wayne added
	}
	if(bEndpointAddress == (udc_read8(MSB250X_UDC_DMA_MODE_CTL1)&0x0f))
	{
		udc_write8((udc_read8(MSB250X_UDC_DMA_MODE_CTL1)&0xf0), MSB250X_UDC_DMA_MODE_CTL1);
	}
}

void ms_autonak_set(u8 bEndpointAddress)
{
	//set auto_nak 0
	if(udc_read8(MSB250X_UDC_EP_BULKOUT) == 0x0)
	{
		udc_write16(bEndpointAddress, MSB250X_UDC_EP_BULKOUT);
		udc_write16(udc_read16(MSB250X_UDC_DMA_MODE_CTL)|M_Mode1_P_NAK_Enable, MSB250X_UDC_DMA_MODE_CTL);
		return;
	}
	else if((udc_read8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f) == 0x0)	//set auto_nak 1
	{
		udc_write8(M_Mode1_P_NAK_Enable_1|bEndpointAddress, MSB250X_UDC_DMA_MODE_CTL1);
		return;
	}

	printk("[UDC]no more auto_nak!!\n");

	return;
}


/********************************************************************************/
#if defined(ENABLE_RIU_2BYTES_READ)
void msb250x_udc_RIU_Read2Bytes(u8 *buf, int len, uintptr_t fifo)
{
	if (len) {
		u8 *buffer = buf;
		int i = len;

		while (i > 1) {
			u16 x = __raw_readw((void *)(fifo));

			*(buffer) = (u8)x;
			*(buffer+1) = (u8)(x>>8);
			buffer = buffer + 2;
			i = i - 2;
		}

		if (i) {
			u16 x = __raw_readw((void *)(fifo));
			*(buffer) = (u8)x;
		}
	}
}
#endif
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_fifo_ctl_count
+------------------------------------------------------------------------------
| DESCRIPTION : get the usb control request info
|
| RETURN      : length
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| crq                 |x  |      | usb_ctrlrequest struct point
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_read_fifo_ctl_req(struct usb_ctrlrequest *crq)
{
    unsigned char *outbuf = (unsigned char*)crq;
    int bytes_read = 0;
    uintptr_t fifo = MSB250X_UDC_EP0_FIFO_ACCESS_L;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_read_fifo_ctl_req\n");
#endif
    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);

    bytes_read = msb250x_udc_fifo_ctl_count();
    if (bytes_read > sizeof(struct usb_ctrlrequest))
        bytes_read = sizeof(struct usb_ctrlrequest);
#if defined(ENABLE_RIU_2BYTES_READ)
	msb250x_udc_RIU_Read2Bytes(outbuf, bytes_read, fifo);
#else
    readsb((void *)/*IO_ADDRESS*/(fifo), outbuf, bytes_read);
#endif
    DBG("%s: len=%d %02x:%02x {%x,%x,%x}\n", __FUNCTION__,
        bytes_read, crq->bRequest, crq->bRequestType,
        crq->wValue, crq->wIndex, crq->wLength);

    return bytes_read;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_write_packet
+------------------------------------------------------------------------------
| DESCRIPTION : write the usb request info
|
| RETURN      : length
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| fifo                 |x  |      | the USB FIFO address register
|--------------------+---+---+-------------------------------------------------
| req                 |x  |      | msb250x_request struct point
|--------------------+---+---+-------------------------------------------------
| max                 |x  |      | max size
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_write_packet(uintptr_t fifo,
        struct msb250x_request *req, unsigned max)
{
    unsigned int len = min(req->req.length - req->req.actual, max);
    u8 *buf = req->req.buf + req->req.actual;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_write_packet\n");
#endif
    prefetch(buf);

    DBG("%s %d %d %d %d\n", __FUNCTION__,
        req->req.actual, req->req.length, len, req->req.actual + len);

    req->req.actual += len;
#ifdef TX_log
	printk("[USB]fifo len:%x\n",len);
#endif

    writesb((void *)/*IO_ADDRESS*/(fifo), buf, len);

    return len;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_write_fifo
+------------------------------------------------------------------------------
| DESCRIPTION : write the usb request info
|
| RETURN      : length
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| ep                      |x  |      | msb250x_ep struct point
|--------------------+---+---+-------------------------------------------------
| req                 |x  |      | msb250x_request struct point
+--------------------+---+---+-------------------------------------------------
*/
#if defined(CONFIG_USB_CONFIGFS_F_FS)
extern	int rx_dma_flag;
#endif
static int msb250x_udc_write_fifo(struct msb250x_ep *ep,
        struct msb250x_request *req)
{
    unsigned int count = 0;
    int is_last = 0;
    u32 idx = 0;
    uintptr_t fifo_reg = 0;
    u32 ep_csr = 0;
#if defined(CONFIG_USB_CONFIGFS_F_FS)
	u8 retry_count=0;
#endif

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_write_fifo\n");
#endif

	idx = ep->bEndpointAddress & 0x7F;
	switch (idx)
	{
		default:
			idx = 0;
		/* Fall Through */
		case 0:
			fifo_reg = MSB250X_UDC_EP0_FIFO_ACCESS_L;
			break;
		case 1:
			fifo_reg = MSB250X_UDC_EP1_FIFO_ACCESS_L;
			break;
		case 2:
			fifo_reg = MSB250X_UDC_EP2_FIFO_ACCESS_L;
			break;
		case 3:
			fifo_reg = MSB250X_UDC_EP3_FIFO_ACCESS_L;
			break;
		case 4:
			fifo_reg = MSB250X_UDC_EP4_FIFO_ACCESS_L;
			break;
		case 5:
			fifo_reg = MSB250X_UDC_EP5_FIFO_ACCESS_L;
			break;
		case 6:
			fifo_reg = MSB250X_UDC_EP6_FIFO_ACCESS_L;
			break;
		case 7:
			fifo_reg = MSB250X_UDC_EP7_FIFO_ACCESS_L;
			break;
	}

    count = msb250x_udc_write_packet(fifo_reg, req, ep->ep.maxpacket);
    /* last packet is often short (sometimes a zlp) */
    if (count != ep->ep.maxpacket)
    {
        is_last = 1;
    }
    else if (req->req.length != req->req.actual || req->req.zero)
     is_last = 0;
    else
        is_last = 2;

#if defined(CONFIG_USB_CONFIGFS_F_FS)
	while(rx_dma_flag) {
		if(udc_read8(M_REG_DMA_INTR) || udc_read16(MSB250X_UDC_INTRRX_REG))
			break;
		else
			udelay(50);

		if(retry_count>=20) {
			printk("[UDC]Polling intr end...\n");
			break;
		}
		retry_count++;
	}
#endif

    /* Only ep0 debug messages are interesting */
    if (idx == 0)
        DBG("Written ep%d %d.%d of %d b [last %d,z %d]\n",
            idx, count, req->req.actual, req->req.length,
            is_last, req->req.zero);

    if (is_last)
    {
        /* The order is important. It prevents sending 2 packets at the same time */
        if (idx == 0)
        {
            /* Reset signal => no need to say 'data sent' */
            if (! (udc_read8(MSB250X_UDC_INTRUSB_REG) & MSB250X_UDC_INTRUSB_RESET))
                msb250x_udc_set_ep0_de_in();

            ep->dev->ep0state=EP0_IDLE;
        }
		else
		{
			if(idx==1)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x112));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x112));
				//udc_write8((ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY),udc_read8(MSB250X_USBCREG(0x112)));
			}
			else if(idx==3)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x132));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x132));
			}
			else if(idx==5)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x152));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x152));
			}
			else if(idx==7)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x172));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x172));
			}
        }
#ifndef TX_modify
        msb250x_udc_done(ep, req, 0);
#endif
        is_last = 1;
    }
    else
    {
        if (idx == 0)
        {
            /* Reset signal => no need to say 'data sent' */
            if (! (udc_read8(MSB250X_UDC_INTRUSB_REG) & MSB250X_UDC_INTRUSB_RESET))
                msb250x_udc_set_ep0_ipr();
        }
		else
		{
			if(idx==1)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x112));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x112));
			}
			else if(idx==3)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x132));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x132));
			}
			else if(idx==5)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x152));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x152));
			}
			else if(idx==7)
			{
				ep_csr = udc_read8(MSB250X_USBCREG(0x172));
				udc_write8(ep_csr|MSB250X_UDC_TXCSR1_TXPKTRDY,MSB250X_USBCREG(0x172));
			}
         }
     }

     return is_last;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_read_packet
+------------------------------------------------------------------------------
| DESCRIPTION : read the usb request info
|
| RETURN      : length
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| fifo                 |x  |      | the USB FIFO address register
|--------------------+---+---+-------------------------------------------------
| buf                 |x  |      | buf point
|--------------------+---+---+-------------------------------------------------
| req                 |x  |      | msb250x_request struct point
|--------------------+---+---+-------------------------------------------------
| avail                 |x  |      | available
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_read_packet(uintptr_t fifo, u8 *buf,
        struct msb250x_request *req, unsigned avail)
{
    unsigned int len = 0;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_read_packet\n");
#endif

    /* Sanity checks */
    if(!buf)
    {
        DBG("%s buff null \n", __FUNCTION__);
        return 0;
    }

    len = min(req->req.length - req->req.actual, avail);
    req->req.actual += len;

#if defined(ENABLE_RIU_2BYTES_READ)
	msb250x_udc_RIU_Read2Bytes(buf, len, fifo);
#else
    readsb((void *)/*IO_ADDRESS*/(fifo), buf, len);
#endif

    return len;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_read_fifo
+------------------------------------------------------------------------------
| DESCRIPTION : read the usb request info
|
| RETURN      : length
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| ep                      |x  |      | msb250x_ep struct point
|--------------------+---+---+-------------------------------------------------
| req                 |x  |      | msb250x_request struct point
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_read_fifo(struct msb250x_ep *ep,
                 struct msb250x_request *req)
{
	u8 *buf = NULL;
	//u32 ep_csr = 0;
	unsigned bufferspace = 0;
	int is_last = 1;
	unsigned avail = 0;
	int fifo_count = 0;
	u32 idx = 0;
	uintptr_t fifo_reg = 0;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_read_fifo\n");
#endif

	idx = ep->bEndpointAddress & 0x7F;

	switch (idx)
	{
		default:
			idx = 0;
			/* Fall Through */
		case 0:
			fifo_reg = MSB250X_UDC_EP0_FIFO_ACCESS_L;
			break;
		case 1:
			fifo_reg = MSB250X_UDC_EP1_FIFO_ACCESS_L;
			break;
		case 2:
			fifo_reg = MSB250X_UDC_EP2_FIFO_ACCESS_L;
			break;
		case 3:
			fifo_reg = MSB250X_UDC_EP3_FIFO_ACCESS_L;
			break;
		case 4:
			fifo_reg = MSB250X_UDC_EP4_FIFO_ACCESS_L;
			break;
		case 5:
			fifo_reg = MSB250X_UDC_EP5_FIFO_ACCESS_L;
			break;
		case 6:
			fifo_reg = MSB250X_UDC_EP6_FIFO_ACCESS_L;
			break;
		case 7:
			fifo_reg = MSB250X_UDC_EP7_FIFO_ACCESS_L;
			break;
	}

    if (!req->req.length)
        return 1;

    buf = req->req.buf + req->req.actual;
    bufferspace = req->req.length - req->req.actual;
	//DBG("len %x -- buf %x -- actual %x\n", req->req.length, req->req.buf, req->req.actual);

    if (!bufferspace)
    {
        printk(KERN_ERR "%s: buffer full!\n", __FUNCTION__);
        return -1;
    }


	if(idx==1)
		fifo_count = msb250x_udc_fifo_count_ep1();
	else if(idx==2)
		fifo_count = msb250x_udc_fifo_count_ep2();
	else if(idx==3)
		fifo_count = msb250x_udc_fifo_count_ep3();
	else if(idx==4)
		fifo_count = msb250x_udc_fifo_count_ep4();
	else if(idx==5)
		fifo_count = msb250x_udc_fifo_count_ep5();
	else if(idx==6)
		fifo_count = msb250x_udc_fifo_count_ep6();
	else if(idx==7)
		fifo_count = msb250x_udc_fifo_count_ep7();
	else	//ep0
		fifo_count = msb250x_udc_fifo_ctl_count();

	DBG("%s fifo count : %d\n", __FUNCTION__, fifo_count);

	if (fifo_count > ep->ep.maxpacket)
		avail = ep->ep.maxpacket;
	else
		avail = fifo_count;
#ifdef RX_mode1_log
	printk("read fifo-> len %x -- buf %p -- avail %x\n", req->req.length, buf, avail);
#endif
    fifo_count = msb250x_udc_read_packet(fifo_reg, buf, req, avail);

    /* checking this with ep0 is not accurate as we already
     * read a control request
    **/
    if (idx != 0 && fifo_count < ep->ep.maxpacket)
    {
        is_last = 1;
        /* overflowed this request?  flush extra data */
        if (fifo_count != avail)
            req->req.status = -EOVERFLOW;
    }
    else
    {
        is_last = (req->req.length <= req->req.actual) ? 1 : 0;
    }

    /* Only ep0 debug messages are interesting */
    if (idx == 0)
        DBG("%s fifo count : %d [last %d]\n",__FUNCTION__, fifo_count,is_last);

    if (is_last)
    {
        if (idx == 0)
        {
            msb250x_udc_set_ep0_de_out();
            ep->dev->ep0state = EP0_IDLE;
        }
        else
        {
            //udc_write8(idx, MSB250X_UDC_INDEX_REG);
            //ep_csr = udc_read8(MSB250X_UDC_RXCSR1_REG);
            //udc_write8(idx, MSB250X_UDC_INDEX_REG);
            //udc_write8(ep_csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY,
            //                            MSB250X_UDC_RXCSR1_REG);
            if(idx==2)
            	udc_write8(0,MSB250X_USBCREG(0x126));
            if(idx==4)
            	udc_write8(0,MSB250X_USBCREG(0x146));
            if(idx==6)
            	udc_write8(0,MSB250X_USBCREG(0x166));
        }

        msb250x_udc_done(ep, req, 0);
#ifndef RX_modify_mode1
#ifdef CONFIG_USB_MSB250X_DMA
		/* if rx dma mode1 we have to check to set allow ack for net rx req early for net req */
		if(using_dma && ep->dev->DmaRxMode == DMA_RX_MODE1)
			msb250x_udc_schedule_done(ep);
#endif
#endif
	}
	else
	{
		if (idx == 0)
		{
			msb250x_udc_clear_ep0_opr();
		}
		else
		{
			if(idx==2)
				udc_write8(0,MSB250X_USBCREG(0x126));
			if(idx==4)
				udc_write8(0,MSB250X_USBCREG(0x146));
			if(idx==6)
				udc_write8(0,MSB250X_USBCREG(0x166));
		}
	}

    return is_last;
}

#ifdef CB2
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_do_request
+------------------------------------------------------------------------------
| DESCRIPTION : dispatch request to use DMA or FIFO
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                | x |   | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| _req               | x |   | usb_request struct point
+--------------------+---+---+-------------------------------------------------
*/
extern void Release_DMA_Channel(s8 channel);
extern u8 DmaRxMode1;
//extern u8 DmaTxMode1;
extern s8 free_dma_channels;

#ifdef RX_modify_mode1
#ifdef CONFIG_USB_MSB250X_DMA
extern void Chip_Inv_Cache_Range(unsigned long u32Addr, unsigned long u32Size);
void RxHandler(struct msb250x_ep *ep, struct msb250x_request *req)
{
	u32 ep_csr = 0;
#if defined(TIMER_PATCH) && defined(CONFIG_USB_MSB250X_DMA)
	u16 ep_idx = 0;
#endif
	int fifo_count = 0;
	//u8 save_idx = udc_read8(MSB250X_UDC_INDEX_REG);
#ifdef RX_modify_mode1
	u32 bytesleft;
	uintptr_t addr, bytesdone, address;
#endif
#ifdef UDC_FUNCTION_LOG
	   printk("RxHandler\n");
#endif

	if((ep->bEndpointAddress & 0x7F)==2)
	{
		ep_csr = udc_read8(MSB250X_USBCREG(0x126));
		fifo_count = msb250x_udc_fifo_count_ep2();
	}
	if((ep->bEndpointAddress & 0x7F)==4)
	{
		ep_csr = udc_read8(MSB250X_USBCREG(0x146));
		fifo_count = msb250x_udc_fifo_count_ep4();
	}
	if((ep->bEndpointAddress & 0x7F)==6)
	{
		ep_csr = udc_read8(MSB250X_USBCREG(0x166));
		fifo_count = msb250x_udc_fifo_count_ep6();
	}


#if defined(CONFIG_USB_CONFIGFS_F_FS)
	if(rx_dma_flag==1)
	{
		rx_dma_flag=0;
		udelay(100);
	}
#endif

	if (ep->DmaRxMode1)
	{
		bytesleft = Get32BitsReg((u32 volatile *)(DMA_COUNT_REGISTER(ep->ch)));
#ifdef RX_mode1_log
		printk("bytesleft:%x\n",bytesleft);
#endif
		//if(fifo_count==bytesleft)
		addr = Get32BitsReg((u32 volatile *)(DMA_ADDR_REGISTER(ep->ch)));
#ifdef RX_mode1_log
		printk("short pack stop, addr:%x\n",addr);
#endif
		addr=PA2BUS(addr);

		bytesdone = (uintptr_t)/*phys_to_virt*/(phys_to_virt((uintptr_t)(addr)) - (uintptr_t)(req->req.buf + req->req.actual));
		address = (uintptr_t)(req->req.buf) + (uintptr_t)(req->req.actual);
		Chip_Inv_Cache_Range(address,bytesdone);
		req->req.actual+=bytesdone;
#ifdef RX_mode1_log
		printk("[USB]RX_handler actual:%x,bytesdone:%x\n",req->req.actual,bytesdone);
#endif
#if defined(TIMER_PATCH) && defined(CONFIG_USB_MSB250X_DMA)
		//hw may subtraction to an negative number
		if(fifo_count>=ep->wMaxPacketSize)
		{
			ep_idx=0x106+(0x10*ep->bEndpointAddress);
			udc_write8(0,MSB250X_USBCREG(ep_idx));
			msb250x_udc_done(ep, req, 0);
		}
		else
			msb250x_udc_read_fifo(ep, req);
#else
		msb250x_udc_read_fifo(ep, req);
#endif
		Release_DMA_Channel(ep->ch);
#if defined(TIMER_PATCH)
		ms_stop_timer(ep);
#endif
		//DmaRxMode1=FALSE;
		ep->DmaRxMode1=0;
		ep->RxShort=1;
		req = NULL;
		//allow ACK
		msb250x_udc_schedule_done(ep);
	}
	else if (check_dma_busy()==DMA_NOT_BUSY)
	{
		if(fifo_count >= ep->ep.maxpacket)
		{
			if((req->req.actual + fifo_count) > req->req.length)
				printk(KERN_ERR "usb req buffer is too small\n");
			if(USB_Set_DMA(&ep->ep, req, fifo_count,DMA_RX_ZERO_IRQ) != SUCCESS)
			{
				DBG("USB_CLASS_COMM: DMA fail use FIFO\n");
				if(msb250x_udc_read_fifo(ep, req))
					req = NULL;
			}
		}
		else
		{
#ifdef RX_mode1_log
			printk("[USB]rx short data read fifo count:%x\n",fifo_count);
#endif
			if(msb250x_udc_read_fifo(ep, req))
				req = NULL;

			msb250x_udc_schedule_done(ep);
		}
	}
	else if(check_dma_busy()==DMA_BUSY)
	{
#ifdef RX_mode1_log
		printk("[USB]RxHandler>>rx dma busy:%x\n",fifo_count);
#endif
		if(fifo_count >= ep->ep.maxpacket)
		{
			if((req->req.actual + fifo_count) > req->req.length)
				printk(KERN_ERR "usb req buffer is too small\n");

			if(msb250x_udc_read_fifo(ep, req))
				req = NULL;
		}
		else
		{
#ifdef RX_mode1_log
			printk("[USB]rx__ short data read fifo count:%x\n",fifo_count);
#endif
			if(msb250x_udc_read_fifo(ep, req))
				req = NULL;

			msb250x_udc_schedule_done(ep);
		}
	}
}
#endif
#endif

struct msb250x_request * msb250x_udc_do_request(struct msb250x_ep *ep, struct msb250x_request *req)
{
	u8 csr2;
	u16 ep_idx;
    u32 ep_csr = 0;
    int fifo_count = 0;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_do_request\n");
#endif

    //u8 save_idx = udc_read8(MSB250X_UDC_INDEX_REG);
#ifdef TX_log
	printk("[USB]msb250x_udc_do_request\n");
#endif
	if (ep->bEndpointAddress)
	{
		if((ep->bEndpointAddress & 0x7F)==1)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x112));
			fifo_count = msb250x_udc_fifo_count_ep1();
		}
		else if((ep->bEndpointAddress & 0x7F)==2)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x126));
			fifo_count = msb250x_udc_fifo_count_ep2();
		}
		else if((ep->bEndpointAddress & 0x7F)==3)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x132));
			fifo_count = msb250x_udc_fifo_count_ep3();
		}
		else if((ep->bEndpointAddress & 0x7F)==4)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x146));
			fifo_count = msb250x_udc_fifo_count_ep4();
		}
		else if((ep->bEndpointAddress & 0x7F)==5)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x152));
			fifo_count = msb250x_udc_fifo_count_ep5();
		}
		else if((ep->bEndpointAddress & 0x7F)==6)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x166));
			fifo_count = msb250x_udc_fifo_count_ep6();
		}
		else if((ep->bEndpointAddress & 0x7F)==7)
		{
			ep_csr = udc_read8(MSB250X_USBCREG(0x172));
			fifo_count = msb250x_udc_fifo_count_ep7();
		}
	}
	else
	{
		ep_csr = udc_read8(MSB250X_USBCREG(0x102));
		fifo_count = msb250x_udc_fifo_ctl_count();
	}

    DBG("ep %d fifo_count: %x req.len %x \n", ep->bEndpointAddress & 0x7F, fifo_count, req->req.length);
#ifdef RX_mode1_log
    printk("ep %d fifo_count: %x req.len %x \n", ep->bEndpointAddress & 0x7F, fifo_count, req->req.length);
#endif
    if(!ep->halted)
    {
        if (ep->bEndpointAddress == MSB250X_UDC_INDEX_EP0 )
        {
            switch (ep->dev->ep0state)
            {
                case EP0_IN_DATA_PHASE:
                    if (!(ep_csr & MSB250X_UDC_CSR0_TXPKTRDY)
                           && msb250x_udc_write_fifo(ep, req))
                    {
                        ep->dev->ep0state = EP0_IDLE;
#ifndef TX_modify
                        req = NULL;
#endif
                    }
                    break;

                case EP0_OUT_DATA_PHASE:
					/* -----------------------*/
					/* !!! Special case !!!   */
					/* EP0 CTRL Write no data */
					if (!req->req.length) {

                        ep->dev->ep0state = EP0_IDLE;
                        req = NULL;
						msb250x_udc_set_ep0_de_out();

					}
					/*-------------------------*/
					else
					if ((ep_csr & MSB250X_UDC_CSR0_RXPKTRDY) && msb250x_udc_read_fifo(ep, req)) {
                        ep->dev->ep0state = EP0_IDLE;
                        req = NULL;
                    }
                    break;

                default:
                    printk(KERN_ERR " EP0 Request Error !!\n");
                    return req;
            }
        }
#ifdef CONFIG_USB_MSB250X_DMA
        else if (using_dma && (ep->bEndpointAddress & USB_DIR_IN))
        {
        	//putb("do_req_TX",0,0);
        /* DMA TX: */
           // if(!(ep_csr & MSB250X_UDC_TXCSR1_TXPKTRDY))
            {
#ifndef xxxx
                if(check_dma_busy() == DMA_BUSY)
                {
                    /* Double check to insure this EP is not DMAing */
                    u8 endpoint = (u8)(udc_read16((uintptr_t)(DMA_CNTL_REGISTER(1))) & 0xf0) >> DMA_ENDPOINT_SHIFT;
					//putb("TX_FIFO",0,0);
                    if(endpoint != (ep->bEndpointAddress &0x7f) && msb250x_udc_write_fifo(ep, req)){
#ifndef TX_modify
                        req = NULL;
#endif
                   }
                }
                else
#endif
#ifdef xxxx
				//printk("dma_busy:%x\n",check_dma_busy());
				if (check_dma_busy()==DMA_NOT_BUSY)
#endif
                {
			if ((req->req.length >= ep->ep.maxpacket) && (req->req.length>8))
                    {
                        u32 tx_dma_count = (u32)(req->req.length) - (u32)(req->req.actual);
						//putb("TX_DMA",0,0);
                        /* If req len equal to maxpacket, it will use DMA TX mode0 automaticly */
                        if(USB_Set_DMA(&ep->ep, req, tx_dma_count, DMA_TX_ONE_IRQ) != SUCCESS)
                        {
                            DBG("Use fifo write mode\n");
							//putb("Use fifo write mode",0,0);
                            if(msb250x_udc_write_fifo(ep, req)){
 #ifndef TX_modify
                                req = NULL;
 #endif
                            }
                        }
                    }
                    else
                    {
#ifdef TX_log
                    	printk("[USB]do_request--write_fifo\n");
#endif
						//putb("do_req--write_fifo",0,0);
                        if(msb250x_udc_write_fifo(ep, req)){
#ifndef TX_modify
                            req = NULL;
#endif
                        }
                    }
                }
            }
        }
        else if (using_dma && !(ep->bEndpointAddress & USB_DIR_IN) && (ep->dev->DmaRxMode != DMA_RX_MODE_NULL))
        {
#ifdef RX_mode1_log
        	printk("[USB]DMA_RX\n");
#endif
			/* DMA RX: */
			if(ep->dev->DmaRxMode == DMA_RX_MODE0)
            {
            /* DMA mode 0 */
                if(ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY)
                {
#ifndef RX_modify_mode1
                    if(check_dma_busy() == DMA_BUSY)
                    {
                        /* Double check to insure this EP is not DMAing */
                        u8 endpoint = (udc_read16((u32)(DMA_CNTL_REGISTER(1))) & 0xf0) >> DMA_ENDPOINT_SHIFT;
                        if(endpoint != (ep->bEndpointAddress &0x7f) && msb250x_udc_read_fifo(ep, req))
                            req = NULL;
                    }
                    else
#endif
#ifdef RX_modify_mode1
					if (check_dma_busy()==DMA_NOT_BUSY)
#endif
                    {
#ifdef RX_mode1_log
                    	printk("[USB]SET_DMA_RX\n");
#endif
                        if(fifo_count >= ep->ep.maxpacket)
                        {
                            if((req->req.actual + fifo_count) > req->req.length)
                                printk(KERN_ERR "usb req buffer is too small\n");

                          if(USB_Set_DMA(&ep->ep, req, fifo_count,DMA_RX_ZERO_IRQ) != SUCCESS)
                            {
                                DBG("USB_CLASS_COMM: DMA fail use FIFO\n");
                                if(msb250x_udc_read_fifo(ep, req))
                                    req = NULL;
                            }
                        }
                        else
                        {
#ifdef RX_mode1_log
							printk("[USB]2FIFO_COUNT:%x\n",fifo_count);
#endif
							if(msb250x_udc_read_fifo(ep, req))
								req = NULL;
						}
					}
				}
			}
			else
			{
            /* DMA mode 1*/
#ifdef RX_modify_mode1
#ifdef RX_mode1_log
				printk("[USB]do_req:rx1\n");
#endif
				{
#ifdef RX_mode1_log
					printk("[USB]do mode 1 for all left \n");
#endif
					if( (req->req.length-req->req.actual)>  ep->ep.maxpacket)
					{
#ifdef RX_mode1_log
					printk("[USB]length:%x,actual:%x\n",req->req.length,req->req.actual);
#endif
						if(check_dma_busy() != DMA_BUSY)
						{
							u32 rx_dma_count=0;
							if(((u32)(req->req.actual))>((u32)(req->req.length)))
							{
								printk("actual[0x%x]>len[0x%x]\n",(u32)(req->req.actual),(u32)(req->req.length));
								msb250x_udc_done(ep, req, -EOVERFLOW);
								return req;
							}

							rx_dma_count = (u32)(req->req.length) - (u32)(req->req.actual);
#ifdef RX_mode1_log
							printk("[USB]set DMA mode1\n");
#endif
							if(USB_Set_DMA(&ep->ep, req, rx_dma_count, DMA_RX_ONE_IRQ) != SUCCESS)
							{
								DBG(KERN_ERR "USB_CLASS_MASS_STORAGE: Set DMA fail FIFO\n");
							}
#ifdef RX_mode1_log
							printk("[USB]allow ack\n");
#endif
						}
						else
						{
							DBG(KERN_ERR "DMA busy.. just queue req \n");
						}
					}
					else
					{
#ifdef RX_mode1_log
						printk("[USB]allow ack\n");
#endif
						ep_idx=0x106+(0x10*ep->bEndpointAddress);
						csr2 = udc_read8((MSB250X_USBCREG(ep_idx))+1);
						udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(ep_idx))+1);
						if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
							ms_AllowAck(ep->bEndpointAddress);
					}
				}
#endif
#ifndef RX_modify_mode1
				if( req->req.length > ep->ep.maxpacket)
				{
					if(check_dma_busy() != DMA_BUSY)
					{
						u32 rx_dma_count = (u32)(req->req.length) - (u32)(req->req.actual);
						if(USB_Set_DMA(&ep->ep, req, rx_dma_count, DMA_RX_ONE_IRQ) != SUCCESS){
							DBG(KERN_ERR "USB_CLASS_MASS_STORAGE: Set DMA fail FIFO\n");
						}
					}
					else{
						DBG(KERN_ERR "DMA busy.. just queue req \n");
					}
				}
				else
				{
					if(ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY)
					{
						if(msb250x_udc_read_fifo(ep, req))
							req = NULL;
					}
					else
						USB_Set_ClrRXMode1();
				}
#endif
			}
		}
#endif    /* CONFIG_USB_MSB250X_DMA */
		else if ((ep->bEndpointAddress & USB_DIR_IN) != 0 && (!(ep_csr & MSB250X_UDC_TXCSR1_TXPKTRDY))
					&& msb250x_udc_write_fifo(ep, req))    /* IN token packet */
		{
#ifdef TX_log
			printk("[USB]TXPACKET_NOT_READY_FIFO_fifo_write\n");
#endif
#ifndef TX_modify
			req = NULL;
#endif
		}
		else if (!(ep->bEndpointAddress & USB_DIR_IN))    /* OUT token packet */
		{
#ifdef RX_mode1_log
			printk("[USB]OUT token packet\n");
#endif
			if ((ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY) &&
					(msb250x_udc_read_fifo(ep, req)))
			{
#ifdef RX_mode1_log
				printk("[USB]MSB250X_UDC_RXCSR1_RXPKTRDY_fifo_read\n");
#endif
				req = NULL;
			}
		}
	}
#ifdef RX_mode1_log
	else
	{
		printk("[USB]halted, return\n");
	}
#endif
	return req;
}

#ifdef CONFIG_USB_MSB250X_DMA
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_schedule_DMA
+------------------------------------------------------------------------------
| DESCRIPTION : Schedule the next DMA EP
|
| RETURN      :
+------------------------------------------------------------------------------
| Variable Name  |IN |OUT|                   Usage
|----------------+---+---+-----------------------------------------------------
| ep             | x |   | msb250x_ep struct point
+-------------- -+---+---+-----------------------------------------------------
*/
s8 msb250x_udc_schedule_DMA(struct msb250x_ep *ep)
{
    struct msb250x_request *req = NULL;
    u8 ep_idx = 0;
    struct msb250x_ep *cur_ep;
    static u8 token = 0;
    u8 i;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_schedule_DMA\n");
#endif


    for(i = 1; i < MSB250X_ENDPOINTS - 1; i++)
    {
        token = (token + 1)%(MSB250X_ENDPOINTS-1);

        ep_idx = token + 1;

        cur_ep = &(ep->dev->ep[ep_idx]);

        if (req == NULL && likely (!list_empty(&cur_ep->queue)))
        {
            req = list_entry(cur_ep->queue.next, struct msb250x_request, queue);

            if(req && req->req.length >= ep->ep.maxpacket)
            {
                if(msb250x_udc_do_request(cur_ep, req) != NULL)
                    break;
            }
        }
    }

    return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_schedule_done
+------------------------------------------------------------------------------
| DESCRIPTION : Schedule the next packet for this EP
|
| RETURN      :
+------------------------------------------------------------------------------
| Variable Name  |IN |OUT|                   Usage
|----------------+---+---+-----------------------------------------------------
| ep             | x |   | msb250x_ep struct point
+-------------- -+---+---+-----------------------------------------------------
*/
s8 msb250x_udc_schedule_done(struct msb250x_ep *ep)
{
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_schedule_done\n");
#endif

    if (likely (!list_empty(&ep->queue)))
    {
        struct msb250x_request *req = NULL;
		//printk("[USB]CHECK QUEUE\n");
        req = list_entry(ep->queue.next, struct msb250x_request, queue);
        msb250x_udc_do_request(ep, req);
    }

    return 0;
}
#endif
#endif
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_done
+------------------------------------------------------------------------------
| DESCRIPTION : complete the usb request
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| ep                        | x  |      | msb250x_ep struct point
|--------------------+---+---+-------------------------------------------------
| req                       | x  |      | msb250x_request struct point
|--------------------+---+---+-------------------------------------------------
| status                   | x  |      | reports completion code, zero or a negative errno
+--------------------+---+---+-------------------------------------------------
*/
#ifdef CB2
void msb250x_udc_done(struct msb250x_ep *ep,
        struct msb250x_request *req, int status)
{
	unsigned halted = ep->halted;
	struct msb250x_udc     *dev = NULL;
	//u32 *prt;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_done\n");
#endif

	DBG("done %d actral %d length %x\n", ep->num, req->req.actual, req->req.length);
	//putb("done ep's_req",ep->num,0);
	dev = ep->dev;

	if(req==NULL)
	{
		printk("[USB]REQ NULL\n");
		return;
	}

	if (ep->num)
		DBG_TR("complete %u\n", ep->num);

	list_del_init(&req->queue);
	if(!(list_empty(&req->queue)))
	{
		printk("[USB]queue not empty!!\n");
	}

	if (likely (req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	ep->halted = 1;
	spin_unlock(&dev->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&dev->lock);
	ep->halted = halted;

	return;
}

#else
void msb250x_udc_done(struct msb250x_ep *ep,
        struct msb250x_request *req, int status)
{
    unsigned halted = ep->halted;
#ifdef CONFIG_USB_MSB250X_DMA
    u32 ep_csr = 0;
    u8 idx, saved_idx;
    u8 inreg;
#endif /* CONFIG_USB_MSB250X_DMA */

    // DBG("done %d\n", ep->num);
    if (ep->num)
        DBG_TR("complete %u\n", ep->num);

    list_del_init(&req->queue);

    if (likely (req->req.status == -EINPROGRESS))
        req->req.status = status;
    else
        status = req->req.status;

    ep->halted = 1;
    req->req.complete(&ep->ep, &req->req);
    ep->halted = halted;

#ifdef CONFIG_USB_MSB250X_DMA
    if (likely (!list_empty(&ep->queue)))
    {
    	//putb("find queue",ep->bEndpointAddress,0);
        req = list_entry(ep->queue.next, struct msb250x_request, queue);
    }
    else
    {
    	//putb("queue null",ep->bEndpointAddress,0);
        req = NULL;
    }

    if (req)
    {
        idx = ep->bEndpointAddress & 0x7f;

        if (idx != 0)
        {
            /* Save index */
            saved_idx = udc_read8(MSB250X_UDC_INDEX_REG);
            udc_write8(idx, MSB250X_UDC_INDEX_REG);

            ep_csr = udc_read8((ep->bEndpointAddress & USB_DIR_IN)
                                ? MSB250X_UDC_TXCSR1_REG
                                : MSB250X_UDC_RXCSR1_REG);
            if (check_dma_busy() != DMA_BUSY)
            {
                if (using_dma && (req->req.length > (ep->ep.maxpacket)))
                {
                    if (!(ep->bEndpointAddress & USB_DIR_IN)) /* OUT token packet */
                    {
                        if (unlikely(USB_Set_DMA(&ep->ep, req, DMA_RX_ONE_IRQ)<0))
                        {
                            printk("DMA Rx Error!!\n");
                        }
                    }
                    else /* IN token packet */
                    {
                        inreg = udc_read16(MSB250X_UDC_TXCSR1_REG);
                        if(!(inreg & MSB250X_UDC_TXCSR1_FIFONOEMPTY)||!(inreg & MSB250X_UDC_TXCSR1_SENTSTALL))
                        {
                            if (unlikely(USB_Set_DMA(&ep->ep, req, DMA_TX_ONE_IRQ)<0))
                            {
                                printk("DMA Tx Error");
                            }
                        }
                        // else
                        // {
                        //     printk("TX FIFO not EMPTY!\n");
                        // }
                    }
                }
                else if ((ep->bEndpointAddress & USB_DIR_IN) != 0 /* IN token packet */
                                    && (!(ep_csr & MSB250X_UDC_TXCSR1_TXPKTRDY))
                                    && msb250x_udc_write_fifo(ep, req))
                {
                    req = NULL;
                }
                else if (!(ep->bEndpointAddress & USB_DIR_IN)) /* OUT token packet */
                {
                    int fifo_count;

                    USB_Set_ClrRXMode1(); // winder

                    fifo_count = msb250x_udc_fifo_count();

                    if ((fifo_count != 0) && (ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY))
                    {
                        msb250x_udc_read_fifo(ep, req);
                    }
                }
            }
            /* restore previous index value */
            udc_write8(saved_idx, MSB250X_UDC_INDEX_REG);
        }
    }
#endif /* CONFIG_USB_MSB250X_DMA */

    return;
}
#endif
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_get_status
+------------------------------------------------------------------------------
| DESCRIPTION :get the USB device status
|
| RETURN      :0 when success, error code in other case.
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| dev                      |x  |      | msb250x_udc struct point
|--------------------+---+---+-------------------------------------------------
| crq                        |x  |      | usb_ctrlrequest struct point
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_get_status(struct msb250x_udc *dev,
        struct usb_ctrlrequest *crq)
{
	u16 status = 0;
	u8 ep_num = crq->wIndex & 0x7F;
	u8 is_in = crq->wIndex & USB_DIR_IN;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_get_status\n");
#endif

	switch (crq->bRequestType & USB_RECIP_MASK)
	{
		case USB_RECIP_INTERFACE:
			break;

		case USB_RECIP_DEVICE:
			status = dev->devstatus;
			break;

		case USB_RECIP_ENDPOINT:
			if (ep_num > 7 || crq->wLength > 2)
				return 1;

			if (ep_num == 0)
			{
				status = udc_read8(MSB250X_USBCREG(0x102));
				status = status & MSB250X_UDC_CSR0_SENDSTALL;
			}
			else
			{
				if (is_in)
				{
					if(ep_num==1)
						status = udc_read8(MSB250X_USBCREG(0x112));
					else if(ep_num==3)
						status = udc_read8(MSB250X_USBCREG(0x132));
					else if(ep_num==5)
						status = udc_read8(MSB250X_USBCREG(0x152));
					else if(ep_num==7)
						status = udc_read8(MSB250X_USBCREG(0x172));

					status = status & MSB250X_UDC_TXCSR1_SENDSTALL;
				}
				else
				{
					if(ep_num==2)
						status = udc_read8(MSB250X_USBCREG(0x126));
					if(ep_num==4)
						status = udc_read8(MSB250X_USBCREG(0x146));
					if(ep_num==6)
						status = udc_read8(MSB250X_USBCREG(0x166));

					status = status & MSB250X_UDC_RXCSR1_SENDSTALL;
				}
			}

			status = status ? 1 : 0;
			break;

		default:
			return 1;
	}

	/* Seems to be needed to get it working. ouch :( */
	udelay(5);
	udc_write8(status & 0xFF, MSB250X_UDC_EP0_FIFO_ACCESS_L);
	udc_write8(status >> 8, MSB250X_UDC_EP0_FIFO_ACCESS_L);
	msb250x_udc_set_ep0_de_in();

    return 0;
}

/*----------- msb250x_udc_set_halt prototype ------------*/
static int msb250x_udc_set_halt(struct usb_ep *_ep, int value);

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_handle_ep0_idle
+------------------------------------------------------------------------------
| DESCRIPTION :handle the endpoint 0 when endpoint 0 is idle
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| dev                 |x  |      | msb250x_udc struct point
|--------------------+---+---+-------------------------------------------------
| ep                      |x  |      | msb250x_ep struct point
|--------------------+---+---+-------------------------------------------------
| crq                 |x  |      | usb_ctrlrequest struct point
|--------------------+---+---+-------------------------------------------------
| ep0csr                 |x  |      | the csr0 register value
+--------------------+---+---+-------------------------------------------------
*/
static void msb250x_udc_handle_ep0_idle(struct msb250x_udc *dev,
                    struct msb250x_ep *ep,
                    struct usb_ctrlrequest *crq,
                    uintptr_t ep0csr)
{
    int len = 0, ret = 0 , tmp = 0;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_handle_ep0_idle\n");
#endif

    /* start control request? */
    if (!(ep0csr & MSB250X_UDC_CSR0_RXPKTRDY))
        return;

    msb250x_udc_nuke(dev, ep, -EPROTO);

    len = msb250x_udc_read_fifo_ctl_req(crq);
    if (len != sizeof(*crq))
    {
    	//putb("fifo READ ERROR",0,0);
		printk("len:%x,crq:%x\n", len, (unsigned int)(sizeof(*crq)));
        printk(KERN_ERR "setup begin: fifo READ ERROR"
             " wanted %d bytes got %d. Stalling out...\n", (unsigned int)(sizeof(*crq)), len);

        msb250x_udc_set_ep0_ss();
            return;
    }

    DBG("bRequest = %x bRequestType %x wLength = %d\n",
            crq->bRequest, crq->bRequestType, crq->wLength);

    /* cope with automagic for some standard requests. */
    dev->req_std = (crq->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD;
    dev->req_config = 0;
    dev->req_pending = 1;

    if (dev->req_std)
    {
        switch (crq->bRequest)
        {
            case USB_REQ_SET_CONFIGURATION:
                DBG("USB_REQ_SET_CONFIGURATION ... \n");
                if (crq->bRequestType == USB_RECIP_DEVICE)
                {
                    dev->req_config = 1;
					/*
						control write no-data case,
						zero length packet should move to do_request end
					*/
                    /* msb250x_udc_set_ep0_de_out(); */
                }
                break;

            case USB_REQ_SET_INTERFACE:
                DBG("USB_REQ_SET_INTERFACE ... \n");

                if (crq->bRequestType == USB_RECIP_INTERFACE)
                {
                    dev->req_config = 1;
					/*
						control write no-data case,
						zero length packet should move to do_request end
					*/
                    /* msb250x_udc_set_ep0_de_out(); */
                }
                break;

            case USB_REQ_SET_ADDRESS:
                DBG("USB_REQ_SET_ADDRESS ... \n");

                if (crq->bRequestType == USB_RECIP_DEVICE)
                {
                    tmp = crq->wValue & 0x7F;
                    dev->address = tmp;
                    udc_write8(tmp, MSB250X_UDC_FADDR_REG);
                    msb250x_udc_set_ep0_de_out();
                    return;
                }
                break;

            case USB_REQ_GET_STATUS:
                DBG("USB_REQ_GET_STATUS ... \n");

                msb250x_udc_clear_ep0_opr();
                if (dev->req_std)
                {
                    if (!msb250x_udc_get_status(dev, crq))
                    {
                        return;
                    }
                }
                break;

            case USB_REQ_CLEAR_FEATURE:
                DBG("USB_REQ_CLEAR_FEATURE ... \n");
                msb250x_udc_clear_ep0_opr();
                if (crq->bRequestType != USB_RECIP_ENDPOINT)
                    break;

                if (crq->wValue != USB_ENDPOINT_HALT || crq->wLength != 0)
                    break;

                msb250x_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 0);
                msb250x_udc_set_ep0_de_out();
                return;

            case USB_REQ_SET_FEATURE:
                DBG("USB_REQ_SET_FEATURE ... \n");

                msb250x_udc_clear_ep0_opr();

				if(crq->bRequestType == USB_RECIP_DEVICE)
				{
					if(crq->wValue==0x02)//USB20_TEST_MODE
					{
						//nUsb20TestMode=crq->wIndex;
					}
				}
                if (crq->bRequestType != USB_RECIP_ENDPOINT)
                    break;

                if (crq->wValue != USB_ENDPOINT_HALT || crq->wLength != 0)
                    break;

                msb250x_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 1);
                msb250x_udc_set_ep0_de_out();
                return;

            default:
                msb250x_udc_clear_ep0_opr();
                break;
        }
    }
    else
    {
        msb250x_udc_clear_ep0_opr();
    }

    if (crq->bRequestType & USB_DIR_IN){
#ifdef test
    		printk("EP0_IN_DATA_PHASE\n");
#endif
        dev->ep0state = EP0_IN_DATA_PHASE;
        }
    else{
#ifdef test
    		printk("EP0_OUT_DATA_PHASE\n");
#endif
        dev->ep0state = EP0_OUT_DATA_PHASE;
	}


    if (dev->driver && dev->driver->setup)
    {
    	spin_unlock (&dev->lock);
        ret = dev->driver->setup(&dev->gadget, crq);
		spin_lock (&dev->lock);
    }
    else
        ret = -EINVAL;


    if (ret < 0)
    {
        if (dev->req_config)
        {
            DBG("config change %02x fail %d?\n",
                        crq->bRequest, ret);
            return;
        }

        if (ret == -EOPNOTSUPP)
            DBG("Operation not supported\n");
        else
            DBG("dev->driver->setup failed. (%d)\n", ret);

        udelay(5);

        msb250x_udc_set_ep0_ss();
		/* msb250x_udc_set_ep0_de_out(); */

        dev->ep0state = EP0_IDLE;

        /* deferred i/o == no response yet */
    }
    else if (dev->req_pending)
    {
        DBG("dev->req_pending... what now?\n");
        dev->req_pending = 0;
    }

    DBG("ep0state %s\n", ep0states[dev->ep0state]);
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_handle_ep0
+------------------------------------------------------------------------------
| DESCRIPTION :handle the endpoint 0
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| dev                 |x  |      | msb250x_udc struct point
+--------------------+---+---+-------------------------------------------------
*/
int magic_number=0;
static void msb250x_udc_handle_ep0(struct msb250x_udc *dev)
{
    uintptr_t ep0csr = 0;
#ifdef CB2
	u32 rxcnt = 0;
#endif
    struct msb250x_ep    *ep = &dev->ep[0];
    struct msb250x_request    *req = NULL;
    struct usb_ctrlrequest    crq;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_handle_ep0\n");
#endif

    if (list_empty(&ep->queue))
        req = NULL;
    else
        req = list_entry(ep->queue.next, struct msb250x_request, queue);

    //udc_write8(MSB250X_UDC_INDEX_EP0, MSB250X_UDC_INDEX_REG);
    //ep0csr = udc_read8(MSB250X_UDC_CSR0_REG);
    ep0csr = udc_read8(MSB250X_USBCREG(0x102));
    rxcnt =	udc_read8(MSB250X_USBCREG(0x108));

    /* clear stall status */
    if (ep0csr & MSB250X_UDC_CSR0_SENTSTALL)
    {
        DBG("... clear SENT_STALL ...\n");
        msb250x_udc_nuke(dev, ep, -EPIPE);
        msb250x_udc_clear_ep0_sst();
        dev->ep0state = EP0_IDLE;
        /* return; */
    }

    /* clear setup end */
    if (ep0csr & MSB250X_UDC_CSR0_SETUPEND)
    {
        DBG("... serviced SETUP_END ...\n");
        msb250x_udc_nuke(dev, ep, 0);
        msb250x_udc_clear_ep0_se();

        dev->ep0state = EP0_IDLE;
    }

    switch (dev->ep0state)
    {
        case EP0_IDLE:
            msb250x_udc_handle_ep0_idle(dev, ep, &crq, ep0csr);
            break;

        case EP0_IN_DATA_PHASE:     /* GET_DESCRIPTOR etc */
            DBG("EP0_IN_DATA_PHASE ... what now?\n");
            if (!(ep0csr & MSB250X_UDC_CSR0_TXPKTRDY) && req)
            {
                msb250x_udc_write_fifo(ep, req);
            }
            break;

        case EP0_OUT_DATA_PHASE:    /* SET_DESCRIPTOR etc */
            DBG("EP0_OUT_DATA_PHASE ... what now?\n");
            if ((ep0csr & MSB250X_UDC_CSR0_RXPKTRDY) && req )
            {
                msb250x_udc_read_fifo(ep,req);
            }
            break;

        case EP0_END_XFER:
            DBG("EP0_END_XFER ... what now?\n");
            dev->ep0state = EP0_IDLE;
            break;

        case EP0_STALL:
            DBG("EP0_STALL ... what now?\n");
            dev->ep0state = EP0_IDLE;
            break;

        default:
            DBG("EP0 status ... what now?\n");
            break;
    }
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_handle_ep
+------------------------------------------------------------------------------
| DESCRIPTION :handle the endpoint except endpoint 0
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| ep                        |x  |      | msb250x_ep struct point
+--------------------+---+---+-------------------------------------------------
*/
int i=0;
extern s8 dma_busy;
static void msb250x_udc_handle_ep(struct msb250x_ep *ep)
{
	struct msb250x_request    *req = NULL;
	int is_in = 0;
	u32 ep_csr1 = 0;
	u32 idx = 0;
	int tx_left_flag=0;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_handle_ep\n");
#endif

	if (likely (!list_empty(&ep->queue)))
	{
#ifdef TX_log
		printk("find ep%x req\n",ep->bEndpointAddress);
#endif
		req = list_entry(ep->queue.next, struct msb250x_request, queue);
	}
	else
	{
#ifdef TX_log
		printk("no find ep%x req in queue\n",ep->bEndpointAddress);
#endif
		req = NULL;
	}

	idx = ep->bEndpointAddress & 0x7F;
	is_in = ep->bEndpointAddress & USB_DIR_IN;

	if (is_in)
	{
#ifdef TX_log
		printk("[USB]TX_INTTRUPT\n");
#endif
#ifdef TX_modify
#ifdef TX_log
		printk("msb250x_udc_handle_ep--DONE\n");
#endif
		if (req==NULL){
			printk("[USB]TX_REQ NULL\n");
		} else {

			if((req->req.length)-((unsigned)(req->req.actual)) > 0)
			{
				msb250x_udc_write_fifo(ep, req);
				tx_left_flag=1;
			}
			else
				msb250x_udc_done(ep, req, 0);
		}
#endif
		if(idx==1)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x112));
		else if(idx==3)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x132));
		else if(idx==5)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x152));
		else if(idx==7)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x172));

		DBG("ep%01d write csr:%02x %d\n", idx, ep_csr1, req ? 1 : 0);

		if (ep_csr1 & MSB250X_UDC_TXCSR1_SENTSTALL)
		{
			DBG("tx st\n");

			if(idx==1)
				udc_write8(ep_csr1 & ~MSB250X_UDC_TXCSR1_SENTSTALL, MSB250X_USBCREG(0x112));
			else if(idx==3)
				udc_write8(ep_csr1 & ~MSB250X_UDC_TXCSR1_SENTSTALL, MSB250X_USBCREG(0x132));
			else if(idx==5)
				udc_write8(ep_csr1 & ~MSB250X_UDC_TXCSR1_SENTSTALL, MSB250X_USBCREG(0x152));
			else if(idx==7)
				udc_write8(ep_csr1 & ~MSB250X_UDC_TXCSR1_SENTSTALL, MSB250X_USBCREG(0x172));
			return;
		}
#ifdef TX_modify
#ifdef CONFIG_USB_MSB250X_DMA
		if(tx_left_flag==0)
			msb250x_udc_schedule_done(ep);
#endif
#endif
#ifndef TX_modify
		if(req != NULL)
		{
			msb250x_udc_do_request(ep, req);
		}
#endif
	}
	else
	{
#ifdef RX_mode1_log
		printk("[USB]RX_INTTRUPT:%x\n",ep->bEndpointAddress);
#endif
#ifdef NAK_MODIFY
		if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
			ms_NAKEnable(ep->bEndpointAddress);
#endif
		if(idx==2)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x126));
		if(idx==4)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x146));
		if(idx==6)
			ep_csr1 = udc_read8(MSB250X_USBCREG(0x166));

		DBG("ep%01d rd csr:%02x\n", idx, ep_csr1);

		if (ep_csr1 & MSB250X_UDC_RXCSR1_SENTSTALL)
		{
			DBG("rx st\n");
			if(idx==2)
				udc_write8(ep_csr1 & ~MSB250X_UDC_RXCSR1_SENTSTALL, MSB250X_USBCREG(0x126));
			if(idx==4)
				udc_write8(ep_csr1 & ~MSB250X_UDC_RXCSR1_SENTSTALL, MSB250X_USBCREG(0x146));
			if(idx==6)
				udc_write8(ep_csr1 & ~MSB250X_UDC_RXCSR1_SENTSTALL, MSB250X_USBCREG(0x166));

			return;
		}
#ifdef RX_modify_mode1
#ifdef CONFIG_USB_MSB250X_DMA
		if(req != NULL)
			RxHandler(ep,req);
		else
		{
#ifdef RX_mode1_log
			printk("req is null\n");
#endif
		}
#endif
#else
		if(req != NULL){
			msb250x_udc_do_request(ep, req);
		}
		else
			printk("req is null\n");
#endif
	}
}

static void wakeup_connection_change_event(struct msb250x_udc *dev)
{
    u16 new_linestate;
    u8 new_soft_conn;

#ifdef UDC_FUNCTION_LOG
	printk("wakeup_connection_change_event\n");
#endif

    mdelay(100);

    new_linestate = (udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;
    new_soft_conn = udc_read8(MSB250X_UDC_PWR_REG)&(MSB250X_UDC_PWR_SOFT_CONN);
    if ((old_linestate != new_linestate) || (old_soft_conn != new_soft_conn))
    {
        /* wake up event queue. */
        dev->conn_chg = 1;
        wake_up_interruptible(&dev->event_q);
        // printk("old linestate 0x%04x new linestate 0x%04x\n", old_linestate, new_linestate);
        // printk("old soft_conn 0x%04x new soft_conn 0x%04x\n", old_soft_conn, new_soft_conn);
    }
    else
    {
        dev->conn_chg = 0;
        // printk("old linestate 0x%04x new linestate 0x%04x\n", old_linestate, new_linestate);
        // printk("old soft_conn 0x%04x new soft_conn 0x%04x\n", old_soft_conn, new_soft_conn);
    }

    old_linestate = new_linestate;
    old_soft_conn = new_soft_conn;

    return;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : USB_ReSet_ClrRXMode1
+------------------------------------------------------------------------------
| DESCRIPTION : Reset DMA control engine
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
|                    |   |   |
+--------------------+---+---+-------------------------------------------------
*/
void USB_ReSet_ClrRXMode1(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("USB_ReSet_ClrRXMode1\n");
#endif
      udc_write16(0, MSB250X_UDC_DMA_MODE_CTL);
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_irq
+------------------------------------------------------------------------------
| DESCRIPTION :the USB interrupt service routine
|
| RETURN      : non-zero when the irq be handled
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| irq                        |x  |      | irq number
|--------------------+---+---+-------------------------------------------------
| _dev                 |x  |      | irq argument
+--------------------+---+---+-------------------------------------------------
*/
static void
reset_gadget(struct msb250x_udc *dev, struct usb_gadget_driver *driver)
{
	int i;
	unsigned long flags;

	/* don't disconnect drivers more than once */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;
	dev->gadget.speed = USB_SPEED_UNKNOWN;

	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < MSB250X_ENDPOINTS; i++) {
		struct msb250x_ep *ep = &dev->ep[i];

		spin_lock_irqsave(&dev->lock, flags);
		msb250x_udc_nuke(dev, ep, -ESHUTDOWN);
		spin_unlock_irqrestore(&dev->lock, flags);
	}

	/* report reset; the driver is already quiesced */
	if (driver)
		usb_gadget_udc_reset(&dev->gadget, driver);
}

static irqreturn_t msb250x_udc_irq(int irq, void *_dev)
{
	struct msb250x_udc *dev = _dev;
	int usb_status = 0;
	int usb_intrx_status = 0, usb_inttx_status = 0;
	int pwr_reg = 0;
	//int ep0csr = 0;
	int i = 0;
	//u32 idx = 0;
	//unsigned long flags,sets;

	spin_lock(&dev->lock);

	/* Read status registers, note also that all active interrupts are cleared
	when this register is read. */

	usb_status = udc_read8(MSB250X_UDC_INTRUSB_REG);
	udc_write8(usb_status, MSB250X_UDC_INTRUSB_REG);

	usb_intrx_status = udc_read16(MSB250X_UDC_INTRRX_REG);
	usb_inttx_status = udc_read16(MSB250X_UDC_INTRTX_REG);
	udc_write16(usb_intrx_status, MSB250X_UDC_INTRRX_REG);
	udc_write16(usb_inttx_status, MSB250X_UDC_INTRTX_REG);

	pwr_reg = udc_read8(MSB250X_UDC_PWR_REG);


	DBG("usbs=%02x, usb_intrxs=%02x, usb_inttxs=%02x pwr=%02x ep0csr=%02x\n",
		usb_status, usb_intrx_status, usb_inttx_status, pwr_reg, ep0csr);

#ifdef CONFIG_USB_MSB250X_DMA
{
	u8 dma_intr;

	dma_intr = udc_read8(M_REG_DMA_INTR);
	udc_write8(dma_intr, M_REG_DMA_INTR);
	if (dma_intr)
	{
		for (i = 0; i < MAX_USB_DMA_CHANNEL; i++)
		{
			if (dma_intr & (1 << i))
			{
				u8 ch;

				ch = i + 1;
				USB_DMA_IRQ_Handler(ch, dev);
			}
		}
	}
}
#endif /* CONFIG_USB_MSB250X_DMA */
	/*
	 * Now, handle interrupts. There's two types :
	 * - Reset, Resume, Suspend coming -> usb_int_reg
	 * - EP -> ep_int_reg
	 */

	/* RESET */
	if (usb_status & MSB250X_UDC_INTRUSB_RESET)
	{
		printk("#######======>>>>>hello_bus_reset\n");
		UTMI_REG_WRITE8(0x58,0x10); //TX-current adjust to 105%=> bit <4> set 1
		UTMI_REG_WRITE8(0x5A,0x02); // Pre-emphasis enable=> bit <1> set 1
		UTMI_REG_WRITE8(0x5E,0x01);	//HS_TX common mode current enable (100mV)=> bit <7> set 1

		if (dev->driver)
		{
			spin_unlock(&dev->lock);
			reset_gadget(dev, dev->driver);
			spin_lock(&dev->lock);
		}

		// clear function addr
		udc_write8(0, MSB250X_UDC_FADDR_REG);

		dev->address = 0;
		dev->ep0state = EP0_IDLE;
		if (udc_read8(MSB250X_UDC_PWR_REG)&MSB250X_UDC_PWR_HS_MODE)
		{
			dev->gadget.speed = USB_SPEED_HIGH;
			UTMI_REG_WRITE16(0x58, 0x0230); //B2 analog parameter
		}
		else
		{
			dev->gadget.speed = USB_SPEED_FULL;
			UTMI_REG_WRITE16(0x58, 0x0030); //B2 analog parameter
		}

#if defined(CONFIG_USB_MSB250X_DMA)
		if(using_dma && dev->DmaRxMode == DMA_RX_MODE1)
		{
			DBG("DMA RX Mode1 ReSetting \n");
			*(DMA_CNTL_REGISTER(MAX_USB_DMA_CHANNEL))=USB_Read_DMA_Control(MAX_USB_DMA_CHANNEL)&0xfe;
			USB_ReSet_ClrRXMode1();
			udc_write8((udc_read8(MSB250X_UDC_USB_CFG6_H)|0x20), MSB250X_UDC_USB_CFG6_H);//short_mode

			//clear all autonak setting
			udc_write16((udc_read16(MSB250X_UDC_EP_BULKOUT)&0xfff0), MSB250X_UDC_EP_BULKOUT);
			udc_write8((udc_read8(MSB250X_UDC_DMA_MODE_CTL1)&0xf0), MSB250X_UDC_DMA_MODE_CTL1);
		}
#endif
		//wakeup_connection_change_event(dev);
		msb250x_udc_kick_intr_bh();
	}

    /* RESUME */
	if (usb_status & MSB250X_UDC_INTRUSB_RESUME)
	{
		if (dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->resume)
			dev->driver->resume(&dev->gadget);

		wakeup_connection_change_event(dev);
	}

	/* SUSPEND */
	if (usb_status & MSB250X_UDC_INTRUSB_SUSPEND)
	{
		for(i=1;i<MSB250X_ENDPOINTS;i++)
		{
			if(dev->ep[i].bEndpointAddress & USB_DIR_IN)
			{
				int ep_idx = (dev->ep[i].bEndpointAddress & 0x0f)*0x10+0x100;
				dev->ep[i].wMaxPacketSize = udc_read16(MSB250X_USBCREG(ep_idx));
			}
			else
			{
				int ep_idx = (dev->ep[i].bEndpointAddress & 0x0f)*0x10+0x104;
				dev->ep[i].wMaxPacketSize = udc_read16(MSB250X_USBCREG(ep_idx));
			}
		}

		if (dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->suspend) {
			DBG("call gadget->suspend\n");
			dev->driver->suspend(&dev->gadget);
		}
		dev->ep0state = EP0_IDLE;

		wakeup_connection_change_event(dev);
	}

	/* EP */
	/* control traffic */
	/* check on ep0csr != 0 is not a good idea as clearing in_pkt_ready
	 * generate an interrupt
	 */
	if (usb_inttx_status & MSB250X_UDC_INTRTX_EP0)
	{
		//printk("USB ep0 irq\n");
		msb250x_udc_handle_ep0(dev);
	}
	/* endpoint data transfers */
	for (i = 1; i < MSB250X_ENDPOINTS; i++)
	{
		u32 tmp = 1 << i;
#if defined(TIMER_PATCH)
		if ((usb_inttx_status & tmp) || (usb_intrx_status & tmp) || (dev->ep[i].sw_ep_irq))
			msb250x_udc_handle_ep(&dev->ep[i]);
#else
		if ((usb_inttx_status & tmp) || (usb_intrx_status & tmp))
			msb250x_udc_handle_ep(&dev->ep[i]);
#endif
	}

	DBG("irq: %d msb250x_udc_done %x.\n", irq, usb_intrx_status);

	spin_unlock(&dev->lock);//spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

/* --------------------- container_of ops ----------------------------------*/
static inline struct msb250x_ep *to_msb250x_ep(struct usb_ep *ep)
{
    return container_of(ep, struct msb250x_ep, ep);
}

static inline struct msb250x_udc *to_msb250x_udc(struct usb_gadget *gadget)
{
    return container_of(gadget, struct msb250x_udc, gadget);
}

static inline struct msb250x_request *to_msb250x_req(struct usb_request *req)
{
    return container_of(req, struct msb250x_request, req);
}


/*------------------------- msb250x_ep_ops ----------------------------------*/

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_ep_enable
+------------------------------------------------------------------------------
| DESCRIPTION : configure endpoint, making it usable
|
| RETURN      : zero, or a negative error code.
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                 |x  |       | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| desc                 |x  |       | usb_endpoint_descriptor struct point
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_ep_enable(struct usb_ep *_ep,
                 const struct usb_endpoint_descriptor *desc)
{
	struct msb250x_udc *dev = NULL;
	struct msb250x_ep *ep = NULL;
	u32 max = 0, tmp = 0;
	u32 csr1 = 0 ,csr2 = 0;
	u32 int_en_reg = 0;
	unsigned long flags;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_ep_enable\n");
#endif

	ep = to_msb250x_ep(_ep);

	if (!_ep || !desc || ep->desc
			|| _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max = le16_to_cpu(desc->wMaxPacketSize) & 0x1fff;

	spin_lock_irqsave(&ep->dev->lock,flags);//local_irq_save (flags);
	_ep->maxpacket = max & 0x7ff;
	ep->desc = desc;
	ep->halted = 0;
	ep->bEndpointAddress = desc->bEndpointAddress;
	ep->bmAttributes = desc->bmAttributes;
	ep->wMaxPacketSize = desc->wMaxPacketSize;

	/* set type, direction, address; reset fifo counters */
	if (desc->bEndpointAddress & USB_DIR_IN)
	{
		csr1 = MSB250X_UDC_TXCSR1_FLUSHFIFO|MSB250X_UDC_TXCSR1_CLRDATAOTG;
		csr2 = MSB250X_UDC_TXCSR2_MODE;

		if((_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC)
		{
			csr2 |= MSB250X_UDC_TXCSR2_ISOC;
		}

		if(ep->num==1)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x110));
			udc_write8(csr1, MSB250X_USBCREG(0x112));
			udc_write8(csr2, (MSB250X_USBCREG(0x112))+1);
		}
		else if(ep->num==3)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x130));
			udc_write8(csr1, MSB250X_USBCREG(0x132));
			udc_write8(csr2, (MSB250X_USBCREG(0x132))+1);
		}
		else if(ep->num==5)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x150));
			udc_write8(csr1, MSB250X_USBCREG(0x152));
			udc_write8(csr2, (MSB250X_USBCREG(0x152))+1);
		}
		else if(ep->num==7)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x170));
			udc_write8(csr1, MSB250X_USBCREG(0x172));
			udc_write8(csr2, (MSB250X_USBCREG(0x172))+1);
		}

		/* enable irqs */
		int_en_reg = udc_read16(MSB250X_UDC_INTRTXE_REG);
		udc_write16(int_en_reg | (1 << ep->num), MSB250X_UDC_INTRTXE_REG);
	}
	else
	{
		/* enable the enpoint direction as Rx */
		csr1 = MSB250X_UDC_RXCSR1_FLUSHFIFO | MSB250X_UDC_RXCSR1_CLRDATATOG;
		csr2 = 0;

		if((_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC)
		{
			csr2 |= MSB250X_UDC_RXCSR2_ISOC;
		}
		else
		{
			ms_autonak_set(ep->bEndpointAddress);
		}

		if(ep->num==2)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x124));
			udc_write8(csr1, MSB250X_USBCREG(0x126));
			udc_write8(csr2, (MSB250X_USBCREG(0x126))+1);

			//if ep is isoc mode, disable AUTONAK
			if(using_dma && (dev->DmaRxMode == DMA_RX_MODE1) && (ep->bmAttributes & USB_ENDPOINT_XFER_ISOC))
			{
				udc_write16(0, MSB250X_UDC_EP_BULKOUT);
				udc_write16(udc_read16(MSB250X_UDC_DMA_MODE_CTL)&~(M_Mode1_P_NAK_Enable), MSB250X_UDC_DMA_MODE_CTL);
			}
		}
		if(ep->num==4)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x144));
			udc_write8(csr1, MSB250X_USBCREG(0x146));
			udc_write8(csr2, (MSB250X_USBCREG(0x146))+1);
		}
		if(ep->num==6)
		{
			udc_write16(_ep->maxpacket , MSB250X_USBCREG(0x164));
			udc_write8(csr1, MSB250X_USBCREG(0x166));
			udc_write8(csr2, (MSB250X_USBCREG(0x166))+1);
		}

		/* enable irqs */
		int_en_reg = udc_read16(MSB250X_UDC_INTRRXE_REG);
		udc_write16(int_en_reg | (1 << ep->num), MSB250X_UDC_INTRRXE_REG);
	}

	/* print some debug message */
	tmp = desc->bEndpointAddress;
	//printk ( "enable %s(%d) ep%x%s-blk max %02x\n",
	//         _ep->name,ep->num, tmp,
	//        desc->bEndpointAddress & USB_DIR_IN ? "in" : "out", max);

	spin_unlock_irqrestore(&ep->dev->lock,flags);//local_irq_restore (flags);

	msb250x_udc_schedule_done(ep);

    return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_ep_disable
+------------------------------------------------------------------------------
| DESCRIPTION : endpoint is no longer usable
|
| RETURN      : zero, or a negative error code.
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                 |x  |       | usb_ep struct point
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_ep_disable(struct usb_ep *_ep)
{
	struct msb250x_ep *ep = to_msb250x_ep(_ep);
	u32 int_en_reg = 0;
	unsigned long flags;
	struct msb250x_udc	*dev;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_ep_disable\n");
#endif

	if (!_ep || !ep->desc)
	{
		DBG("%s not enabled\n",_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}
	dev = ep->dev;
	DBG("Entered %s\n", __FUNCTION__);
	DBG("ep_disable: %s\n", _ep->name);

	ep->desc = NULL;
	ep->halted = 1;

	Release_DMA_Channel(ep->ch);
#if defined(TIMER_PATCH)
	ms_stop_timer(ep);
#endif
	spin_lock_irqsave(&dev->lock,flags);
	msb250x_udc_nuke(ep->dev, ep, -ESHUTDOWN);
	spin_unlock_irqrestore(&dev->lock,flags);

	if(((ep->bEndpointAddress & 0x80) == 0) && (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
	{
		ms_autonak_clear(ep->bEndpointAddress);
	}

	/* disable irqs */
	if(ep->bEndpointAddress & USB_DIR_IN)
	{
		int_en_reg = udc_read16(MSB250X_UDC_INTRTXE_REG);
		udc_write16(int_en_reg & ~(1<<ep->num), MSB250X_UDC_INTRTXE_REG);
	}
	else
	{
		int_en_reg = udc_read16(MSB250X_UDC_INTRRXE_REG);
		udc_write16(int_en_reg & ~(1<<ep->num), MSB250X_UDC_INTRRXE_REG);
	}

	DBG("%s disabled\n", _ep->name);
	return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_alloc_request
+------------------------------------------------------------------------------
| DESCRIPTION : allocate a request object to use with this endpoint
|
| RETURN      : the request, or null if one could not be allocated.
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                 |x  |       | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| mem_flags          |x  |       | GFP_* flags to use
+--------------------+---+---+-------------------------------------------------
*/
static struct usb_request *
msb250x_udc_alloc_request(struct usb_ep *_ep, gfp_t mem_flags)
{
    struct msb250x_request *req = NULL;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_alloc_request\n");
#endif

    DBG("Entered %s(%p,%d)\n", __FUNCTION__, _ep, mem_flags);

    if (!_ep)
        return NULL;

    req = kzalloc (sizeof(struct msb250x_request), mem_flags);
    if (!req)
        return NULL;

    INIT_LIST_HEAD (&req->queue);
    return &req->req;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_free_request
+------------------------------------------------------------------------------
| DESCRIPTION : frees a request object
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                 |x  |       | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| _req                 |x  |       |usb_request struct point
+--------------------+---+---+-------------------------------------------------
*/
static void
msb250x_udc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
    struct msb250x_ep *ep = to_msb250x_ep(_ep);
    struct msb250x_request    *req = to_msb250x_req(_req);

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_free_request\n");
#endif

    DBG("Entered %s(%p,%p)\n", __FUNCTION__, _ep, _req);

    if (!ep || !_req || (!ep->desc && _ep->name != ep0name))
        return;

    WARN_ON (!list_empty (&req->queue));
    kfree(req);
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_queue
+------------------------------------------------------------------------------
| DESCRIPTION : queues (submits) an I/O request to an endpoint
|
| RETURN      : zero, or a negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                 |x  |       | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| _req                 |x  |       |usb_request struct point
|--------------------+---+---+-------------------------------------------------
| gfp_flags          |x  |       | GFP_* flags to use
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_queue(struct usb_ep *_ep, struct usb_request *_req,
        gfp_t gfp_flags)
{
    struct msb250x_request    *req = to_msb250x_req(_req);
    struct msb250x_ep *ep = to_msb250x_ep(_ep);
    struct msb250x_udc     *dev = NULL;
    unsigned long        flags;

    DBG("Entered %s\n", __FUNCTION__);

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_queue\n");
#endif

    if (unlikely (!_ep || (!ep->desc && ep->ep.name != ep0name)))
    {
        DBG("%s: invalid args\n", __FUNCTION__);
        return -EINVAL;
    }

    dev = ep->dev;
    if (unlikely (!dev->driver
            || dev->gadget.speed == USB_SPEED_UNKNOWN))
    {
        return -ESHUTDOWN;
    }

     spin_lock_irqsave(&dev->lock,flags);//local_irq_save (flags);

    if (unlikely(!_req || !_req->complete
            || !_req->buf || !list_empty(&req->queue)))
    {
        if (!_req)
            DBG("%s: 1 X X X\n", __FUNCTION__);
        else
        {
            DBG("%s: 0 %01d %01d %01d\n",
                        __FUNCTION__, !_req->complete,!_req->buf,
                        !list_empty(&req->queue));
        }

        spin_unlock_irqrestore(&dev->lock,flags);//local_irq_restore(flags);
        return -EINVAL;
    }

    _req->status = -EINPROGRESS;
    _req->actual = 0;

    DBG("%s: ep%x len %d\n",
                     __FUNCTION__, ep->bEndpointAddress, _req->length);
#ifdef	RX_mode1_log
	printk("@@@QUEUE ep%x len %x buf:%p\n",
					 ep->bEndpointAddress, _req->length,_req->buf);
#endif

	//putb("ep len buf",ep->bEndpointAddress, _req->length);
	//tmp_buff=req->req.buf;

	if(list_empty(&ep->queue))
	{	//putb("recv ep len",ep->bEndpointAddress, _req->length);
		req = msb250x_udc_do_request(ep, req);
	}
	 /* pio or dma irq handler will advance the queue. */
	if (req != NULL){
		 list_add_tail(&req->queue, &ep->queue);
#ifdef TX_log
	if(ep->bEndpointAddress==2)
		 printk("[USB]add_QUEUE:%x\n",ep->bEndpointAddress);
#endif
		//putb("add queue ep,len",ep->bEndpointAddress, _req->length);
	}

    spin_unlock_irqrestore(&dev->lock,flags);//local_irq_restore(flags);

    DBG("%s ok and dev->ep0state=%d \n", __FUNCTION__, dev->ep0state);

    return 0;

}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_dequeue
+------------------------------------------------------------------------------
| DESCRIPTION : dequeues (cancels, unlinks) an I/O request from an endpoint
|
| RETURN      : zero, or a negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                |x  |   | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| _req               |x  |   | usb_request struct point
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
    struct msb250x_ep *ep = to_msb250x_ep(_ep);
    //struct msb250x_udc  *udc = NULL;
    struct msb250x_udc	*dev;
    struct msb250x_request    *req = NULL;
    int retval = -EINVAL;
    unsigned long flags;

    DBG("Entered %s(%p,%p)\n", __FUNCTION__, _ep, _req);
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_dequeue\n");
#endif

	dev = ep->dev;

    if (!sg_udc_controller->driver)
        return -ESHUTDOWN;

    if(!_ep || !_req)
        return retval;

    // udc = to_msb250x_udc(ep->gadget);

    spin_lock_irqsave(&dev->lock,flags);//local_irq_save (flags);

    list_for_each_entry (req, &ep->queue, queue)
    {
        if (&req->req == _req)
        {
            list_del_init (&req->queue);
            _req->status = -ECONNRESET;
            retval = 0;
            break;
        }
    }

    if (retval == 0)
    {
        DBG("dequeued req %p from %s, len %d buf %p\n",
                    req, _ep->name, _req->length, _req->buf);

        msb250x_udc_done(ep, req, -ECONNRESET);
    }

    spin_unlock_irqrestore(&dev->lock,flags);//local_irq_restore (flags);
    return retval;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_halt
+------------------------------------------------------------------------------
| DESCRIPTION : sets the endpoint halt feature.
|
| RETURN      : zero, or a negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _ep                 |x  |       | usb_ep struct point
|--------------------+---+---+-------------------------------------------------
| value                 |x  |       |set halt or not
+--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_set_halt(struct usb_ep *_ep, int value)
{
    struct msb250x_ep *ep = to_msb250x_ep(_ep);
    u32 ep_csr = 0;
    u32 idx = 0;
    //unsigned long flags;
    struct msb250x_request    *preq = NULL;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_halt\n");
#endif

    if (unlikely (!_ep || (!ep->desc && ep->ep.name != ep0name)))
    {
        DBG("%s: inval 2\n", __FUNCTION__);
        return -EINVAL;
    }

    if ((!list_empty(&ep->queue)))
        preq = list_entry(ep->queue.next, struct msb250x_request, queue);
    else
        preq = NULL;

    DBG("Entered %s\n", __FUNCTION__);

    //spin_lock_irqsave(&ep->dev->lock,flags);//local_irq_save (flags);
    idx = ep->bEndpointAddress & 0x7F;

    ep->halted = value ? 1 : 0;

    if (idx == 0)
    {
        msb250x_udc_set_ep0_ss();
        /*msb250x_udc_set_ep0_de_out();*/
    }
    else
    {
#if 0
        udc_write8(idx, MSB250X_UDC_INDEX_REG);
        ep_csr = udc_read8((ep->bEndpointAddress & USB_DIR_IN)
                              ? MSB250X_UDC_TXCSR1_REG
                              : MSB250X_UDC_RXCSR1_REG);
#endif
		if(idx==1)
			ep_csr = udc_read8(MSB250X_USBCREG(0x112));
		else if(idx==2)
			ep_csr = udc_read8(MSB250X_USBCREG(0x126));
		else if(idx==3)
			ep_csr = udc_read8(MSB250X_USBCREG(0x132));
		else if(idx==4)
			ep_csr = udc_read8(MSB250X_USBCREG(0x146));
		else if(idx==5)
			ep_csr = udc_read8(MSB250X_USBCREG(0x152));
		else if(idx==6)
			ep_csr = udc_read8(MSB250X_USBCREG(0x166));
		else if(idx==7)
			ep_csr = udc_read8(MSB250X_USBCREG(0x172));

		if ((ep->bEndpointAddress & USB_DIR_IN) != 0)
		{
			if (value)
			{
				if (ep_csr & MSB250X_UDC_TXCSR1_FIFONOEMPTY)
				{
					DBG("%s fifo busy, cannot halt\n", _ep->name);
					ep->halted = 0;
					//spin_unlock_irqrestore(&ep->dev->lock,flags);//local_irq_restore (flags);
					return -EAGAIN;
				}
				DBG("%s stall\n", _ep->name);

				if(idx==1)
					udc_write8(ep_csr | MSB250X_UDC_TXCSR1_SENDSTALL,MSB250X_USBCREG(0x112));
				else if(idx==3)
					udc_write8(ep_csr | MSB250X_UDC_TXCSR1_SENDSTALL,MSB250X_USBCREG(0x132));
				else if(idx==5)
					udc_write8(ep_csr | MSB250X_UDC_TXCSR1_SENDSTALL,MSB250X_USBCREG(0x152));
				else if(idx==7)
					udc_write8(ep_csr | MSB250X_UDC_TXCSR1_SENDSTALL,MSB250X_USBCREG(0x172));
			}
			else
			{
				//printk("[USB]clear stall\n");
				if(idx==1)
				{
					udc_write8(MSB250X_UDC_TXCSR1_CLRDATAOTG, MSB250X_USBCREG(0x112));
					udc_write8(0,MSB250X_USBCREG(0x112));
				}
				else if(idx==3)
				{
					udc_write8(MSB250X_UDC_TXCSR1_CLRDATAOTG, MSB250X_USBCREG(0x132));
					udc_write8(0,MSB250X_USBCREG(0x132));
				}
				else if(idx==5)
				{
                	udc_write8(MSB250X_UDC_TXCSR1_CLRDATAOTG, MSB250X_USBCREG(0x152));
					udc_write8(0,MSB250X_USBCREG(0x152));
				}
				else if(idx==7)
				{
                	udc_write8(MSB250X_UDC_TXCSR1_CLRDATAOTG, MSB250X_USBCREG(0x172));
					udc_write8(0,MSB250X_USBCREG(0x172));
				}

				if (!(ep_csr & MSB250X_UDC_TXCSR1_TXPKTRDY) && preq)
				{
					msb250x_udc_write_fifo(ep, preq);
				}
			}
		}
		else /* out token */
		{
			if (value)
			{
				if(idx==2)
					udc_write8(ep_csr | MSB250X_UDC_RXCSR1_SENDSTALL,MSB250X_USBCREG(0x126));
                if(idx==4)
					udc_write8(ep_csr | MSB250X_UDC_RXCSR1_SENDSTALL,MSB250X_USBCREG(0x146));
                if(idx==6)
					udc_write8(ep_csr | MSB250X_UDC_RXCSR1_SENDSTALL,MSB250X_USBCREG(0x166));
			}
			else
			{
				if(idx==2)
				{
					ep_csr &= ~MSB250X_UDC_RXCSR1_SENDSTALL;
					udc_write8(ep_csr, MSB250X_USBCREG(0x126));
					ep_csr |= MSB250X_UDC_RXCSR1_CLRDATATOG;
					udc_write8(ep_csr, MSB250X_USBCREG(0x126));
				}
				if(idx==4)
				{
					ep_csr &= ~MSB250X_UDC_RXCSR1_SENDSTALL;
					udc_write8(ep_csr, MSB250X_USBCREG(0x146));
					ep_csr |= MSB250X_UDC_RXCSR1_CLRDATATOG;
					udc_write8(ep_csr, MSB250X_USBCREG(0x146));
				}
				if(idx==6)
				{
					ep_csr &= ~MSB250X_UDC_RXCSR1_SENDSTALL;
					udc_write8(ep_csr, MSB250X_USBCREG(0x166));
					ep_csr |= MSB250X_UDC_RXCSR1_CLRDATATOG;
					udc_write8(ep_csr, MSB250X_USBCREG(0x166));
				}

				if ((ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY) && preq)
				{
					msb250x_udc_read_fifo(ep, preq);
				}
			}
		}
	}

    //spin_unlock_irqrestore(&ep->dev->lock,flags);//local_irq_restore (flags);

    return 0;
}

/* endpoint-specific parts of the api to the usb controller hardware */
static const struct usb_ep_ops sg_msb250x_ep_ops =
{
    .enable = msb250x_udc_ep_enable,
    .disable = msb250x_udc_ep_disable,

    .alloc_request = msb250x_udc_alloc_request,
    .free_request = msb250x_udc_free_request,

    .queue = msb250x_udc_queue,
    .dequeue = msb250x_udc_dequeue,

    .set_halt = msb250x_udc_set_halt,
};

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_pullup_i
+------------------------------------------------------------------------------
| DESCRIPTION : internal software connection function
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| is_on                 |x  |       | enable the software connection or not
|--------------------+---+---+-------------------------------------------------
*/
static void msb250x_udc_pullup_i(int is_on)
{
	u32 pwr_reg = 0;

	printk("[UDC]pullup_i:0x%d\n",is_on);

	pwr_reg = udc_read8(MSB250X_UDC_PWR_REG);

	if (is_on) {
		//reset UTMI
		UTMI_REG_WRITE8(0x06*2, UTMI_REG_READ8(0x06*2) | BIT0 | BIT1);
		//clear reset UTMI
		UTMI_REG_WRITE8(0x06*2, UTMI_REG_READ8(0x06*2) & ~(BIT1|BIT0));
		//dp pull up
		udc_write8((pwr_reg |= MSB250X_UDC_PWR_SOFT_CONN), MSB250X_UDC_PWR_REG);
	} else
		udc_write8((pwr_reg &= ~MSB250X_UDC_PWR_SOFT_CONN), MSB250X_UDC_PWR_REG);
}

/*------------------------- usb_gadget_ops ----------------------------------*/

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_get_frame
+------------------------------------------------------------------------------
| DESCRIPTION : get frame count
|
| RETURN      : the current frame number
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _gadget          |x  |       | usb_gadget struct point
|--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_get_frame(struct usb_gadget *_gadget)
{
    int tmp = 0;
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_get_frame\n");
#endif

    DBG("Entered %s\n", __FUNCTION__);

    tmp = udc_read16(MSB250X_UDC_FRAME_L_REG);
    return tmp;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_wakeup
+------------------------------------------------------------------------------
| DESCRIPTION : tries to wake up the host connected to this gadget
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _gadget          |x  |       | usb_gadget struct point
|--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_wakeup(struct usb_gadget *_gadget)
{
    DBG("Entered %s\n", __FUNCTION__);
    return 0;
}


/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_set_selfpowered
+------------------------------------------------------------------------------
| DESCRIPTION : sets the device selfpowered feature
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| gadget                 |x  |       | usb_gadget struct point
|--------------------+---+---+-------------------------------------------------
| value                 |x  |       | set this feature or not
|--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_set_selfpowered(struct usb_gadget *gadget, int value)
{
    struct msb250x_udc *udc = to_msb250x_udc(gadget);

    DBG("Entered %s\n", __FUNCTION__);
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_set_selfpowered\n");
#endif

    if (value)
        udc->devstatus |= (1 << USB_DEVICE_SELF_POWERED);
    else
        udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);

    return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_pullup
+------------------------------------------------------------------------------
| DESCRIPTION : software-controlled connect to USB host
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| gadget                 |x  |       | usb_gadget struct point
|--------------------+---+---+-------------------------------------------------
| is_on                 |x  |       | set software-controlled connect to USB host or not
|--------------------+---+---+-------------------------------------------------
*/
void do_soft_connect(void);
static int msb250x_udc_pullup(struct usb_gadget *gadget, int is_on)
{
    struct msb250x_udc *udc = to_msb250x_udc(gadget);

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_pullup\n");
#endif

    //DBG("Entered %s\n", __FUNCTION__);
    //printk("#####====>msb250x_udc_pullup\n");
    //do_soft_connect();

    if (udc->driver && udc->driver->disconnect)
        udc->driver->disconnect(&udc->gadget);

    msb250x_udc_pullup_i(is_on);

    return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_vbus_session
+------------------------------------------------------------------------------
| DESCRIPTION : establish the USB session
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| gadget                 |x  |       | usb_gadget struct point
|--------------------+---+---+-------------------------------------------------
| is_active          |x  |       | establish the session or not
|--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_vbus_session(struct usb_gadget *gadget, int is_active)
{
    DBG("Entered %s\n", __FUNCTION__);
    return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_vbus_draw
+------------------------------------------------------------------------------
| DESCRIPTION : constrain controller's VBUS power usage
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| _gadget          |x  |       | usb_gadget struct point
|--------------------+---+---+-------------------------------------------------
| ma                 |x  |       | milliAmperes
|--------------------+---+---+-------------------------------------------------
*/
static int msb250x_vbus_draw(struct usb_gadget *_gadget, unsigned ma)
{
    DBG("Entered %s\n", __FUNCTION__);
    return 0;
}

static int msb250x_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct msb250x_udc *udc = sg_udc_controller;

	printk("msb250x_udc--4.9v\n");


	if (!g)
	{
		printk("[USB]ENODEV\n");
		return -ENODEV;
	}

	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;

	old_linestate = 0;
	old_soft_conn = 0;
	udc->conn_chg = 0;
	//init_waitqueue_head(&(udc->event_q));
#ifdef CONFIG_USB_MSB250X_DMA
	udc->DmaRxMode = DMA_RX_MODE1;
	using_dma = 1;
#endif
#if !defined( CONFIG_USB_CHARGER_DETECT )
	//msb250x_udc_enable(udc);
#endif
	 mdelay(1);

#if !defined( CONFIG_USB_CHARGER_DETECT )
	old_linestate = (udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;
	old_soft_conn = udc_read8(MSB250X_UDC_PWR_REG)&(MSB250X_UDC_PWR_SOFT_CONN);
#endif
	printk("end probe_driver\n");

	return 0;
}

static int msb250x_udc_stop(struct usb_gadget *g)
{
	struct msb250x_udc *dev = to_msb250x_udc(g);
	unsigned i = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->driver = NULL;

	for (i = 0; i < MSB250X_ENDPOINTS; i++){
		msb250x_udc_nuke (dev, &dev->ep[i], -ESHUTDOWN);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}


/* the rest of the api to the controller hardware: device operations,
 * which don't involve endpoints (or i/o).
 */
static const struct usb_gadget_ops sg_msb250x_gadget_ops =
{
	.get_frame = msb250x_udc_get_frame,
	.wakeup = msb250x_udc_wakeup,
	.set_selfpowered = msb250x_udc_set_selfpowered,
	.pullup = msb250x_udc_pullup,
	.vbus_session = msb250x_udc_vbus_session,
	.vbus_draw    = msb250x_vbus_draw,
	.udc_start = msb250x_udc_start,
	//.match_ep	= msb250x_udc_match_ep,
	.udc_stop = msb250x_udc_stop,
};

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_disable
+------------------------------------------------------------------------------
| DESCRIPTION : disable udc
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| dev                 |x  |       | msb250x_udc struct point
|--------------------+---+---+-------------------------------------------------
*/
void msb250x_udc_disable(struct msb250x_udc *dev)
{
#ifndef CB2
    u32 tmp=0;
#endif
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_disable\n");
#endif
    DBG("Entered %s\n", __FUNCTION__);
    /* Disable all interrupts */
    udc_write8(0x00, MSB250X_UDC_INTRUSBE_REG);
    udc_write16(0x00, MSB250X_UDC_INTRTXE_REG);
    udc_write16(0x00, MSB250X_UDC_INTRRXE_REG);

    /* Clear the interrupt registers */
    /* All active interrupts will be cleared when this register is read */
#ifdef CB2
	udc_write8(udc_read8(MSB250X_UDC_INTRUSB_REG), MSB250X_UDC_INTRUSB_REG);
	udc_write16(udc_read16(MSB250X_UDC_INTRTX_REG), MSB250X_UDC_INTRTX_REG);
	udc_write16(udc_read16(MSB250X_UDC_INTRRX_REG), MSB250X_UDC_INTRRX_REG);
#else
	tmp = udc_read8(MSB250X_UDC_INTRUSB_REG);
    tmp = udc_read16(MSB250X_UDC_INTRTX_REG);
    tmp = udc_read16(MSB250X_UDC_INTRRX_REG);
#endif
    /* Good bye, cruel world */
    msb250x_udc_pullup_i(0);

#if !defined(QC_BOARD)
    /* USB device reset, write 0 to reset OTG IP, active low*/
    //udc_write16(0, (OTG0_BASE_ADDR + 0x100));
    //udc_write16(1, (OTG0_BASE_ADDR + 0x100));
#endif

    /* Set speed to unknown */
    dev->gadget.speed = USB_SPEED_UNKNOWN;

#ifdef CONFIG_USB_MSB250X_DMA
	//DmaRxMode1=0;
	free_dma_channels=0x7f;
#endif
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_reinit
+------------------------------------------------------------------------------
| DESCRIPTION : reinit the ep list
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| dev                 |x  |       | msb250x_udc struct point
|--------------------+---+---+-------------------------------------------------
*/
static void msb250x_udc_reinit(struct msb250x_udc *dev)
{
    u32 i = 0;

    DBG("Entered %s\n", __FUNCTION__);

    /* device/ep0 records init */
    INIT_LIST_HEAD (&dev->gadget.ep_list);
    INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);

	dev->gadget.ep0 = &dev->ep [0].ep;
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	//dev->ep0state = EP0_DISCONNECT;
	//dev->irqs = 0;
	//dev->ep0state = EP0_IDLE;
	printk("[UDC]meet ep\n");

    for (i = 0; i < MSB250X_ENDPOINTS; i++)
    {
        struct msb250x_ep *ep = &dev->ep[i];

		ep->num = i;
		//ep->ep.name = ep_name[i];
		//ep->reg_fifo = &dev->regs->ep_fifo [i];
		//ep->reg_status = &dev->regs->ep_status [i];
		//ep->reg_mode = &dev->regs->ep_mode[i];

		//ep->ep.ops = &sg_msb250x_ep_ops;

		list_add_tail (&ep->ep.ep_list, &dev->gadget.ep_list);
		//ep->dev = dev;
		INIT_LIST_HEAD (&ep->queue);

		usb_ep_set_maxpacket_limit(&ep->ep, 1024);

		if (i == 0)
			ep->ep.caps.type_control = true;
		else
		{
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_int = true;
		}
		if(dev->ep[i].bEndpointAddress & 0x80)
		{
			ep->ep.caps.dir_in = true;
			ep->ep.caps.dir_out = false;
		}
		else
		{
			ep->ep.caps.dir_in = false;
			ep->ep.caps.dir_out = true;
		}
		//ep->ep.caps.dir_in = true;
		//ep->ep.caps.dir_out = true;
    }

	usb_ep_set_maxpacket_limit(&dev->ep[0].ep, 64);
	list_del_init (&dev->ep[0].ep.ep_list);
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_enable
+------------------------------------------------------------------------------
| DESCRIPTION : enable udc
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| dev                 |x  |       | msb250x_udc struct point
|--------------------+---+---+-------------------------------------------------
*/
USB_INFO_st pg_USBInfo;
void InitUSBVar(USB_INFO_st *pUsbInfo)
{
    unsigned char u8EpNum = 0;

#ifdef UDC_FUNCTION_LOG
	printk("InitUSBVar\n");
#endif

    if (NULL == pUsbInfo)
        return;

    pUsbInfo->otgTestMode = 0;
    pUsbInfo->otgSetFaddr = 0;
    pUsbInfo->otgClearedEP = 0;
    pUsbInfo->otgGetStatusResponse = 0;
    pUsbInfo->otgMyDeviceHNPSupport = 1;
    pUsbInfo->free_dma_channels = 0x7f;
    pUsbInfo->otgCSW_Addr = 0;
    pUsbInfo->otgSetupCmdBuf_Addr = 0;
    pUsbInfo->otgCBWCB_Addr = 0;
    pUsbInfo->otgRegPower = 0;
    pUsbInfo->otgIntStatus = 0;
    pUsbInfo->otgDMARXIntStatus = 0;
    pUsbInfo->otgDMATXIntStatus = 0;
    pUsbInfo->otgDataPhaseDir = 0;
    pUsbInfo->otgMassCmdRevFlag = 0;
    pUsbInfo->otgMassRxDataReceived = 0;
    pUsbInfo->otgReqOTGState = 0;
    pUsbInfo->otgCurOTGState = 0;
    pUsbInfo->otgSuspended = 0;
    pUsbInfo->otgRemoteWakeup = 0;
    pUsbInfo->otgHNPEnabled = 0;
    pUsbInfo->otgHNPSupport = 0;
    pUsbInfo->otgSelfPower = 1;
    pUsbInfo->otgConfig = 0;
    pUsbInfo->otgInterface = 0;
    pUsbInfo->otgUSBState = 0;
    pUsbInfo->otgcid = 0;
    pUsbInfo->otgFaddr = 0;
    pUsbInfo->otgRegDevCtl = 0;
    pUsbInfo->otgSpeed = 0;
    pUsbInfo->otgResetComplete = 0;
    pUsbInfo->otgSOF_1msCount = 0;
    pUsbInfo->otgIsNonOSmodeEnable = 0;
    pUsbInfo->otgUDPAddress = 0;
    pUsbInfo->otgUDPTxPacketCount = 0;
    pUsbInfo->otgUDPRxPacketCount = 0;
    pUsbInfo->bDownloadCode = 0;
    pUsbInfo->u8USBDeviceMode = 0;
    pUsbInfo->DeviceConnect = 0;
    pUsbInfo->u8USBDevMode = 0;
    pUsbInfo->gu16UplinkStart = 0;
    pUsbInfo->gu32UplinkSize = 0;
    pUsbInfo->otgSelectROMRAM = 0;
    pUsbInfo->PPB_One_CB= 0;
    pUsbInfo->PPB_Two_CB= 0;
    pUsbInfo->UploadResume = 1;
    pUsbInfo->gu16BBErrorCode = 0;
    pUsbInfo->nTransferLength=0;
    pUsbInfo->NonOS_UsbDeviceDataBuf_CB=0;
    pUsbInfo->bHIF_GetUplinkDataStatus=0;
    pUsbInfo->SizeofUSB_Msdfn_Dscr=0;
    pUsbInfo->otgEP0Setup.bmRequestType = 0;
    pUsbInfo->otgEP0Setup.bRequest = 0;
    pUsbInfo->otgEP0Setup.wValue = 0;
    pUsbInfo->otgEP0Setup.wIndex = 0;
    pUsbInfo->otgEP0Setup.wLength = 0;
    pUsbInfo->otgFSenseKey= 0;
    pUsbInfo->otgFASC= 0;
    pUsbInfo->otgFASCQ= 0;
    pUsbInfo->otgfun_residue= 0;
    pUsbInfo->otgactualXfer_len= 0;
    pUsbInfo->otgdataXfer_dir= 0;
    pUsbInfo->USB_CREATEPORT_COUNTER= 0;
    pUsbInfo->USB_PB_CONNECTED= 0;


    for (u8EpNum = 0; u8EpNum<3; u8EpNum++)
    {
        pUsbInfo->otgUSB_EP[u8EpNum].FIFOSize = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].MaxEPSize = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].BytesRequested = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].BytesProcessed = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].DRCInterval = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].intr_flag = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].pipe = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].BltEP = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].DRCDir = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].LastPacket = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].IOState = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].Halted = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].transfer_buffer = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].transfer_buffer_length = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].FifoRemain = 0;
        pUsbInfo->otgUSB_EP[u8EpNum].Infnum = 0;
    }
}

void do_soft_connect(void)
{
#ifdef UDC_FUNCTION_LOG
	printk("do_soft_connect\n");
#endif
    USB_REG_WRITE8(M_REG_POWER, (USB_REG_READ8(M_REG_POWER) | M_POWER_SOFTCONN));
}

#if defined(ENABLE_OTG_USB_NEW_MIU_SLE)
void MIU_select_setting_udc(uintptr_t USBC_base)
{
	printk("[UDC] config miu select [%x] [%x] [%x] ][%x]\n", USB_MIU_SEL0, USB_MIU_SEL1, USB_MIU_SEL2, USB_MIU_SEL3);
	writeb(USB_MIU_SEL0, (void*)(USBC_base+0x14*2));	//Setting MIU0 segment
	writeb(USB_MIU_SEL1, (void*)(USBC_base+0x16*2));	//Setting MIU1 segment
	writeb(USB_MIU_SEL2, (void*)(USBC_base+0x17*2-1));	//Setting MIU2 segment
	writeb(USB_MIU_SEL3, (void*)(USBC_base+0x18*2));	//Setting MIU3 segment
	writeb(readb((void*)(USBC_base+0x19*2-1)) | 0x1, (void*)(USBC_base+0x19*2-1));	//Enable miu partition mechanism
#if  0//defined(DISABLE_MIU_LOW_BOUND_ADDR_SUBTRACT_ECO)
	printk("[USB] enable miu lower bound address subtraction\n");
	writeb(readb((void*)(USBC_base+0x0F*2-1)) | 0x1, (void*)(USBC_base+0x0F*2-1));
#endif
}
#endif

void msb250x_udc_enable(void)
{
	printk("+USBInit\r\n");
	InitUSBVar(&pg_USBInfo);

#if defined(ENABLE_OTG_USB_NEW_MIU_SLE)
	MIU_select_setting_udc(USBC_BASE_ADDR);
#endif

	pg_USBInfo.u8DeviceClass = E_Class_Serial;
	pg_USBInfo.u8DeviceCap = HIGH_SPEED;
	pg_USBInfo.u8USBDevMode = E_USB_VirtCOM;

#if !defined(CONFIG_USB_MS_OTG)
	// Enable OTG controller
	USBC_REG_WRITE8(0x02*2, (USBC_REG_READ8(0x02*2)& ~(OTG_BIT0|OTG_BIT1)) | (OTG_BIT1));
#endif

#if defined(ENABLE_SQUELCH_LEVEL)
	/* Init UTMI squelch level setting befor CA */
	if(UTMI_DISCON_LEVEL_2A & (0x08|0x04|0x02|0x01))
	{
		writeb((UTMI_DISCON_LEVEL_2A & (0x08|0x04|0x02|0x01)), (void*)(UTMI_BASE_ADDR+0x2a*2));
		printk("[UDC] init squelch level 0x%x\n", readb((void*)(UTMI_BASE_ADDR+0x2a*2)));
	}
#endif

	UTMI_REG_WRITE8(0x3C*2, UTMI_REG_READ8(0x3C*2) | 0x1); // set CA_START as 1
	mdelay(10);
	UTMI_REG_WRITE8(0x3C*2, UTMI_REG_READ8(0x3C*2) & ~0x01); // release CA_START
	while ((UTMI_REG_READ8(0x3C*2) & 0x02) == 0);        // polling bit <1> (CA_END)

	// Reset OTG controllers
	USBC_REG_WRITE8(0, USBC_REG_READ8(0)|(OTG_BIT3|OTG_BIT2));

	// Unlock Register R/W functions  (RST_CTRL[6] = 1)
	// Enter suspend  (RST_CTRL[3] = 1)
	//USBC_REG_WRITE8(0, 0x48);
	USBC_REG_WRITE8(0, (USBC_REG_READ8(0)&~(OTG_BIT2))|OTG_BIT6);

	printk("+UTMI\n");
	UTMI_REG_WRITE8(0x06*2, (UTMI_REG_READ8(0x06*2) & 0x9F) | 0x40); //reg_tx_force_hs_current_enable
	UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) | 0x28); //Disconnect window select
	UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) & 0xef); //Disconnect window select
	UTMI_REG_WRITE8(0x07*2-1, UTMI_REG_READ8(0x07*2-1) & 0xfd); //Disable improved CDR
	UTMI_REG_WRITE8(0x09*2-1, UTMI_REG_READ8(0x09*2-1) |0x81);  // UTMI RX anti-dead-loc, ISI effect improvement
	UTMI_REG_WRITE8(0x15*2-1, UTMI_REG_READ8(0x15*2-1) |0x20);  // Chirp signal source select
	UTMI_REG_WRITE8(0x0b*2-1, UTMI_REG_READ8(0x0b*2-1) |0x80);  // set reg_ck_inv_reserved[6] to solve timing problem
	UTMI_REG_WRITE8(0x2c*2,   UTMI_REG_READ8(0x2c*2) |0x98);
	UTMI_REG_WRITE8(0x2d*2-1, UTMI_REG_READ8(0x2d*2-1) |0x02);
	UTMI_REG_WRITE8(0x2e*2,   UTMI_REG_READ8(0x2e*2) |0x10);
	UTMI_REG_WRITE8(0x2f*2-1, UTMI_REG_READ8(0x2f*2-1) |0x01);

	printk("-UTMI\n");

#if !defined(CONFIG_USB_MS_OTG)
	// 2'b10: OTG enable
	USBC_REG_WRITE8(0x02*2, (USBC_REG_READ8(0x02*2)& ~(OTG_BIT0|OTG_BIT1)) | (OTG_BIT1));
#endif
	USB_REG_WRITE8(0x100, USB_REG_READ8(0x100)&0xFE); // Reset OTG
	USB_REG_WRITE8(0x100, USB_REG_READ8(0x100)|0x01);

	//endpoint arbiter setting
	udc_write8(udc_read8(MSB250X_UDC_USB_CFG7_L) & ~0x1, MSB250X_UDC_USB_CFG7_L);
	udc_write16(udc_read16(MSB250X_UDC_USB_CFG6_L) | 0x8000, MSB250X_UDC_USB_CFG6_L);
	udc_write16(udc_read16(MSB250X_UDC_USB_CFG6_L) & ~0x1100, MSB250X_UDC_USB_CFG6_L);
	udc_write16(udc_read16(MSB250X_UDC_USB_CFG6_L) | 0x4000, MSB250X_UDC_USB_CFG6_L);

	// Set FAddr to 0
	USB_REG_WRITE8(M_REG_FADDR, 0);
	// Set Index to 0
	USB_REG_WRITE8(M_REG_INDEX, 0);
	USB_REG_WRITE8(M_REG_CFG6_H, USB_REG_READ8(M_REG_CFG6_H) | 0x08);
	USB_REG_WRITE8(M_REG_CFG6_H, USB_REG_READ8(M_REG_CFG6_H) | 0x40);

	printk("HIGH SPEED\n");
	USB_REG_WRITE8(M_REG_POWER, (USB_REG_READ8(M_REG_POWER) & ~M_POWER_ENSUSPEND) | M_POWER_HSENAB);

	USB_REG_WRITE8(M_REG_DEVCTL,0);

	// Flush the next packet to be transmitted/ read from the endpoint 0 FIFO
	USB_REG_WRITE16(M_REG_CSR0, USB_REG_READ16(M_REG_CSR0) | M_CSR0_FLUSHFIFO);

	// Flush the latest packet from the endpoint Tx FIFO
	USB_REG_WRITE8(M_REG_INDEX, 1);
	USB_REG_WRITE16(M_REG_TXCSR, USB_REG_READ16(M_REG_TXCSR) | M_TXCSR_FLUSHFIFO);

	// Flush the next packet to be read from the endpoint Rx FIFO
	USB_REG_WRITE8(M_REG_INDEX, 2);
	USB_REG_WRITE16(M_REG_RXCSR, USB_REG_READ16(M_REG_RXCSR) | M_RXCSR_FLUSHFIFO);

	USB_REG_WRITE8(M_REG_INDEX, 0);

	// Clear all control/status registers
	USB_REG_WRITE16(M_REG_CSR0, 0);
	USB_REG_WRITE8(M_REG_INDEX, 1);
	USB_REG_WRITE16(M_REG_TXCSR, 0);
	USB_REG_WRITE8(M_REG_INDEX, 2);
	USB_REG_WRITE16(M_REG_RXCSR, 0);

	USB_REG_WRITE8(M_REG_INDEX, 0);

	// Enable all endpoint interrupts
	USB_REG_WRITE8(M_REG_INTRUSBE, 0xf7);
	USB_REG_WRITE16(M_REG_INTRTXE, 0xff);
	USB_REG_WRITE16(M_REG_INTRRXE, 0xff);
	USB_REG_READ8(M_REG_INTRUSB);
	USB_REG_READ16(M_REG_INTRTX);
	USB_REG_READ16(M_REG_INTRRX);
}


void usb_probe_driver(struct usb_gadget_driver *driver)
{
	struct msb250x_udc *udc = sg_udc_controller;

	printk("msb250x_udc--4.9v\n");

    udc->driver = driver;
    udc->gadget.dev.driver = &driver->driver;

	old_linestate = 0;
	old_soft_conn = 0;
	udc->conn_chg = 0;
	//init_waitqueue_head(&(udc->event_q));
#ifdef CONFIG_USB_MSB250X_DMA
	udc->DmaRxMode = DMA_RX_MODE1;
	using_dma = 1;
#endif
#if !defined( CONFIG_USB_CHARGER_DETECT )
	//msb250x_udc_enable(udc);
#endif
	 mdelay(1);

#if !defined( CONFIG_USB_CHARGER_DETECT )
	old_linestate = (udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;
	old_soft_conn = udc_read8(MSB250X_UDC_PWR_REG)&(MSB250X_UDC_PWR_SOFT_CONN);
#endif
	printk("end probe_driver\n");

}
EXPORT_SYMBOL(usb_probe_driver);

void usb_unprobe_driver(void)
{
	struct msb250x_udc *udc = sg_udc_controller;

#ifdef CB2
	/* Disable udc */
	msb250x_udc_disable(udc);
#endif
}
EXPORT_SYMBOL(usb_unprobe_driver);

/*------------------------- gadget driver handling---------------------------*/
/*
+------------------------------------------------------------------------------
| FUNCTION    : usb_gadget_register_driver
+------------------------------------------------------------------------------
| DESCRIPTION : register a gadget driver
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| driver                 |x  |       | usb_gadget_driver struct point
|--------------------+---+---+-------------------------------------------------
*/
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
    struct msb250x_udc *udc = sg_udc_controller;
    int        retval = 0;

#ifdef UDC_FUNCTION_LOG
	printk("usb_gadget_register_driver\n");
#endif

    DBG("Entered %s driver.name=%s \n", __FUNCTION__, driver->driver.name);

    /* Sanity checks */
    if (!udc)
        return -ENODEV;

    if (udc->driver)
        return -EBUSY;

    //if (!driver->bind || !driver->setup
    //{
    //    DBG("Invalid driver: bind %p setup %p speed %d\n",
    //                    driver->bind, driver->setup, driver->speed);
    //    return -EINVAL;
    //}

#if defined(MODULE)
    if (!driver->unbind)
    {
        DBG("Invalid driver: no unbind method\n");
        return -EINVAL;
    }
#endif

    /* Hook the driver */
    udc->driver = driver;
    udc->gadget.dev.driver = &driver->driver;

    /* Bind the driver */
    if ((retval = device_add(&udc->gadget.dev)) != 0)
    {
        DBG("Error in device_add() : %d\n",retval);
        goto register_error;
    }

    DBG("binding gadget driver '%s'\n", driver->driver.name);

    //if ((retval = driver->bind (&udc->gadget)) != 0)
    //{
    //    device_del(&udc->gadget.dev);
    //    goto register_error;
    //}

    /* init event queue. */
    old_linestate = 0;
    old_soft_conn = 0;
    udc->conn_chg = 0;
    init_waitqueue_head(&(udc->event_q));
#ifdef CB2
#ifdef CONFIG_USB_MSB250X_DMA
		udc->DmaRxMode = msb250_udc_set_dma_rx_by_name( driver->driver.name);
		using_dma = 1;
#endif
#endif
    /* Enable udc */
    //msb250x_udc_enable(udc);

    mdelay(1);

    old_linestate = (udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;
    old_soft_conn = udc_read8(MSB250X_UDC_PWR_REG)&(MSB250X_UDC_PWR_SOFT_CONN);


    return 0;

register_error:
    udc->driver = NULL;
    udc->gadget.dev.driver = NULL;
    return retval;

	return 0;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : usb_gadget_unregister_driver
+------------------------------------------------------------------------------
| DESCRIPTION : unregister a gadget driver
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| driver                 |x  |       | usb_gadget_driver struct point
|--------------------+---+---+-------------------------------------------------
*/

/*---------------------platform_driver  msb250x_udc_driver -------------------------------*/

/* set the gadget and endpoint parameter */
static struct msb250x_udc sg_udc_config =
{
    .gadget =
    {
        .ops = &sg_msb250x_gadget_ops,
        .ep0 = &sg_udc_config.ep[0].ep,
        .name = sg_gadget_name,
        .dev =
        {
            //.bus_id = "gadget",
            .init_name = "gadget",
            //.release	= nop_release,
        },
    },

    /* control endpoint */
    .ep[0] =
    {
        .num = 0,
        .ep =
        {
            .name = ep0name,
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = EP0_FIFO_SIZE,
        },
        .dev = &sg_udc_config,
    },

    /* first group of endpoints */
    .ep[1] =
    {
        .num = 1,
        .ep =
        {
            .name = "ep1in-bulk",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev= &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_IN | 1,
        .bmAttributes    = USB_ENDPOINT_XFER_BULK,
    },
    .ep[2] =
    {
        .num = 2,
        .ep =
        {
            .name = "ep2out-bulk",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev = &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_OUT | 2,
        .bmAttributes    = USB_ENDPOINT_XFER_BULK,
    },
    .ep[3] =
    {
        .num = 3,
        .ep =
        {
            .name = "ep3in-int",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev = &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_IN | 3,
        .bmAttributes    = USB_ENDPOINT_XFER_INT,
    },
    .ep[4] =
    {
        .num = 4,
        .ep =
        {
            .name = "ep4out-bulk",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev = &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_OUT | 4,
        .bmAttributes    = USB_ENDPOINT_XFER_BULK,
    },
    .ep[5] =
    {
        .num = 5,
        .ep =
        {
            .name = "ep5in-bulk",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev= &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_IN | 5,
        .bmAttributes    = USB_ENDPOINT_XFER_BULK,
    },
    .ep[6] =
    {
        .num = 6,
        .ep =
        {
            .name = "ep6out-bulk",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev = &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_OUT | 6,
        .bmAttributes    = USB_ENDPOINT_XFER_BULK,
    },
    .ep[7] =
    {
        .num = 7,
        .ep =
        {
            .name = "ep7in-bulk",
            .ops = &sg_msb250x_ep_ops,
            .maxpacket = 512,
        },
        .dev= &sg_udc_config,
        .fifo_size    = 512,
        .bEndpointAddress = USB_DIR_IN | 7,
        .bmAttributes    = USB_ENDPOINT_XFER_BULK,
    },

    .got_irq = 0,
};

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_utmi_init
+------------------------------------------------------------------------------
| DESCRIPTION : initial the UTMI interface
|
| RETURN      : NULL
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
*/

static void msb250x_udc_intr_bh(struct work_struct* work)
{
    struct msb250x_udc *udc = &sg_udc_config;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_intr_bh\n");
#endif
    wakeup_connection_change_event(udc);
}

/*
 * Open and close
 */

int msb250x_udc_open(struct inode *inode, struct file *filp)
{
    struct msb250x_udc *dev; /* device information */

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_open\n");
#endif

    dev = container_of(inode->i_cdev, struct msb250x_udc, cdev);
    filp->private_data = dev; /* for other methods */

    if (!dev->driver)
    {
        printk(KERN_ERR "Driver not registered yet!\n");

        return -EFAULT;
    }
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    DBG("msb250x_udc opened.\n");

    up(&dev->sem);

    return 0;          /* success */
}

int msb250x_udc_release(struct inode *inode, struct file *filp)
{
    DBG("release msb250x udc.\n");

    return 0;
}


/*
 * The ioctl() implementation
 */

int msb250x_udc_ioctl(struct inode *inode, struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    int err = 0;
    struct msb250x_udc *dev = (struct msb250x_udc *)filp->private_data;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_ioctl\n");
#endif

    /*
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if (_IOC_TYPE(cmd) != MSB250X_UDC_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > MSB250X_UDC_IOC_MAXNR)
        return -ENOTTY;

    /*
     * the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user-oriented, while
     * access_ok is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    if (_IOC_DIR(cmd) & _IOC_READ)
            err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
            err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    switch(cmd)
    {
        case MSB250X_UDC_CONN_CHG:
        {
            int conn;
            u8 linestate;
            u8 soft_conn;

            if (wait_event_interruptible(dev->event_q, (dev->conn_chg != 0)))
                 return -ERESTARTSYS; /* signal: tell the fs layer to handle it */

            dev->conn_chg = 0;

            linestate = (udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;
            soft_conn = udc_read8(MSB250X_UDC_PWR_REG)&(MSB250X_UDC_PWR_SOFT_CONN);

            if (linestate) /* if line state is not zero, cable is not connected. */
            {
                conn = 0;
            }
            else
            {
                if (soft_conn) /* if cable is connected and soft_conn is enable, */
                {              /* then device is connected. */
                    conn = 1;
                }
                else /* if cable is connected, but soft_conn is disable, */
                {    /* then device is also considered disconnected. */
                    conn = 0;
                }
            }

            retval = __put_user(conn, (int __user *)arg);

            break;
        }

        case MSB250X_UDC_SET_CONN:
        {
            int set_conn;
            u8 tmp;

            if (down_interruptible(&dev->sem))
                return -ERESTARTSYS;

            retval = __get_user(set_conn, (int __user *)arg);
            if (retval == 0)
            {
                tmp = udc_read8(MSB250X_UDC_PWR_REG);

                if (set_conn)
                {
                    tmp |= MSB250X_UDC_PWR_SOFT_CONN;
                    udc_write8(tmp, MSB250X_UDC_PWR_REG);
                }
                else
                {
                    tmp &= (~(MSB250X_UDC_PWR_SOFT_CONN));
                    udc_write8(tmp, MSB250X_UDC_PWR_REG);
                }
            }

            up(&dev->sem);

            break;
        }

        case MSB250X_UDC_GET_LINESTAT:
        {
            int linestate;

            if (down_interruptible(&dev->sem))
                return -ERESTARTSYS;

            linestate = (int)(udc_read8(UTMI_SIGNAL_STATUS)>>6) & 0x3;

            retval = __put_user(linestate, (int __user *)arg);

            up(&dev->sem);
            break;
        }

        case MSB250X_UDC_GET_CONN:
        {
            int soft_conn;

            if (down_interruptible(&dev->sem))
                return -ERESTARTSYS;

            soft_conn = (int)(udc_read8(MSB250X_UDC_PWR_REG)>>6) & 0x1;

            retval = __put_user(soft_conn, (int __user *)arg);

            up(&dev->sem);
            break;
        }

        default:  /* redundant, as cmd was checked against MAXNR */

            return -ENOTTY;
    }

    return retval;

}

struct file_operations msb250x_udc_fops = {
    .owner =    THIS_MODULE,
    //.ioctl =    msb250x_udc_ioctl,
    .open =     msb250x_udc_open,
    .release =  msb250x_udc_release,
};

/*
 * Set up the char_dev structure for this device.
 */
static int msb250x_udc_setup_cdev(struct msb250x_udc *dev)
{
    int result = 0;
    dev_t devno;
    int i;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_setup_cdev\n");
#endif

    /*
     * Get a range of minor numbers to work with, asking for a dynamic
     * major unless directed otherwise at load time.
     */
    if (msb250x_udc_major)
    {
        devno = MKDEV(msb250x_udc_major, msb250x_udc_minor);
        result = register_chrdev_region(devno, MSB250X_UDC_NR_DEVS, "msb250x_udc");
    } else {
        result = alloc_chrdev_region(&devno, msb250x_udc_minor, MSB250X_UDC_NR_DEVS,
                "msb250x_udc");
        msb250x_udc_major = MAJOR(devno);
    }

    if (result < 0)
    {
        printk(KERN_WARNING "scull: can't get major %d\n", msb250x_udc_major);
        return result;
    }

    init_MUTEX(&dev->sem);

    cdev_init(&dev->cdev, &msb250x_udc_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &msb250x_udc_fops;
    result = cdev_add (&dev->cdev, devno, 1);
    /* Fail gracefully if need be */
    if (result)
        printk(KERN_NOTICE "Error %d adding msb250x_udc0", result);

    msb250x_udc_class = class_create(THIS_MODULE, "msb250x_udc");
    if (IS_ERR(msb250x_udc_class))
    {
        cdev_del(&dev->cdev);
        /* cleanup_module is never called if registering failed */
        unregister_chrdev_region(devno, MSB250X_UDC_NR_DEVS);

        return PTR_ERR(msb250x_udc_class);
    }

    for (i=0;i<MSB250X_UDC_NR_DEVS;i++)
    {
        device_create(msb250x_udc_class, NULL, devno,
                      NULL, "msb250x_udc%d", i);
    }

    return result;
}


/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_probe
+------------------------------------------------------------------------------
| DESCRIPTION : The generic driver interface function which called for initial udc
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| pdev                 |x  |       | platform_device struct point
|--------------------+---+---+-------------------------------------------------
*/
#if defined(CONFIG_OF)
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);
#endif

#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
static struct str_waitfor_dev waitfor;
#if defined(CONFIG_OF)
static struct dev_pm_ops mstar_udc_pm_ops;
#else
static struct dev_pm_ops msb250x_udc_pm_ops;
#endif
static int msb250x_udc_suspend_wrap(struct device *dev);
static int msb250x_udc_resume_wrap(struct device *dev);
#endif

static int msb250x_udc_probe(struct platform_device *pdev)
{
	struct msb250x_udc *udc = &sg_udc_config;
	int retval = 0;
	int ret,irq=-1;

#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_probe\n");
#endif

	DBG("Entered %s\n", __FUNCTION__);
	spin_lock_init (&udc->lock);

	INIT_WORK(&usb_bh, msb250x_udc_intr_bh);

	/* since event_q is used in irq, we need to init it before request_irq() */
	init_waitqueue_head(&(udc->event_q));
	//device_initialize(&udc->gadget.dev);
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;
	udc->pdev = pdev;
	udc->enabled = 0;
	udc->gadget.max_speed = USB_SPEED_HIGH;

	platform_set_drvdata(pdev, udc);

	msb250x_udc_disable(udc);

	msb250x_udc_reinit(udc);

	udc->active_suspend = 0;

#ifdef CONFIG_USB_MSB250X_DMA
	using_dma = 1;
#endif /* CONFIG_USB_MSB250X_DMA */

	/* Setup char device */
	retval = msb250x_udc_setup_cdev(udc);

	/* irq setup after old hardware state is cleaned up */
	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
#if !defined(ENABLE_IRQ_REMAP)
	if(irq <= 0)
	{
		printk(KERN_ERR "[UDC] can not get irq for %s\n", pdev->name);
		return -ENODEV;
	}
#endif
#ifdef ENABLE_IRQ_REMAP
	irq = MSTAR_UDC_IRQ;
#endif
#if defined(TIMER_PATCH)
	udc->remap_irq=irq;
#endif
	retval = request_irq(irq/*INT_MS_OTG*/, msb250x_udc_irq,
                                                0, sg_gadget_name, udc);
	if (retval != 0)
	{
		printk("cannot get irq, err %d\n", retval);
		return -EBUSY;
	}
	else
		printk(KERN_INFO "[USB] %s irq --> %d\n", pdev->name, irq);

	udc->got_irq = 1;

#ifdef ANDROID_WAKELOCK
	printk(KERN_INFO "init usb_connect_lock\n");
	wake_lock_init(&usb_connect_wake_lock, WAKE_LOCK_SUSPEND, "usb_connect_lock");
#endif

	sg_udc_controller = udc;


	ret = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if(ret)
	{
		printk("error probe\n");
		while(1);
	}


	msb250x_udc_enable();

	if (udc_read8(MSB250X_UDC_PWR_REG)&MSB250X_UDC_PWR_HS_MODE)
	{
		udc->gadget.speed = USB_SPEED_HIGH;
	}
	else
	{
		udc->gadget.speed = USB_SPEED_FULL;
	}

#if defined(TIMER_PATCH)
	ms_init_timer();
#endif

#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
#if defined(CONFIG_OF)
	of_mstar_str("Mstar-udc", &pdev->dev, &mstar_udc_pm_ops, &waitfor,
			&msb250x_udc_suspend_wrap, &msb250x_udc_resume_wrap,
			NULL, NULL);
#else
	of_mstar_str(sg_gadget_name, &pdev->dev, &msb250x_udc_pm_ops, &waitfor,
			&msb250x_udc_suspend_wrap, &msb250x_udc_resume_wrap,
			NULL, NULL);
#endif /* CONFIG_OF */
#endif /* CONFIG_MP_MSTAR_STR_OF_ORDER */

	printk("end porbe\n");
	return retval;
}

/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_remove
+------------------------------------------------------------------------------
| DESCRIPTION : The generic driver interface function which called for disable udc
|
| RETURN      : zero on success, else negative error code
+------------------------------------------------------------------------------
| Variable Name      |IN |OUT|                   Usage
|--------------------+---+---+-------------------------------------------------
| pdev                 |x  |       | platform_device struct point
|--------------------+---+---+-------------------------------------------------
*/
static int msb250x_udc_remove(struct platform_device *pdev)
{
    struct msb250x_udc *udc = platform_get_drvdata(pdev);
    dev_t devno = MKDEV(msb250x_udc_major, msb250x_udc_minor);
    int i, irq;

    DBG("Entered %s\n", __FUNCTION__);
#ifdef UDC_FUNCTION_LOG
	printk("msb250x_udc_remove\n");
#endif

    if (udc->driver)
        return -EBUSY;

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
    if(udc->got_irq)
    {
    	//DBG("free irq: %d \n", udc->irq);
        free_irq(irq , udc);
        udc->got_irq = 0;
    }

    platform_set_drvdata(pdev, NULL);

    DBG("%s: remove ok\n", __FUNCTION__);

    cdev_del(&udc->cdev);
    /* cleanup_module is never called if registering failed */
    unregister_chrdev_region(devno, MSB250X_UDC_NR_DEVS);
    class_destroy(msb250x_udc_class);
    for (i=0;i<MSB250X_UDC_NR_DEVS;i++)
    {
        device_destroy(msb250x_udc_class, devno);
    }

    return 0;
}

#ifdef CONFIG_PM
static int msb250x_udc_suspend(struct platform_device *pdev, pm_message_t message)
{
    struct msb250x_udc *udc = platform_get_drvdata(pdev);

    DBG("Entered %s\n", __FUNCTION__);
#if 1//def UDC_FUNCTION_LOG
	printk("UDC suspend....\n");
#endif

    // disable udc
    msb250x_udc_disable(udc);

    // disable power
    /* USB_REG_WRITE8(M_REG_POWER, 0); */

    // mark suspend state
    udc->active_suspend = 1;

    return 0;
}

int msb250x_udc_resume(struct platform_device *pdev)
{
	struct msb250x_udc *udc = platform_get_drvdata(pdev);

	printk("UDC resume....\n");

	if (udc->active_suspend)
	{
		if (udc->driver)
		{
			// enable udc
			msb250x_udc_enable();
			printk("[UDC]dp+\n");
			do_soft_connect();
			mdelay(1);
		}
		udc->active_suspend = 0;
	}
	return 0;
}

#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
static int msb250x_udc_suspend_wrap(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	if (WARN_ON(!pdev))
		return -ENODEV;

	if (waitfor.stage1_s_wait)
		wait_for_completion(&(waitfor.stage1_s_wait->power.completion));

	return msb250x_udc_suspend(pdev, dev->power.power_state);
}

static int msb250x_udc_resume_wrap(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	if (WARN_ON(!pdev))
		return -ENODEV;

	if (waitfor.stage1_r_wait)
		wait_for_completion(&(waitfor.stage1_r_wait->power.completion));

	return msb250x_udc_resume(pdev);
}
#endif /* CONFIG_MP_MSTAR_STR_OF_ORDER */
#else
#define msb250x_udc_suspend NULL
#define msb250x_udc_resume NULL
#endif


static const struct of_device_id udc_of_match[] = {
	{
		.name = "usb",
		.compatible = "udc-bigendian",
	},
	{
		.name = "usb",
		.compatible = "udc-be",
	},
	{},
};


/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_init
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


#if defined(CONFIG_OF)
static struct of_device_id mstar_udc_of_device_ids[] = {
	{.compatible = "Mstar-udc"},
	{},
};

static struct platform_driver udc_mstar_driver = {
	.probe 		= msb250x_udc_probe,
	.remove 	= msb250x_udc_remove,
#if defined(CONFIG_PM) && !defined(CONFIG_MP_MSTAR_STR_OF_ORDER)
	.suspend	= msb250x_udc_suspend,
	.resume		= msb250x_udc_resume,
#endif
	.driver = {
		.name	= "Mstar-udc",
#if defined(CONFIG_OF)
		.of_match_table = mstar_udc_of_device_ids,
#endif
//		.bus	= &platform_bus_type,
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
		.pm = &mstar_udc_pm_ops,
#endif
	}
};

#define	PLATFORM_DRIVER		udc_mstar_driver
static int __init udc_init(void)
{
	int retval = 0;
	printk("udc_init\n");
	retval = platform_driver_register(&PLATFORM_DRIVER);
	if (retval < 0)
		printk("UDC fail\n");
	return 0;
}
#else
static struct platform_driver msb250x_udc_driver =
{
    .probe    = msb250x_udc_probe,
    .remove = msb250x_udc_remove,
#ifndef CONFIG_MP_MSTAR_STR_OF_ORDER
    .suspend = msb250x_udc_suspend,
    .resume = msb250x_udc_suspend,
#endif
    .driver =
    {
        .name = "msb250x_udc",
        .owner = THIS_MODULE,
        .of_match_table = udc_of_match,
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
        .pm = &msb250x_udc_pm_ops,
#endif
    },
};

#endif


//#ifndef CONFIG_USB_EHCI_HCD
extern void (*ms_udc_probe_usb_driver)(struct usb_gadget_driver *driver);
static int __init msb250x_udc_init(void)
{
	int retval = 0;
    DBG("Entered %s: gadget_name=%s version=%s\n", __FUNCTION__, sg_gadget_name, DRIVER_VERSION);
	printk("MSTAR UDC INIT\n");
#if defined(CONFIG_OF)
	retval= udc_init();
	return retval;
#else
	ms_udc_probe_usb_driver=usb_probe_driver;
	platform_device_register(&ms_udc_device);
    return platform_driver_register(&msb250x_udc_driver);
#endif
}
//#endif
/*
+------------------------------------------------------------------------------
| FUNCTION    : msb250x_udc_exit
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
#if defined(CONFIG_OF)
static void __exit msb250x_udc_exit(void)
{
    DBG("Entered %s \n", __FUNCTION__);
    platform_driver_unregister(&udc_mstar_driver);
}

#else
static void __exit msb250x_udc_exit(void)
{
    DBG("Entered %s \n", __FUNCTION__);
    platform_driver_unregister(&msb250x_udc_driver);
}
#endif

//EXPORT_SYMBOL(usb_gadget_unregister_driver);
EXPORT_SYMBOL(usb_gadget_register_driver);
EXPORT_SYMBOL(msb250x_udc_do_request);
//EXPORT_SYMBOL(msb250x_udc_schedule_done);
EXPORT_SYMBOL(msb250x_udc_done);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep1);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep2);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep3);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep4);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep5);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep6);
EXPORT_SYMBOL(msb250x_udc_fifo_count_ep7);


//#ifndef CONFIG_USB_EHCI_HCD
module_init(msb250x_udc_init);
module_exit(msb250x_udc_exit);
//#endif

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");


//arch_initcall(ms_udc_device_init);
//fs_initcall(msb250x_udc_init); //use fs_initcall due to this should be earlier than ADB module_init
//module_exit(msb250x_udc_exit);

//MODULE_ALIAS(DRIVER_NAME);
//MODULE_LICENSE("GPL");
//MODULE_DESCRIPTION(DRIVER_DESC);


