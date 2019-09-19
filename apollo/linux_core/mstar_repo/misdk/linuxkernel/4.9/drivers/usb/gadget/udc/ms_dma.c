/*------------------------------------------------------------------------------
	Copyright (c) 2009 MStar Semiconductor, Inc.  All rights reserved.
------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
    PROJECT: MSB250x Linux BSP
    DESCRIPTION:
        DMA driver of MSB250x dual role USB device controllers

    HISTORY:
        6/11/2008     Winder Sung    First Created

    NOTE:
        This driver is from other project in MStar Co,.
-------------------------------------------------------------------------------*/

/******************************************************************************
 * Include Files
 ******************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
//#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/cacheflush.h>
#include <linux/jiffies.h>

//#include <kernel.h>

//#include <cache_ops.h>
#if defined( CONFIG_ARM )
#include <mach/irqs.h>
#endif
//#include "hal_timer.h"
#include "ms_dma.h"
#include "ms_config.h"
#include "ms_otg.h"
#include "ms_drc.h"
#include "ms_cpu.h"
#include <asm/outercache.h>
#define HalUtilPHY2MIUAddr(addr)        addr
#define HalUtilMIU2PHYAddr(addr)        addr

#if defined(CONFIG_ARCH_MSW8533X)
#include <hal_drv_util.h>

#define SYSHAL_DCACHE_LINE_SIZE 32

#define HAL_DCACHE_START_ADDRESS(_addr_) \
    (((u32)(_addr_)) & ~(SYSHAL_DCACHE_LINE_SIZE-1))

#define HAL_DCACHE_END_ADDRESS(_addr_, _asize_) \
    (((u32)((_addr_) + (_asize_) + (SYSHAL_DCACHE_LINE_SIZE-1) )) & \
     ~(SYSHAL_DCACHE_LINE_SIZE-1))

void _hal_dcache_flush(void *base , u32 asize)
{
    register u32 _addr_ = HAL_DCACHE_START_ADDRESS((u32)base);
    register u32 _eaddr_ = HAL_DCACHE_END_ADDRESS((u32)(base), asize);

    for( ; _addr_ < _eaddr_; _addr_ += SYSHAL_DCACHE_LINE_SIZE )
        __asm__ __volatile__ ("MCR p15, 0, %0, c7, c14, 1" : : "r" (_addr_));

    /* Drain write buffer */
    _addr_ = 0x00UL;
    __asm__ __volatile__ ("MCR p15, 0, %0, c7, c10, 4" : : "r" (_addr_));
}

#define msb250x_dma_dcache_flush(addr, size) _hal_dcache_flush(addr, size)
#define msb250x_dma_dcache_invalidate(addr, size)
#define msb250x_dma_dcache_flush_invalidate(addr, size) _hal_dcache_flush((void *)addr, size)
#elif defined(CONFIG_MSTAR_MSW8X68) || defined(CONFIG_MSTAR_MSW8X68T)
#ifdef CONFIG_OUTER_CACHE
#include <asm/outercache.h>
#endif
#define HalUtilPHY2MIUAddr(addr)        addr
#define HalUtilMIU2PHYAddr(addr)        addr

#define SYSHAL_DCACHE_LINE_SIZE 32

#define HAL_DCACHE_START_ADDRESS(_addr_) \
    (((u32)(_addr_)) & ~(SYSHAL_DCACHE_LINE_SIZE-1))

#define HAL_DCACHE_END_ADDRESS(_addr_, _asize_) \
    (((u32)((_addr_) + (_asize_) + (SYSHAL_DCACHE_LINE_SIZE-1) )) & \
     ~(SYSHAL_DCACHE_LINE_SIZE-1))

void _hal_dcache_flush(void *base , u32 asize)
{
    register u32 _addr_ = HAL_DCACHE_START_ADDRESS((u32)base);
    register u32 _eaddr_ = HAL_DCACHE_END_ADDRESS((u32)(base), asize);

    for( ; _addr_ < _eaddr_; _addr_ += SYSHAL_DCACHE_LINE_SIZE )
        __asm__ __volatile__ ("MCR p15, 0, %0, c7, c14, 1" : : "r" (_addr_));

    /* Drain write buffer */
    _addr_ = 0x00UL;
    __asm__ __volatile__ ("MCR p15, 0, %0, c7, c10, 4" : : "r" (_addr_));
}

#define msb250x_dma_dcache_flush(addr, size) _hal_dcache_flush(addr, size)
#define msb250x_dma_dcache_invalidate(addr, size)
#ifdef CONFIG_OUTER_CACHE
#define msb250x_dma_dcache_flush_invalidate(addr, size) \
    do{ \
        _hal_dcache_flush((void *)addr, size); \
        outer_flush_range(__pa(addr),__pa(addr) + size); \
        outer_inv_range(__pa(addr),__pa(addr) + size); \
    }while(0)
#else
#define msb250x_dma_dcache_flush_invalidate(addr, size) _hal_dcache_flush((void *)addr, size)
#endif
#else
#define HalUtilPHY2MIUAddr(addr)        addr
#define HalUtilMIU2PHYAddr(addr)        addr

