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
#include <linux/delay.h>
#include <linux/usb/gadget.h>
#include "msb250x_udc.h"

#if 1
#ifdef CONFIG_USB_MSB250X_DEBUG
#define DBG_MSG(x...)	printk(KERN_INFO x)
#else
#define DBG_MSG(x...)
#endif

s8 free_dma_channels = 0x7f;
#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
int rx_dma_flag = 0;
#endif

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

static void Set32BitsReg(u32 volatile* Reg,u32 val)
{
	UDC_WRITE16((val & 0xffff), (uintptr_t)Reg);
	UDC_WRITE16((val >> 16), (uintptr_t)(Reg + 1));
}

u32 Get32BitsReg(u32 volatile* Reg)
{
	return (UDC_READ16((uintptr_t)Reg) & 0xffff) | (UDC_READ16((uintptr_t)(Reg + 1)) << 16);
}

static inline s8 Get_DMA_Channel(void)
{
	s8 i, bit;

	for(i = 0, bit = 1; i <  MAX_USB_DMA_CHANNEL; i++, bit <<= 1) {
		if(free_dma_channels & bit) {
			free_dma_channels &= ~bit;
			DBG_MSG( "Get Channel:%x ",i+1);
			return i+1;
		}
	}
	DBG_MSG( "no channel");
	return -EBUSY;
}

void Release_DMA_Channel(s8 channel)
{
	DBG_MSG( "release channel:%x \n", channel);
	if((0 < channel) && (channel <= MAX_USB_DMA_CHANNEL))
		free_dma_channels |= (1 << (channel - 1));
}
EXPORT_SYMBOL(Release_DMA_Channel);
s8 check_dma_busy(void)
{
	s8 i, bit;

	for(i = 0, bit = 1; i < MAX_USB_DMA_CHANNEL; i++, bit <<= 1) {
		if(free_dma_channels & bit)
			return DMA_NOT_BUSY;
	}
	return DMA_BUSY;
}

#if defined(TIMER_PATCH)
#include <linux/irq.h>
struct udc_timer {
	struct timer_list timer;
	struct msb250x_ep *ep;
} dma_polling_timer[MAX_USB_DMA_CHANNEL];

void ms_init_timer(void)
{
	int i;

	for (i = 0; i < MAX_USB_DMA_CHANNEL; i++)
		dma_init_timer(&dma_polling_timer[i].timer);
}

void ms_start_timer(struct msb250x_ep *ep)
{
	ep->wtd_dma_count=0;
	ep->wtd_rx_count=0;
	dma_polling_timer[ep->ch-1].ep = ep;
	dma_polling_timer[ep->ch-1].timer.expires = jiffies + msecs_to_jiffies(TIMEOUT_MS);
	mod_timer(&dma_polling_timer[ep->ch-1].timer, dma_polling_timer[ep->ch-1].timer.expires);
}

void ms_stop_timer(struct msb250x_ep *ep)
{
	if((!(ep->bEndpointAddress & USB_DIR_IN)) && ((0 < ep->ch)
			&& (ep->ch <= MAX_USB_DMA_CHANNEL))) {
		del_timer(&dma_polling_timer[ep->ch-1].timer);
		ep->sw_ep_irq = 0;
	}
}
EXPORT_SYMBOL(ms_stop_timer);
static void polling_func(struct timer_list *t)
{
	struct udc_timer *dma_polling_timer = from_timer(dma_polling_timer, t, timer);
	struct msb250x_ep *ep = dma_polling_timer->ep;
	u16 count_idx;

	if (!ep) {
		pr_err("ep structure null\n");
		return;
	}

	count_idx = 0x108 + (0x10 * ep->bEndpointAddress);

	//printk("D_count:%d, D_cntl:0x%x, rcsr:0x%x, rco:%d\n", Get32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ep->ch)), Get32BitsReg((u32 volatile*)DMA_CNTL_REGISTER(ep->ch)), UDC_READ16(MSB250X_USBCREG(0x126)), UDC_READ16(MSB250X_USBCREG(0x128)));
	if((ep->wtd_dma_count == Get32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ep->ch))) && (ep->wtd_rx_count == UDC_READ16(MSB250X_USBCREG(count_idx))) && (ep->wtd_dma_count!=0))
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
		ep->wtd_rx_count = UDC_READ16(MSB250X_USBCREG(count_idx));
		mod_timer(&dma_polling_timer[ep->ch-1].timer, jiffies + msecs_to_jiffies(TIMEOUT_MS));
	}

	return;
}