#define msb250x_dma_dcache_flush(addr, size) _dma_cache_wback(addr, size)
#define msb250x_dma_dcache_invalidate(addr, size) _dma_cache_inv(addr, size)
#define msb250x_dma_dcache_flush_invalidate(addr, size) _dma_cache_wback_inv(addr, size)
#endif

#ifdef CONFIG_USB_MSB250X_DMA

#ifdef CONFIG_USB_MSB250X_DEBUG
#define DBG_MSG(x...) printk(KERN_INFO x)
#else
#define DBG_MSG(x...)
#endif

#ifndef SUCCESS
#define SUCCESS 1
#define FAILURE -1
#endif

extern int msb250x_udc_fifo_count(void);
extern int msb250x_udc_fifo_ctl_count(void);
extern int msb250x_udc_fifo_count_ep1(void);
extern int msb250x_udc_fifo_count_ep2(void);
extern int msb250x_udc_fifo_count_ep3(void);
extern int msb250x_udc_fifo_count_ep4(void);
extern int msb250x_udc_fifo_count_ep5(void);
extern int msb250x_udc_fifo_count_ep6(void);
extern int msb250x_udc_fifo_count_ep7(void);
extern void putb(char* string,int a,int b);//,int c,int d,int e,int f,int g,int h,int i,int j,int k,int l,int m);
/* Winder, we should think to reuse this inline functions with msb250x_udc.c */
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
/* --------------------------------------------------------------------------*/

s8 free_dma_channels = 0x7f;

//u8 rx_dma_busy = DMA_NOT_BUSY;
//u8 tx_dma_busy = DMA_NOT_BUSY;

//u8 DmaRxMode1=0;
//u8 DmaTxMode1=0;
static void Set32BitsReg(u32 volatile* Reg,u32 val)
{
    udc_write16((val & 0xffff), (uintptr_t)Reg);
    udc_write16((val>>16), (uintptr_t)(Reg+1));
}

u32 Get32BitsReg(u32 volatile* Reg)
{
     return (udc_read16((uintptr_t)Reg) & 0xffff) | (udc_read16((uintptr_t)(Reg + 1)) << 16);
}

//extern s8 dma_busy;
static inline s8 Get_DMA_Channel(void)
{
    s8 i, bit;
//    unsigned long        flags;

    /* cyg_semaphore_wait(&ChannelDmaSem); */
    for(i = 0, bit = 1; i <  MAX_USB_DMA_CHANNEL; i++, bit <<= 1)
    {
        if(free_dma_channels & bit)
        {
            free_dma_channels &= ~bit;
            DBG_MSG( "Get Channel:%x ",i+1);
            /* cyg_semaphore_post(&ChannelDmaSem); */
			//printk("set DMA busy\n");

		//	local_irq_save (flags);
            //dma_busy = DMA_BUSY;
			//ocal_irq_restore(flags);

			return i+1;
        }
    }

    DBG_MSG( "no channel");
    /* cyg_semaphore_post(&ChannelDmaSem); */
    return -EBUSY;
}
#if 1
void Release_DMA_Channel(s8 channel)
{
	DBG_MSG( "release channel:%x \n",channel);

	if((0 < channel) && (channel<= MAX_USB_DMA_CHANNEL))
		free_dma_channels |= (1 << (channel - 1));
}
#endif

s8 check_dma_busy(void)
{
    s8 i, bit;

    for(i = 0, bit = 1; i <  MAX_USB_DMA_CHANNEL; i++, bit <<= 1)
    {
        if(free_dma_channels & bit)
        {

	   return DMA_NOT_BUSY;
        }
    }
	return DMA_BUSY;

}
//s8 check_tx_dma_busy(void)
//{
//	return tx_dma_busy;
//}
//s8 check_rx_dma_busy(void)
//{
  //  return rx_dma_busy;
//}
u8 *buf=NULL;
u8 *ppp=NULL;
u32 buff_ttt[20000];
int size_ttt;
u32 add_ttt;
int ii=0;
//unsigned long	val=0,val2=0,ms=0;
extern int xx;
extern void Chip_Flush_Memory(void);
extern void Chip_Flush_Cache_Range_VA_PA(unsigned long u32VAddr,unsigned long u32PAddr,unsigned long u32Size);
extern void Chip_Inv_Cache_Range_VA_PA(unsigned long u32VAddr,unsigned long u32PAddr,unsigned long u32Size);
extern void Chip_Inv_Cache_Range(unsigned long u32Addr, unsigned long u32Size);
extern void Chip_Clean_Cache_Range_VA_PA(unsigned long u32VAddr,unsigned long u32PAddr,unsigned long u32Size);
extern void ms_NAKEnable(u8 bEndpointAddress);

#if defined(TIMER_PATCH)
#include <linux/irq.h>
struct timer_list dma_polling_timer[MAX_USB_DMA_CHANNEL];
void ms_init_timer(void)
{
	int i=0;

	for(i=0;i<MAX_USB_DMA_CHANNEL;i++)
		dma_init_timer(&dma_polling_timer[i]);
}

void ms_start_timer(struct msb250x_ep *ep)
{
	ep->wtd_dma_count=0;
	ep->wtd_rx_count=0;
	dma_polling_timer[ep->ch-1].data=(unsigned long)ep;
	dma_polling_timer->expires = jiffies + msecs_to_jiffies(30);
	add_timer(&dma_polling_timer[ep->ch-1]);
}

void ms_stop_timer(struct msb250x_ep *ep)
{
	if((!(ep->bEndpointAddress & USB_DIR_IN)) && ((0 < ep->ch) && (ep->ch <= MAX_USB_DMA_CHANNEL)))
	{
		del_timer(&dma_polling_timer[ep->ch-1]);
		ep->sw_ep_irq=0;
	}
}

static void polling_func(unsigned long data)
{
	struct msb250x_ep *ep;
	u16 ep_idx;
	if(!data) {
		printk("ep structure null\n");
		return;
	}

	ep = (struct msb250x_ep*) data;
	ep_idx=0x108+(0x10*ep->bEndpointAddress);

	//printk("D_count:%d, D_cntl:0x%x, rcsr:0x%x, rco:%d\n", Get32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ep->ch)), Get32BitsReg((u32 volatile*)DMA_CNTL_REGISTER(ep->ch)), udc_read16(MSB250X_USBCREG(0x126)), udc_read16(MSB250X_USBCREG(0x128)));
	if((ep->wtd_dma_count == Get32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ep->ch))) && (ep->wtd_rx_count == udc_read16(MSB250X_USBCREG(ep_idx))) && (ep->wtd_dma_count!=0))
	{
		unsigned long flag;
		ep->sw_ep_irq=1;
		local_irq_save(flag);
		generic_handle_irq(ep->dev->remap_irq);
		local_irq_restore(flag);
	}
	else
	{
		ep->wtd_dma_count = Get32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ep->ch));
		ep->wtd_rx_count = udc_read16(MSB250X_USBCREG(ep_idx));
		mod_timer(&dma_polling_timer[ep->ch-1], jiffies + msecs_to_jiffies(30));
	}

	return;
}

void dma_init_timer(struct timer_list *dma_polling_timer)
{
	printk("[UDC]timer init\n");
	init_timer(dma_polling_timer);
	dma_polling_timer->function = polling_func;
	dma_polling_timer->data = ((unsigned long) 0);
}
#endif


void ms_Ok2Rcv(u8 bEndpointAddress, u16 packnum)
{
	if(bEndpointAddress == udc_read8(MSB250X_UDC_EP_BULKOUT))
	{
		udc_write16((M_Mode1_P_OK2Rcv|M_Mode1_P_NAK_Enable|packnum), MSB250X_UDC_DMA_MODE_CTL);
	}

	if(bEndpointAddress == (udc_read8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f))
	{
		udc_write16((packnum), MSB250X_UDC_USB_CFG1_L);
		udc_write8((M_Mode1_P_OK2Rcv_1|M_Mode1_P_NAK_Enable_1|M_Mode1_P_BulkOut_EP_4), MSB250X_UDC_DMA_MODE_CTL1);
	}
}