void dma_init_timer(struct timer_list *dma_polling_timer)
{
	pr_info("[UDC]timer init\n");
	timer_setup(dma_polling_timer, polling_func, 0);
}
#endif

void ms_Ok2Rcv(u8 bEndpointAddress, u16 packnum)
{
	if(bEndpointAddress == UDC_READ8(MSB250X_UDC_EP_BULKOUT))
		UDC_WRITE16((M_Mode1_P_OK2Rcv | M_Mode1_P_NAK_Enable | packnum), MSB250X_UDC_DMA_MODE_CTL);
	if(bEndpointAddress == (UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f)) {
		UDC_WRITE16((packnum), MSB250X_UDC_USB_CFG1_L);
		UDC_WRITE8((M_Mode1_P_OK2Rcv_1 | M_Mode1_P_NAK_Enable_1 | M_Mode1_P_BulkOut_EP_4), MSB250X_UDC_DMA_MODE_CTL1);
	}
}

s8 USB_Set_DMA(struct usb_ep *_ep, struct msb250x_request *req, u32 count, u32 mode)
{
	s8 ch, idx;
	u8 retry_count=0;
	u16 control, packnum, csr_idx;
	u32 csr2=0, fiforemain=0;
	uintptr_t address, pa, new_addr=0;
	/* enum dma_data_direction dir; */
	enum dma_data_direction dir;
	struct msb250x_ep *ep = to_msb250x_ep(_ep);

	address = (uintptr_t)(req->req.buf) + (uintptr_t)(req->req.actual);
	idx = ep->bEndpointAddress & 0x7F;

#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
	if(ep->bEndpointAddress & USB_DIR_IN)
		while (rx_dma_flag) {
			if (UDC_READ8(M_REG_DMA_INTR) || UDC_READ16(MSB250X_UDC_INTRRX_REG))
				break;
			else
				udelay(50);

			if(retry_count >= 20)
				break;
			retry_count++;
		}
#endif
	RX_MODE1_LOG("ep %d count %x dma actual %x \n", idx, count, req->req.actual);
	ch = Get_DMA_Channel();

	if(ch < 0) {
		printk( "Get DMA channel fail %d\n", free_dma_channels);
		return -EBUSY;
	}

	if (count > _ep->maxpacket)
		mode |= DMA_MODE_ONE;
	else {
		if(mode & DMA_TX) {
			u16 ep_maxpacket;

			ep_maxpacket = _ep->maxpacket;
			count = min((u16)ep_maxpacket, (u16)count);
		}
		else {
			fiforemain = msb250x_udc_fifo_count_epx(idx);

			count = min(fiforemain, count);
		}
		mode &= ~DMA_MODE_ONE;
	}

	control = DMA_ENABLE_BIT | mode | (idx << DMA_ENDPOINT_SHIFT) | (DMA_BurstMode << 9);
	dir = ((ep->bEndpointAddress & 0x80)==0x80) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	pa = dma_map_single(&ep->dev->pdev->dev,(void *)address, req->req.length - req->req.actual, dir);
	ep->pa_addr = pa;
	ep->dma_flag = 1;
	new_addr = BUS2PA(pa);
	Set32BitsReg((u32 volatile*)DMA_ADDR_REGISTER(ch), (u32)new_addr);
	Set32BitsReg((u32 volatile*)DMA_COUNT_REGISTER(ch), count);
	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);

	switch(mode & DMA_MODE_MASK) {
		case DMA_RX | DMA_MODE_ZERO:
			DBG_MSG( "1_ Rx_0 ep: %x, count = %x, Request = %x, control = %x\n", idx, count, req->req.length, control);
			UDC_WRITE8(UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1) & ~0x20,(MSB250X_USBCREG(csr_idx)) + 1);
			csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
			Enable_RX_EP_Interrupt(idx);
			UDC_WRITE8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(csr_idx)) + 1);
			break;
		case DMA_TX | DMA_MODE_ZERO:
			DBG_MSG( "2_ Tx_0 ep: %x, buff = %x, count = %x, Request = %x, control = %x\n", idx, address, count, req->req.length, control);
			Enable_TX_EP_Interrupt(idx);
			csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
			UDC_WRITE8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(csr_idx)) + 1);
			break;
		case DMA_RX | DMA_MODE_ONE:
#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
			rx_dma_flag=1;