#if defined(CONFIG_USB_CONFIGFS_F_FS)
int rx_dma_flag = 0;
#endif
s8 USB_Set_DMA(struct usb_ep *_ep, struct msb250x_request *req, u32 count, u32 mode)
{
	s8 ch;
	u32 /*index,*/ csr2=0;
#ifdef 	RX_modify_mode1
	u16 packnum;
#endif
	u16 control;
	uintptr_t address,pa;
	u32 fiforemain=0;
	uintptr_t new_addr=0;
	s8  idx;
	//u8	*test_buf;
	struct msb250x_ep *ep = to_msb250x_ep(_ep);

	address = (uintptr_t)(req->req.buf) + (uintptr_t)(req->req.actual);
	idx = ep->bEndpointAddress & 0x7F;

#if defined(CONFIG_USB_CONFIGFS_F_FS)
	if(ep->bEndpointAddress & USB_DIR_IN)
	{
		//do not use dma while tx phase
		return -1;
	}
#endif

	DBG_MSG("ep %d count %x  dma req.actual %x \n", idx, count, req->req.actual);
#ifdef	RX_mode1_log
	printk("ep %d count %x dma actual %x \n", idx, count, req->req.actual);
#endif

	ch=Get_DMA_Channel();

	if(ch < 0)       /* no free channel */
	{
		printk( "Get DMA channel fail %d\n", free_dma_channels);
		return -EBUSY;
	}


	/* for multiple Bulk packets, set Mode 1 */
	if (count > _ep->maxpacket)
	{
		mode |= DMA_MODE_ONE;
	}
	else /* mode 0 */
	{
		if(mode & DMA_TX)
		{
			u16 ep_maxpacket;

			ep_maxpacket = _ep->maxpacket;

			count = min((u16)ep_maxpacket, (u16)count);
		}
		else
		{
			if(idx==1)
				fiforemain = msb250x_udc_fifo_count_ep1();
			else if(idx==2)
				fiforemain = msb250x_udc_fifo_count_ep2();
			else if(idx==3)
				fiforemain = msb250x_udc_fifo_count_ep3();
			else if(idx==4)
				fiforemain = msb250x_udc_fifo_count_ep4();
			else if(idx==5)
				fiforemain = msb250x_udc_fifo_count_ep5();
			else if(idx==6)
				fiforemain = msb250x_udc_fifo_count_ep6();
			else if(idx==7)
				fiforemain = msb250x_udc_fifo_count_ep7();

			count = min(fiforemain, count);
		}
		mode &= ~DMA_MODE_ONE;
		DBG_MSG("count less than maxpacket.\n");
	}
	/* flush and invalidate data cache */


	/* prepare DMA control register */
	control = DMA_ENABLE_BIT | mode | (idx << DMA_ENDPOINT_SHIFT) | (DMA_BurstMode<<9);

	size_ttt=count;


	Chip_Flush_Cache_Range_VA_PA(address,HalUtilPHY2MIUAddr(virt_to_phys((void *)address)),count);
	Chip_Clean_Cache_Range_VA_PA(address,__pa(address), count);
	Chip_Inv_Cache_Range(address,count);//Chip_Inv_Cache_Range_VA_PA(address,__pa(address),size_ttt);
	pa = HalUtilPHY2MIUAddr(virt_to_phys((void *)address));

#ifdef DRAM_MORE_THAN_1G_PATCH
	if((HalUtilPHY2MIUAddr(BUS2PA(pa))>>31)==1)
	{
		new_addr=(HalUtilPHY2MIUAddr(BUS2PA(pa))|0x40000000);
		new_addr=new_addr&0x7fffffff;
	}
	else
#endif
	{
		new_addr=HalUtilPHY2MIUAddr(BUS2PA(pa));
	}

	Set32BitsReg((u32 volatile*)DMA_ADDR_REGISTER(ch), (u32)new_addr);
	Set32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ch), count);

	/* program DRC registers */

	switch(mode & DMA_MODE_MASK)
	{
		case DMA_RX | DMA_MODE_ZERO:
			if(idx==2)
				udc_write8(udc_read8((MSB250X_USBCREG(0x126))+1) & ~0x20,(MSB250X_USBCREG(0x126))+1);
			else if(idx==4)
				udc_write8(udc_read8((MSB250X_USBCREG(0x146))+1) & ~0x20,(MSB250X_USBCREG(0x146))+1);
			else if(idx==6)
				udc_write8(udc_read8((MSB250X_USBCREG(0x166))+1) & ~0x20,(MSB250X_USBCREG(0x166))+1); 

			DBG_MSG( "1_ Rx_0 ep: %x, count = %x, Request = %x, control = %x\n",idx,count,req->req.length,control);

			if(idx==2)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x126))+1);
				Enable_RX_EP_Interrupt(idx);
				udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(0x126))+1);
			}
			else if(idx==4)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x146))+1);
				Enable_RX_EP_Interrupt(idx);
				udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(0x146))+1);
			}
			else if(idx==6)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x166))+1);
				Enable_RX_EP_Interrupt(idx);
				udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(0x166))+1);
			}
			break;

		case DMA_TX | DMA_MODE_ZERO:
			DBG_MSG( "2_ Tx_0 ep: %x, buff = %x, count = %x, Request = %x, control = %x\n",idx, address,count,req->req.length,control);
			Enable_TX_EP_Interrupt(idx);
			if(idx==1)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x112))+1);
				udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x112))+1);
			}
			else if(idx==3)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x132))+1);
				udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x132))+1);
			}
			else if(idx==5)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x152))+1);
				udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x152))+1);
			}
			else if(idx==7)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x172))+1);
				udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x172))+1);
			}
			break;

		case DMA_RX | DMA_MODE_ONE:
#if defined(CONFIG_USB_CONFIGFS_F_FS)
			rx_dma_flag=1;
#endif
			if(idx==2)
			{
				udc_write8(udc_read8((MSB250X_USBCREG(0x126))+1) & ~0x20,(MSB250X_USBCREG(0x126))+1);
				csr2 = udc_read8((MSB250X_USBCREG(0x126))+1);
			}
			else if(idx==4)
			{
				udc_write8(udc_read8((MSB250X_USBCREG(0x146))+1) & ~0x20,(MSB250X_USBCREG(0x146))+1);
				csr2 = udc_read8((MSB250X_USBCREG(0x146))+1);
			}
			else if(idx==6)
			{
				udc_write8(udc_read8((MSB250X_USBCREG(0x166))+1) & ~0x20,(MSB250X_USBCREG(0x166))+1);
				csr2 = udc_read8((MSB250X_USBCREG(0x166))+1);
			}

			DBG_MSG( "3_ Rx_1 ep: %x, count = %x, Request = %x, ch = %x\n",idx,count,req->req.length,ch);
			udc_write16(control, (uintptr_t)DMA_CNTL_REGISTER(ch));
			Enable_RX_EP_Interrupt(idx);

			if(idx==2)
				udc_write8((csr2 | RXCSR2_MODE1| MSB250X_UDC_RXCSR2_DISNYET), (MSB250X_USBCREG(0x126))+1);
			else if(idx==4)
				udc_write8((csr2 | RXCSR2_MODE1| MSB250X_UDC_RXCSR2_DISNYET), (MSB250X_USBCREG(0x146))+1);
			else if(idx==6)
				udc_write8((csr2 | RXCSR2_MODE1| MSB250X_UDC_RXCSR2_DISNYET), (MSB250X_USBCREG(0x166))+1);

			DBG_MSG("%s: DMA TX MODE1 Set DMA CTL\n",__FUNCTION__);
#ifdef RX_modify_mode1
			packnum=count/(_ep->maxpacket);
			if (count % _ep->maxpacket)
				packnum+=1;
			if(((ep->bEndpointAddress)&0x0f)==2)
			{
				if (ep->RxShort==1)
				{
					udelay(125);
					ep->RxShort=0;
				}
				if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
				{
					ms_Ok2Rcv(ep->bEndpointAddress, packnum);
				}
			}
			else if(((ep->bEndpointAddress)&0x0f)==4)
			{
				if (ep->RxShort==1)
				{
					udelay(125);
					ep->RxShort=0;
				}
				if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
				{
					ms_Ok2Rcv(ep->bEndpointAddress, packnum);
				}
			}
#endif
#ifdef RX_mode1_log
			printk( "[USB]Set RX1,count = %x, Request = %x, ch = %x\n",count,req->req.length,ch);
#endif
			ep->DmaRxMode1=1;
			ep->ch=ch;
#if defined(TIMER_PATCH)	//sw patch for timer to watch dma status
			ms_start_timer(ep);
#endif
			return SUCCESS;
		case DMA_TX | DMA_MODE_ONE:
			DBG_MSG( "4_ Tx_1 ep: %x, count = %x, Request = %x, control = %x\n",idx,count,req->req.length,control);
#ifdef TX_log
			printk( "DMA Tx_1 count = %x, Request = %x, ch = %x\n",count,req->req.length,ch);
#endif
			Enable_TX_EP_Interrupt(idx);
			if(idx==1)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x112))+1);
				udc_write8((csr2 | TXCSR2_MODE1), (MSB250X_USBCREG(0x112))+1);
			}
			else if(idx==3)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x132))+1);
				udc_write8((csr2 | TXCSR2_MODE1), (MSB250X_USBCREG(0x132))+1);
			}
			else if(idx==5)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x152))+1);
				udc_write8((csr2 | TXCSR2_MODE1), (MSB250X_USBCREG(0x152))+1);
			}
			else if(idx==7)
			{
				csr2 = udc_read8((MSB250X_USBCREG(0x172))+1);
				udc_write8((csr2 | TXCSR2_MODE1), (MSB250X_USBCREG(0x172))+1);
			}