#endif
			UDC_WRITE8(UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1) & ~0x20,(MSB250X_USBCREG(csr_idx)) + 1);
			csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
			UDC_WRITE16(control, (uintptr_t)DMA_CNTL_REGISTER(ch));
			Enable_RX_EP_Interrupt(idx);
			UDC_WRITE8((csr2 | RXCSR2_MODE1 | MSB250X_UDC_RXCSR2_DISNYET), (MSB250X_USBCREG(csr_idx)) + 1);
			packnum = count / (_ep->maxpacket);
			if (count % _ep->maxpacket)
				packnum += 1;

			if(((ep->bEndpointAddress) & 0x0f) == 2) {
				if (ep->RxShort == 1) {
					udelay(125);
					ep->RxShort = 0;
				}
				if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
					ms_Ok2Rcv(ep->bEndpointAddress, packnum);
			}
			else if(((ep->bEndpointAddress) & 0x0f) == 4) {
				if (ep->RxShort == 1) {
					udelay(125);
					ep->RxShort = 0;
				}
				if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
					ms_Ok2Rcv(ep->bEndpointAddress, packnum);
			}

			RX_MODE1_LOG("[USB]Set RX1,count = %x, Request = %x, ch = %x\n",count,req->req.length,ch);
			ep->DmaRxMode1 = 1;
			ep->ch=ch;
#if defined(TIMER_PATCH)
			//sw patch for timer to watch dma status
			ms_start_timer(ep);
#endif
			return SUCCESS;
		case DMA_TX | DMA_MODE_ONE:
			TX_LOG( "4_ Tx_1 ep: %x, count = %x, Request = %x, control = %x\n", idx, count, req->req.length, control);
			Enable_TX_EP_Interrupt(idx);
			csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
			UDC_WRITE8((csr2 | TXCSR2_MODE1), (MSB250X_USBCREG(csr_idx)) + 1);
			break;
	}

	UDC_WRITE16(control, (uintptr_t)DMA_CNTL_REGISTER(ch));
	return SUCCESS;
}

void USB_DMA_IRQ_Handler(u8 ch, struct msb250x_udc *dev)
{
	u8 endpoint, direction, csr2, csr2_tmp=0;
	u16 csr_idx = 0;
	u32 csr, mode, bytesleft, bytesdone, control, fiforemain, lastpacket = 0;
	/* enum dma_data_direction dir; */
	enum dma_data_direction dir;
	struct msb250x_ep *ep;
	struct msb250x_request	*req = NULL;
	uintptr_t addr;

	control = UDC_READ16((uintptr_t)(DMA_CNTL_REGISTER(ch)));
	mode = control & 0xf;

	addr = (uintptr_t)Get32BitsReg((u32 volatile *)(DMA_ADDR_REGISTER(ch)));
	bytesleft = Get32BitsReg((u32 volatile *)(DMA_COUNT_REGISTER(ch)));

	endpoint = (UDC_READ16((uintptr_t)(DMA_CNTL_REGISTER(ch))) & 0xf0) >> DMA_ENDPOINT_SHIFT;
	direction = (mode & DMA_TX) ? 0 : 1;

	ep = &dev->ep[endpoint];

	if (likely (!list_empty(&ep->queue)))
		req = list_entry(ep->queue.next, struct msb250x_request, queue);
	else
		req = NULL;

	if (!req) {
		printk("no request but DMA done?!\n");
		printk("ep %x  left %x \n", endpoint, bytesleft);
		return;
	}

	addr = PA2BUS(addr);
	bytesdone = (uintptr_t)(phys_to_virt((uintptr_t)(addr)) - (uintptr_t)(req->req.buf + req->req.actual));
	dir = ((ep->bEndpointAddress & 0x80)==0x80) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	if(ep->dma_flag == 0)
		printk("[UDC]unmap twice....\n");
	ep->dma_flag = 0;
	dma_unmap_single(&ep->dev->pdev->dev, ep->pa_addr, req->req.length - req->req.actual, dir);
	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);

	RX_MODE1_LOG("@@@bytesdone:%x\n", bytesdone);

#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
	if((ep->bEndpointAddress & USB_DIR_IN)==0) {
		rx_dma_flag = 0;
		udelay(100);
	}
#endif
	req->req.actual += bytesdone;
	Release_DMA_Channel(ch);
#if defined(TIMER_PATCH)
	ms_stop_timer(ep);
#endif
	if (mode & DMA_TX) {
		csr2_tmp = UDC_READ8(MSB250X_USBCREG(csr_idx));
		while((csr2_tmp & (MSB250X_UDC_TXCSR1_TXPKTRDY | MSB250X_UDC_TXCSR1_FIFONOEMPTY)) != 0)
			csr2_tmp = UDC_READ8(MSB250X_USBCREG(csr_idx));

		csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
		UDC_WRITE8((csr2 & ~TXCSR2_MODE1), (MSB250X_USBCREG(csr_idx)) + 1);
	}
	else {
		csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
		UDC_WRITE8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(csr_idx)) + 1);
	}

	/* Bus Error */
	if (control & DMA_BUSERROR_BIT) {
		printk(KERN_ERR "DMA Bus ERR\n");
		ep->halted = 1;
		return;
	}
	if (mode & DMA_TX) {
		if (req->req.actual == req->req.length) {
			if ((req->req.actual % ep->ep.maxpacket) || ((mode & DMA_MODE_ONE) == 0)) {
				TX_LOG("DMA_TX mode1 short packet\n");
				lastpacket = 1; /* need to set TXPKTRDY manually */
			}
			else {
				/* the last packet size is equal to MaxEPSize */
				msb250x_udc_done(ep, req, 0);
				if(mode & DMA_MODE_ONE)
					msb250x_udc_schedule_done(ep);
				return;
			}
		}
	}
	else {
		Enable_RX_EP_Interrupt(endpoint);
		fiforemain = bytesleft;
		if (fiforemain == 0) {
#if defined(TIMER_PATCH)
			//hw will receive all packets and trigger dma interrupt
			if ((req->req.actual % ep->ep.maxpacket) && (req->req.length!=req->req.actual))
				lastpacket = 1;
#else
			//short packet
			if (req->req.actual % ep->ep.maxpacket)
				lastpacket = 1;
#endif
			//the last packet size is equal to MaxEPSize
			else {
				if (mode & DMA_MODE_ONE) {
#if defined(TIMER_PATCH)
					if(req->req.actual % ep->ep.maxpacket)
						UDC_WRITE8(0, MSB250X_USBCREG(csr_idx));
#endif
					if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
						ms_NAKEnable(ep->bEndpointAddress);

					//polling last done bit
					while((UDC_READ8(MSB250X_UDC_USB_CFG7_H)&0x80) == 0)
						printk("[USB]Last done bit\n");

					ep->DmaRxMode1=0;
					msb250x_udc_done(ep, req, 0);
					msb250x_udc_schedule_done(ep);
					return;
                }
				else {
					csr = UDC_READ8(MSB250X_USBCREG(csr_idx));
					UDC_WRITE8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(csr_idx));
					if(req->req.actual == req->req.length) {
						DBG_MSG("buff full len %x \n", req->req.actual);
						msb250x_udc_done(ep, req, 0);
						msb250x_udc_schedule_done(ep);
					}
					else
						msb250x_udc_do_request(ep, req);

					return;
				}
			}
		}
	}

	/*  for short packet, CPU needs to handle TXPKTRDY/RXPKTRDY bit  */
	if (lastpacket) {
		if (mode & DMA_TX) {
			UDC_WRITE8(MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(csr_idx));
			TX_LOG("DMA_TX TX_PACKET_READY\n");
		}
		else {
			csr = UDC_READ8(MSB250X_USBCREG(csr_idx));
			UDC_WRITE8((csr & ~MSB250X_UDC_RXCSR1_RXPKTRDY), MSB250X_USBCREG(csr_idx));
		}
	}
	return;
}

void Control_EP_Interrupt(s8 ep, u32 mode)
{
	u8 endpoint;
	uintptr_t reg, current_reg, bit;

	endpoint = ep;

	if(mode & EP_IRQ_TX)
		reg = (endpoint < 8) ? MSB250X_UDC_INTRTX1E_REG : MSB250X_UDC_INTRTX2E_REG;
	else
		reg = (endpoint < 8) ? MSB250X_UDC_INTRRX1E_REG : MSB250X_UDC_INTRRX2E_REG;

	current_reg = UDC_READ8(reg);
	bit = 1 << (endpoint % 8);

	if(mode & EP_IRQ_ENABLE)
		UDC_WRITE8((current_reg | bit), reg);
	else
		UDC_WRITE8((current_reg & ~bit), reg);
}

void USB_Set_ClrRXMode1(void)
{
	UDC_WRITE16((UDC_READ16(MSB250X_UDC_DMA_MODE_CTL) | M_Mode1_P_OK2Rcv), MSB250X_UDC_DMA_MODE_CTL);
	UDC_WRITE16((UDC_READ16(MSB250X_UDC_DMA_MODE_CTL) | M_Mode1_P_AllowAck), MSB250X_UDC_DMA_MODE_CTL);
}
u16 USB_Read_DMA_Control(s8 nChannel)
{
	return *((DMA_CNTL_REGISTER(nChannel)));
}
#endif /* CONFIG_USB_MSB250X_DMA */