#if 0
if(count>=200)
{
int ii=0;
for(ii=0;ii<=12;ii++)
	printk("%x ",buf[ii]);
printk("\n");
}
#endif
			break;
	}

	udc_write16(control, (uintptr_t)DMA_CNTL_REGISTER(ch));

	return SUCCESS;
}
extern int xx;
//extern int tmp_buff;
int test_start=0;
int ch_test=0;
/*static */extern struct msb250x_request * msb250x_udc_do_request(struct msb250x_ep *ep, struct msb250x_request *req);
void USB_DMA_IRQ_Handler(u8 ch, struct msb250x_udc *dev)
{
	u8  /*index,*/ endpoint, direction;
	u32 csr, mode, bytesleft, bytesdone, control;
	u8  csr2,csr2_tmp=0;
#if defined(TIMER_PATCH)
	u16 ep_idx = 0;
#endif
	u32 lastpacket = 0;
	u32 fiforemain;
	//u32 *prt;
	struct msb250x_ep *ep;
	struct msb250x_request	*req = NULL;
	//unsigned int tick_start,tick_end,timeout;
	uintptr_t address, addr;
	/* get DMA Mode, address and byte counts from DMA registers */
	control = udc_read16((uintptr_t)(DMA_CNTL_REGISTER(ch)));
	mode = control & 0xf;

	addr = (uintptr_t)Get32BitsReg((u32 volatile *)(DMA_ADDR_REGISTER(ch)));
	bytesleft =Get32BitsReg((u32 volatile *)(DMA_COUNT_REGISTER(ch)));
	ch_test=ch;
#ifdef RX_mode1_log
	printk("bytesleft:%x\n",bytesleft);
#endif
	/* get endpoint, URB pointer */
	endpoint = (udc_read16((uintptr_t)(DMA_CNTL_REGISTER(ch))) & 0xf0) >> DMA_ENDPOINT_SHIFT;
	direction = (mode & DMA_TX) ? 0 : 1;

	ep = &dev->ep[endpoint];

	if (likely (!list_empty(&ep->queue)))
		req = list_entry(ep->queue.next, struct msb250x_request, queue);
	else
		req = NULL;

	if (!req)
	{
		printk("no request but DMA done?!\n");
		//printk("flag:%x\n",xx);
		printk("ep %x  left %x \n", endpoint, bytesleft);
		return;
	}

	DBG_MSG("DMA done__ ep %d\n", endpoint);\
	//buf=(u32)(req->req.buf + req->req.actual);
	address = (uintptr_t)(req->req.buf) + (uintptr_t)(req->req.actual);

	Chip_Inv_Cache_Range(address,size_ttt);//Chip_Inv_Cache_Range_VA_PA(address,__pa(address),size_ttt);

#ifdef DRAM_MORE_THAN_1G_PATCH
	if((addr>>30)==1)
	{
		addr=addr-0x40000000;
		addr=addr+0x80000000;
	}
#endif
	addr=PA2BUS(addr);
	bytesdone = (uintptr_t)/*phys_to_virt*/(phys_to_virt((uintptr_t)(addr)) - (uintptr_t)(req->req.buf + req->req.actual));
#ifdef RX_mode1_log
	printk("@@@bytesdone:%x\n",bytesdone);
#endif
	DBG_MSG("irq-- data: %02x %02x %02x \n", *((u8 *)req->req.buf), *((u8 *)req->req.buf + 1), *((u8 *)req->req.buf + 2));


#if defined(CONFIG_USB_CONFIGFS_F_FS)
	if((ep->bEndpointAddress & USB_DIR_IN)==0)
	{
		rx_dma_flag=0;
		udelay(100);
	}
#endif

	req->req.actual += bytesdone;

	DBG_MSG("ep %d ac %x lt %x \n", endpoint, bytesdone, bytesleft);

	/* release DMA channel */
	Release_DMA_Channel(ch);
#if defined(TIMER_PATCH)
	ms_stop_timer(ep);
#endif

	/* clean DMA setup in CSR  */
	if (mode & DMA_TX)
	{
#if 1
		if((ep->bEndpointAddress & 0x7F)==1)
			csr2_tmp=udc_read8(MSB250X_USBCREG(0x112));
		else if((ep->bEndpointAddress & 0x7F)==3)
			csr2_tmp=udc_read8(MSB250X_USBCREG(0x132));
		else if((ep->bEndpointAddress & 0x7F)==5)
			csr2_tmp=udc_read8(MSB250X_USBCREG(0x152));
		else if((ep->bEndpointAddress & 0x7F)==7)
			csr2_tmp=udc_read8(MSB250X_USBCREG(0x172));
		else
			printk("ERROR\n");

	//tick_start = HalTimerRead(TIMER_FREERUN_XTAL);
	//while((udc_read8(MSB250X_UDC_TXCSR1_REG)&(MSB250X_UDC_TXCSR1_TXPKTRDY|MSB250X_UDC_TXCSR1_FIFONOEMPTY))!=0)
#if 1
		while((csr2_tmp&(MSB250X_UDC_TXCSR1_TXPKTRDY|MSB250X_UDC_TXCSR1_FIFONOEMPTY))!=0)
		{
			if((ep->bEndpointAddress & 0x7F)==1)
				csr2_tmp=udc_read8(MSB250X_USBCREG(0x112));
			else if((ep->bEndpointAddress & 0x7F)==3)
				csr2_tmp=udc_read8(MSB250X_USBCREG(0x132));
			else if((ep->bEndpointAddress & 0x7F)==5)
				csr2_tmp=udc_read8(MSB250X_USBCREG(0x152));
			else if((ep->bEndpointAddress & 0x7F)==7)
				csr2_tmp=udc_read8(MSB250X_USBCREG(0x172));
			else
				printk("error\n");
	}
#endif
		if((ep->bEndpointAddress & 0x7F)==1)
		{
		csr2 = udc_read8((MSB250X_USBCREG(0x112))+1);
		udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x112))+1);
		}
		else if((ep->bEndpointAddress & 0x7F)==3)
		{
		csr2 = udc_read8((MSB250X_USBCREG(0x132))+1);
		udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x132))+1);
		}
		else if((ep->bEndpointAddress & 0x7F)==5)
		{
		csr2 = udc_read8((MSB250X_USBCREG(0x152))+1);
		udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x152))+1);
		}
		else if((ep->bEndpointAddress & 0x7F)==7)
		{
		csr2 = udc_read8((MSB250X_USBCREG(0x172))+1);
		udc_write8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(0x172))+1);
		}
#endif
	}
	else /* DMA RX */
	{
		if((ep->bEndpointAddress & 0x7F)==2)
		{
			csr2 = udc_read8((MSB250X_USBCREG(0x126))+1);
			udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(0x126))+1);
		}
		else if((ep->bEndpointAddress & 0x7F)==4)
		{
			csr2 = udc_read8((MSB250X_USBCREG(0x146))+1);
			udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(0x146))+1);
		}
		else if((ep->bEndpointAddress & 0x7F)==6)
		{
			csr2 = udc_read8((MSB250X_USBCREG(0x166))+1);
			udc_write8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(0x166))+1);
		}
	}

	/* Bus Error */
	if (control & DMA_BUSERROR_BIT)
    {
		printk(KERN_ERR "DMA Bus ERR\n");

		ep->halted = 1; /* Winder */

		return;
	}

	if (mode & DMA_TX)
	{
		if (req->req.actual == req->req.length)
		{
			if ((req->req.actual % ep->ep.maxpacket) || ((mode & DMA_MODE_ONE)==0)) /* short packet || TX DMA mode0 */
			{
				if (mode & DMA_MODE_ONE)
				{
#ifdef modify_log
					printk("DMA_TX mode1 short packet\n");
#endif
				}
				lastpacket = 1; /* need to set TXPKTRDY manually */
			}
			else  /* the last packet size is equal to MaxEPSize */
			{
				msb250x_udc_done(ep, req, 0);
				/* if DMA busy, it will use FIFO mode, do no one will catually wait DMA */
				/* msb250x_udc_schedule_DMA(ep); */

				/*
				* Because there is no TX interrtup follow TX mode1 & big packet so we
				* have to schedule next TX Req there
				*/
				if(mode & DMA_MODE_ONE)
				msb250x_udc_schedule_done(ep);
				return;
			}
		}
	}
	else /* DMA RX */
	{
#ifdef RX_mode1_log
		printk("[USB]DMA_IRQ_Handler_RX\n");
#endif
		Enable_RX_EP_Interrupt(endpoint);		//????
		fiforemain = bytesleft;

		if (fiforemain == 0)
		{
#if defined(TIMER_PATCH)
			if ((req->req.actual % ep->ep.maxpacket) && (req->req.length!=req->req.actual)) //hw will receive all packets and trigger dma interrupt
#else
			if (req->req.actual % ep->ep.maxpacket) /* short packet */
#endif
			{
				lastpacket = 1;
			}
			else  /* the last packet size is equal to MaxEPSize */
			{
				if (mode & DMA_MODE_ONE)
				{
#if defined(TIMER_PATCH)
					ep_idx=0x106+(0x10*ep->bEndpointAddress);
					if(req->req.actual % ep->ep.maxpacket)
						udc_write8(0, MSB250X_USBCREG(ep_idx));
#endif
#ifdef NAK_MODIFY
					if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
						ms_NAKEnable(ep->bEndpointAddress);

					while((udc_read8(MSB250X_UDC_USB_CFG7_H)&0x80)==0)	//last done bit
					{
						printk("[USB]Last done bit\n");
					}
#endif

#ifdef RX_modify_mode1
					ep->DmaRxMode1=0;
#endif
                    msb250x_udc_done(ep, req, 0);

                    /* if DMA busy, it will use FIFO mode, no one will catually wait DMA */
                    /* msb250x_udc_schedule_DMA(ep); */

                    /*
                     * Because there is no RX interrtup follow RX mode1 & big packet so we
                     * have to schedule next RX Req there
                      */
                    msb250x_udc_schedule_done(ep);
					//val2=jiffies;
					//ms=val2 - val;
					//ms=jiffies_to_milliseconds(val2 - val);
					//ms=ms*1000*1000/HZ;
					//ms= jiffies_to_usecs(val2 - val);
					//printk("[USB]tick:%ld\n",ms);
//udelay(150);
                    return;
                }
				else
				{
					DBG_MSG("DMA rx mode 0 partial done \n");
#ifdef RX_mode1_log
					printk("DMA rx mode 0 partial done \n");
#endif
					if((ep->bEndpointAddress & 0x7F)==2)
					{
						csr = udc_read8(MSB250X_USBCREG(0x126));
						udc_write8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(0x126));
					}
					if((ep->bEndpointAddress & 0x7F)==4)
					{
						csr = udc_read8(MSB250X_USBCREG(0x146));
						udc_write8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(0x146));
					}
					if((ep->bEndpointAddress & 0x7F)==6)
					{
						csr = udc_read8(MSB250X_USBCREG(0x166));
						udc_write8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(0x166));
					}

                     /* Buffer Full */

					if(req->req.actual == req->req.length)			//finished
					{
						DBG_MSG("buff full len %x \n", req->req.actual);
						msb250x_udc_done(ep, req, 0);
						//printk("mode 0 done->do request\n");
						msb250x_udc_schedule_done(ep);
					}
#ifdef RX_modify_mode1
					else
					{
#ifdef RX_mode1_log
						printk("[USB]do_request for following mode1 packet\n");
#endif
						//do_request
						msb250x_udc_do_request(ep, req);
					}
#endif
                     /* Do not call this becasue if DMA busy, it will use FIFO mode, do no one will catually wait DMA */
                     /* msb250x_udc_schedule_DMA(ep); */
//udelay(150);
                     return;
                }
            }
        }
    }

    /*  for short packet, CPU needs to handle TXPKTRDY/RXPKTRDY bit  */
    if (lastpacket)
    {
#ifdef RX_mode1_log
        printk("[USB]DMA_IRQ_Handler_shortpacket\n");
#endif
        if (mode & DMA_TX)
        {
            //udc_write8(MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_UDC_TXCSR1_REG);
            if((ep->bEndpointAddress & 0x7F)==1)
				udc_write8(MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(0x112));
			else if((ep->bEndpointAddress & 0x7F)==3)
				udc_write8(MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(0x132));
			else if((ep->bEndpointAddress & 0x7F)==5)
				udc_write8(MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(0x152));
			else if((ep->bEndpointAddress & 0x7F)==7)
				udc_write8(MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(0x172));
#ifdef TX_log
			printk("DMA_TX TX_PACKET_READY\n");
#endif
        }
        else
        {
			DBG_MSG( "DMARXCSR : %x \n",csr);
			if((ep->bEndpointAddress & 0x7F)==2)
			{
				csr = udc_read8(MSB250X_USBCREG(0x126));
				udc_write8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(0x126));
			}
			else if((ep->bEndpointAddress & 0x7F)==4)
			{
				csr = udc_read8(MSB250X_USBCREG(0x146));
				udc_write8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(0x146));
			}
			else if((ep->bEndpointAddress & 0x7F)==6)
			{
				csr = udc_read8(MSB250X_USBCREG(0x166));
				udc_write8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(0x166));
			}
#ifdef TX_modify
			//	msb250x_udc_done(ep, req, 0);	//rx need to do done, tx has interupt to notify
#endif
		}
#ifndef TX_modify
		msb250x_udc_done(ep, req, 0);
#endif
        /* Do not call this becasue if DMA busy, it will use FIFO mode, do no one will catually wait DMA */
        /* msb250x_udc_schedule_DMA(ep); */
    }

    return;
}

void USB_DisableDMAChannel(s8 channel)
{
    u16 control;

    control = udc_read16((uintptr_t)(DMA_CNTL_REGISTER(channel)));
    control &= (u16)~DMA_ENABLE_BIT;
    udc_write16(control, (uintptr_t)(DMA_CNTL_REGISTER(channel)));
}

void USB_DisableDMAMode1(void)
{
    udc_write16(udc_read16(MSB250X_UDC_DMA_MODE_CTL)&~M_Mode1_P_Enable, MSB250X_UDC_DMA_MODE_CTL);
}

void USB_ResetDMAMode(void)
{
    udc_write16(0, MSB250X_UDC_DMA_MODE_CTL); //disable set_ok2rcv[15]&ECO4NAK_en[14],wayne added
}

void USB_EnableDMA(void)
{
    USB_DisableDMAChannel(MAX_USB_DMA_CHANNEL);
    USB_ResetDMAMode();
    udc_write16((udc_read16(MSB250X_UDC_EP_BULKOUT)|M_Mode1_P_BulkOut_EP), MSB250X_UDC_EP_BULKOUT);
    udc_write16((udc_read16(MSB250X_UDC_DMA_MODE_CTL)|M_Mode1_P_Enable|M_Mode1_P_AllowAck), MSB250X_UDC_DMA_MODE_CTL); // Allow ACK
}

void USB_DisableDMA(void)
{
    USB_ResetDMAMode();
    USB_DisableDMAChannel(MAX_USB_DMA_CHANNEL);
}

void Control_EP_Interrupt(s8 ep, u32 mode)
{
    uintptr_t reg, current_reg, bit;
    u8 endpoint;

    endpoint = ep;

    if(mode & EP_IRQ_TX)
        reg = (endpoint < 8) ? MSB250X_UDC_INTRTX1E_REG : MSB250X_UDC_INTRTX2E_REG;
    else
        reg = (endpoint < 8) ? MSB250X_UDC_INTRRX1E_REG : MSB250X_UDC_INTRRX2E_REG;


    current_reg = udc_read8(reg);

    bit = 1 << (endpoint % 8);

    if(mode & EP_IRQ_ENABLE)
        udc_write8((current_reg | bit), reg);
    else
        udc_write8((current_reg & ~bit), reg);

}

void USB_Set_ClrRXMode1(void)
{
    DBG_MSG("USB_Set_ClrRXMode1\n");
    //udc_write16((udc_read16(MSB250X_UDC_DMA_MODE_CTL)&~M_Mode1_P_OK2Rcv), MSB250X_UDC_DMA_MODE_CTL); //disable set_ok2rcv[15]&ECO4NAK_en[14],wayne added
	udc_write16((udc_read16(MSB250X_UDC_DMA_MODE_CTL)|M_Mode1_P_OK2Rcv), MSB250X_UDC_DMA_MODE_CTL);
	udc_write16((udc_read16(MSB250X_UDC_DMA_MODE_CTL)|M_Mode1_P_AllowAck), MSB250X_UDC_DMA_MODE_CTL); //enable Allow ok,wayne added
}

u16 USB_Read_DMA_Control(s8 nChannel)
{
    return *((DMA_CNTL_REGISTER(nChannel)));
}

enum DMA_RX_MODE_TYPE msb250_udc_set_dma_rx_by_name(const char * dev_name)
{
    if(!strcmp(dev_name,"g_ether"))
    {
        DBG_MSG("DMA_CONFIG: %s with dma_rx_mode0 \n", dev_name);
        return DMA_RX_MODE0;
    }

    if(!strcmp(dev_name,"g_file_storage"))
    {
        DBG_MSG("DMA_CONFIG: %s with dma_rx_mode1 \n", dev_name);
        return DMA_RX_MODE1;
    }

    DBG_MSG("DMA_CONFIG: %s with NO RX DMA \n", dev_name);
    return DMA_RX_MODE1;//DMA_RX_MODE_NULL;
}

EXPORT_SYMBOL_GPL(USB_Set_ClrRXMode1);
#endif /* CONFIG_USB_MSB250X_DMA */
