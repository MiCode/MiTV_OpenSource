#include <linux/module.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <asm/uaccess.h>

#include "msb250x_udc.h"

#if defined(MSB250X_UDC_DEBUG)
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#if defined(UDC_DEBUG_TX_RX)
#define DBG_TR(x...) printk(KERN_INFO x)
#else
#define DBG_TR(x...)
#endif
#if defined(UDC_FUNCTION_LOG)
#define DBG_fn(x...) printk(KERN_INFO x)
#else
#define DBG_fn(x...)
#endif

#define	PLATFORM_DRIVER	udc_mstar_driver
/* #define init_MUTEX(sem)	sema_init(sem, 1) */

static const char sg_gadget_name[] = "msb250x_udc";
struct msb250x_udc *sg_udc_controller = NULL;
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
static int using_dma = 0;
#endif
static int msb250x_udc_major = MSB250X_UDC_MAJOR;
static int msb250x_udc_minor = 0;
static struct class *msb250x_udc_class;
extern s8 free_dma_channels;

module_param(msb250x_udc_major, int, S_IRUGO);
module_param(msb250x_udc_minor, int, S_IRUGO);

#define MSB250X_UDC_CLEAR_EP0_OPR()	UDC_WRITE8(MSB250X_UDC_CSR0_SRXPKTRDY, MSB250X_USBCREG(0x102))
#define MSB250X_UDC_CLEAR_EP0_SST() UDC_WRITE8(~MSB250X_UDC_CSR0_SENTSTALL & UDC_READ8(MSB250X_USBCREG(0x102)), MSB250X_USBCREG(0x102))
#define MSB250X_UDC_CLEAR_EP0_SE()	UDC_WRITE8(MSB250X_UDC_CSR0_SSETUPEND, MSB250X_USBCREG(0x102))
#define MSB250X_UDC_SET_EP0_IPR()	UDC_WRITE8(MSB250X_UDC_CSR0_TXPKTRDY, MSB250X_USBCREG(0x102))
#define MSB250X_UDC_SET_EP0_DE()	UDC_WRITE8(MSB250X_UDC_CSR0_DATAEND, MSB250X_USBCREG(0x102))
#define MSB250X_UDC_SET_EP0_SS()	UDC_WRITE8(MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_SENDSTALL, MSB250X_USBCREG(0x102))
#define MSB250X_UDC_SET_EP0_DE_OUT() UDC_WRITE8((MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_DATAEND), MSB250X_USBCREG(0x102))
#define MSB250X_UDC_SET_EP0_SSE_OUT() UDC_WRITE8((MSB250X_UDC_CSR0_SRXPKTRDY | MSB250X_UDC_CSR0_SSETUPEND), MSB250X_USBCREG(0x102))
#define MSB250X_UDC_SET_EP0_DE_IN() UDC_WRITE8((MSB250X_UDC_CSR0_TXPKTRDY | MSB250X_UDC_CSR0_DATAEND), MSB250X_USBCREG(0x102))

extern void ms_stop_timer(struct msb250x_ep *ep);
extern void Release_DMA_Channel(s8 channel);

static void msb250x_udc_nuke(struct msb250x_udc *udc, struct msb250x_ep *ep, int status)
{
	DBG_fn("%s\n", __func__);

	if (&ep->queue == NULL)
		return;

	while (!list_empty (&ep->queue)) {
		struct msb250x_request *req;
		req = list_entry (ep->queue.next, struct msb250x_request, queue);
		msb250x_udc_done(ep, req, status);
	}
}

int msb250x_udc_fifo_count_epx(u8 ep_idx)
{
	return (UDC_READ16(MSB250X_UDC_RXCOUNT_L_EPX_REG(ep_idx)));
}


u16 GetCSR_EpIdx(u8 bEndpointAddress)
{
	u16 csr_idx;

	if((bEndpointAddress & 0x80) == 0x80)
		return csr_idx = 0x102 + (0x10*(bEndpointAddress & 0x0f));
	else
		return csr_idx = 0x106 + (0x10*bEndpointAddress);
}

void ms_NAKEnable(u8 bEndpointAddress)
{
	if(bEndpointAddress == UDC_READ8(MSB250X_UDC_EP_BULKOUT))
		UDC_WRITE16((UDC_READ16(MSB250X_UDC_DMA_MODE_CTL) | (M_Mode1_P_NAK_Enable)), MSB250X_UDC_DMA_MODE_CTL);

	if(bEndpointAddress == (UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f))
		UDC_WRITE8((UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) | (M_Mode1_P_NAK_Enable_1)), MSB250X_UDC_DMA_MODE_CTL1);
}

void ms_AllowAck(u8 bEndpointAddress)
{
	if(bEndpointAddress == UDC_READ8(MSB250X_UDC_EP_BULKOUT))
		UDC_WRITE16(UDC_READ16(MSB250X_UDC_DMA_MODE_CTL) | M_Mode1_P_AllowAck | M_Mode1_P_NAK_Enable, MSB250X_UDC_DMA_MODE_CTL);

	if(bEndpointAddress == (UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f))
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) | M_Mode1_P_AllowAck_1 | M_Mode1_P_NAK_Enable_1, MSB250X_UDC_DMA_MODE_CTL1);
}

void ms_autonak_clear(u8 bEndpointAddress)
{
	if(bEndpointAddress == UDC_READ8(MSB250X_UDC_EP_BULKOUT)) {
		UDC_WRITE16((UDC_READ16(MSB250X_UDC_EP_BULKOUT) & 0xfff0), MSB250X_UDC_EP_BULKOUT);
		UDC_WRITE16(UDC_READ16(MSB250X_UDC_DMA_MODE_CTL) & ~(M_Mode1_P_NAK_Enable), MSB250X_UDC_DMA_MODE_CTL);
	}
	if(bEndpointAddress == (UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f)) {
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0xf0, MSB250X_UDC_DMA_MODE_CTL1);
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & ~M_Mode1_P_NAK_Enable_1, MSB250X_UDC_DMA_MODE_CTL1);
	}
}

void ms_autonak_set(u8 bEndpointAddress)
{
	//set auto_nak 0
	if(UDC_READ8(MSB250X_UDC_EP_BULKOUT) == 0x0) {
		UDC_WRITE16(bEndpointAddress, MSB250X_UDC_EP_BULKOUT);
		UDC_WRITE16(UDC_READ16(MSB250X_UDC_DMA_MODE_CTL) | M_Mode1_P_NAK_Enable, MSB250X_UDC_DMA_MODE_CTL);
		return;
	}
	//set auto_nak 1
	else if((UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0x0f) == 0x0) {
		UDC_WRITE8(M_Mode1_P_NAK_Enable_1 | bEndpointAddress, MSB250X_UDC_DMA_MODE_CTL1);
		return;
	}

	printk("[UDC]no more auto_nak!!\n");
	return;
}

#if defined(ENABLE_RIU_2BYTES_READ)
void msb250x_udc_RIU_Read2Bytes(u8 *buf, int len, uintptr_t fifo)
{
	if (len) {
		u8 *buffer = buf;
		int i = len;

		while (i > 1) {
			u16 x = __raw_readw((void *)(fifo));

			*(buffer) = (u8)x;
			*(buffer + 1) = (u8)(x >> 8);
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

static int msb250x_udc_read_fifo_ctl_req(struct usb_ctrlrequest *crq)
{
	unsigned char *outbuf = (unsigned char*)crq;
	int bytes_read = 0;
	uintptr_t fifo = MSB250X_UDC_EP0_FIFO_ACCESS_L;

	DBG_fn("%s\n", __func__);
	bytes_read = msb250x_udc_fifo_count_epx(0);
	if (bytes_read > sizeof(struct usb_ctrlrequest))
		bytes_read = sizeof(struct usb_ctrlrequest);
#if defined(ENABLE_RIU_2BYTES_READ)
	msb250x_udc_RIU_Read2Bytes(outbuf, bytes_read, fifo);
#else
	readsb((void *)(fifo), outbuf, bytes_read);
#endif
	DBG("%s: len=%d %02x:%02x {%x,%x,%x}\n", __FUNCTION__,
		bytes_read, crq->bRequest, crq->bRequestType,
		crq->wValue, crq->wIndex, crq->wLength);

	return bytes_read;
}

static int msb250x_udc_write_packet(uintptr_t fifo, struct msb250x_request *req, unsigned max)
{
	unsigned int len = min(req->req.length - req->req.actual, max);
	u8 *buf = req->req.buf + req->req.actual;

	prefetch(buf);
	DBG("%s %d %d %d %d\n", __FUNCTION__, req->req.actual, req->req.length, len, req->req.actual + len);
	req->req.actual += len;
    writesb((void *)(fifo), buf, len);

	return len;
}

#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
extern	int rx_dma_flag;
#endif
static int msb250x_udc_write_fifo(struct msb250x_ep *ep, struct msb250x_request *req)
{
	unsigned int count = 0;
	int is_last = 0;
#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
	u8 retry_count=0;
#endif
	u16 csr_idx;
	u32 idx = 0,ep_csr = 0;
	uintptr_t fifo_reg = 0;

	DBG_fn("%s\n", __func__);
	idx = ep->bEndpointAddress & 0x7F;

	fifo_reg = MSB250X_UDC_EPX_FIFO_ACCESS_L(idx);

	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);
	count = msb250x_udc_write_packet(fifo_reg, req, ep->ep.maxpacket);
	if (count != ep->ep.maxpacket)
		is_last = 1;
	else if (req->req.length != req->req.actual || req->req.zero)
		is_last = 0;
	else
		is_last = 2;

#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
	while(rx_dma_flag) {
		if(UDC_READ8(M_REG_DMA_INTR) || UDC_READ16(MSB250X_UDC_INTRRX_REG))
			break;
		else
			udelay(50);

		if(retry_count >= 20)
			break;

		retry_count++;
	}
#endif

	if (idx == 0)
		DBG("Written ep%d %d.%d of %d b [last %d,z %d]\n",
			idx, count, req->req.actual, req->req.length,
			is_last, req->req.zero);

	if (is_last) {
		if (idx == 0) {
			if (! (UDC_READ8(MSB250X_UDC_INTRUSB_REG) & MSB250X_UDC_INTRUSB_RESET))
				MSB250X_UDC_SET_EP0_DE_IN();
			ep->dev->ep0state=EP0_IDLE;
		}
		else {
			ep_csr = UDC_READ8(MSB250X_USBCREG(csr_idx));
			UDC_WRITE8(ep_csr | MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(csr_idx));
		}
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
#else
		/* FIFO mode */
		msb250x_udc_done(ep, req, 0);
#endif
		is_last = 1;
	}
	else {
		if (idx == 0) {
			if (! (UDC_READ8(MSB250X_UDC_INTRUSB_REG) & MSB250X_UDC_INTRUSB_RESET))
				MSB250X_UDC_SET_EP0_IPR();
		}
		else {
			ep_csr = UDC_READ8(MSB250X_USBCREG(csr_idx));
			UDC_WRITE8(ep_csr | MSB250X_UDC_TXCSR1_TXPKTRDY, MSB250X_USBCREG(csr_idx));
		}
	}

	return is_last;
}

static int msb250x_udc_read_packet(uintptr_t fifo, u8 *buf, struct msb250x_request *req, unsigned avail)
{
	unsigned int len = 0;

	if(!buf) {
		DBG("%s buff null \n", __FUNCTION__);
		return 0;
	}
	len = min(req->req.length - req->req.actual, avail);
	req->req.actual += len;
#if defined(ENABLE_RIU_2BYTES_READ)
	msb250x_udc_RIU_Read2Bytes(buf, len, fifo);
#else
	readsb((void *)(fifo), buf, len);
#endif

	return len;
}

static int msb250x_udc_read_fifo(struct msb250x_ep *ep, struct msb250x_request *req)
{
	u8 *buf = NULL;
	unsigned bufferspace = 0, avail = 0;
	int is_last = 1, fifo_count = 0;
	u16 csr_idx;
	u32 idx = 0;
	uintptr_t fifo_reg = 0;

	idx = ep->bEndpointAddress & 0x7F;

	fifo_reg = MSB250X_UDC_EPX_FIFO_ACCESS_L(idx);

    if (!req->req.length)
        return 1;

	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);
	buf = req->req.buf + req->req.actual;
	bufferspace = req->req.length - req->req.actual;

    if (!bufferspace) {
		printk(KERN_ERR "%s: buffer full!\n", __FUNCTION__);
		return -1;
	}

	fifo_count = msb250x_udc_fifo_count_epx(idx);

	DBG("%s fifo count : %d\n", __FUNCTION__, fifo_count);

	if (fifo_count > ep->ep.maxpacket)
		avail = ep->ep.maxpacket;
	else
		avail = fifo_count;

	RX_MODE1_LOG("read fifo-> len %x -- buf %p -- avail %x\n", req->req.length, buf, avail);
	fifo_count = msb250x_udc_read_packet(fifo_reg, buf, req, avail);

	if (idx != 0 && fifo_count < ep->ep.maxpacket) {
		is_last = 1;
		if (fifo_count != avail)
			req->req.status = -EOVERFLOW;
	}
	else
		is_last = (req->req.length <= req->req.actual) ? 1 : 0;

	if (is_last) {
		if (idx == 0) {
			MSB250X_UDC_SET_EP0_DE_OUT();
			ep->dev->ep0state = EP0_IDLE;
		}
		else
			UDC_WRITE8(0, MSB250X_USBCREG(csr_idx));
		msb250x_udc_done(ep, req, 0);
	}
	else {
		if (idx == 0)
			MSB250X_UDC_CLEAR_EP0_OPR();
		else
			UDC_WRITE8(0,MSB250X_USBCREG(csr_idx));
	}

	return is_last;
}

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
void RxHandler(struct msb250x_ep *ep, struct msb250x_request *req)
{
	int fifo_count = 0;
#if defined(TIMER_PATCH)
	u16 csr_idx = 0;
#endif
	u32 bytesleft;
	uintptr_t addr, bytesdone;
	u8 ep_idx;

	DBG_fn("%s\n", __func__);

	ep_idx = ep->bEndpointAddress & 0x7F;
	if ((ep_idx & 0x01) == 0) { // even EPx
		fifo_count = msb250x_udc_fifo_count_epx(ep_idx);
	} else {
		printk("[UDC] RxHandler with odd endpoint %x?\n", ep_idx);
	}


#if !defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
	if(rx_dma_flag==1) {
		rx_dma_flag=0;
		udelay(100);
	}
#endif

	if (ep->DmaRxMode1) {
		bytesleft = Get32BitsReg((u32 volatile *)(DMA_COUNT_REGISTER(ep->ch)));
		addr = Get32BitsReg((u32 volatile *)(DMA_ADDR_REGISTER(ep->ch)));
		addr = PA2BUS(addr);
		bytesdone = (uintptr_t)(phys_to_virt((uintptr_t)(addr)) - (uintptr_t)(req->req.buf + req->req.actual));
		if(ep->dma_flag == 0)
			printk("[UDC]unmap twice....\n");
		ep->dma_flag = 0;
		dma_unmap_single(&ep->dev->pdev->dev, ep->pa_addr, req->req.length - req->req.actual, DMA_FROM_DEVICE);
		req->req.actual += bytesdone;
		DBG("[USB]RX_handler actual:%x,bytesdone:%x\n", req->req.actual, bytesdone);
#if defined(TIMER_PATCH)
		//hw may subtraction to an negative number, fix the correct actual length
		if(fifo_count>=ep->wMaxPacketSize) {
			u16 cnt;
			csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);
			UDC_WRITE8(0, MSB250X_USBCREG(csr_idx));
			//RxCount uses 11bits
			if ((fifo_count > 1023) && (fifo_count < 2048)) {
					cnt = 0x800 - fifo_count;
					req->req.actual = req->req.actual - cnt;
			}
			else
					printk("[UDC]fifo count strange:%x\n", fifo_count);
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

		ep->DmaRxMode1=0;
		ep->RxShort=1;
		req = NULL;
		msb250x_udc_schedule_done(ep);
	}
	else if (check_dma_busy() == DMA_NOT_BUSY) {
		if(fifo_count > ep->ep.maxpacket) {
			if((req->req.actual + fifo_count) > req->req.length)
				printk(KERN_ERR "usb req buffer is too small\n");
			if(USB_Set_DMA(&ep->ep, req, fifo_count,DMA_RX_ZERO_IRQ) != SUCCESS) {
				if(msb250x_udc_read_fifo(ep, req))
					req = NULL;
			}
		}
		else {
			if(msb250x_udc_read_fifo(ep, req))
				req = NULL;
			msb250x_udc_schedule_done(ep);
		}
	}
	else if(check_dma_busy()==DMA_BUSY) {
		if(fifo_count > ep->ep.maxpacket) {
			if((req->req.actual + fifo_count) > req->req.length)
				printk(KERN_ERR "usb req buffer is too small\n");
			if(msb250x_udc_read_fifo(ep, req))
				req = NULL;
		}
		else {
			if(msb250x_udc_read_fifo(ep, req))
				req = NULL;
			msb250x_udc_schedule_done(ep);
		}
	}
}
#endif

struct msb250x_request * msb250x_udc_do_request(struct msb250x_ep *ep, struct msb250x_request *req)
{
	u8 csr2;
	u8 ep_num;
	u16 csr_idx;
	u32 ep_csr = 0;
	int fifo_count = 0;

	DBG_fn("%s\n", __func__);

	ep_num = ep->bEndpointAddress & 0x7F;
	fifo_count = msb250x_udc_fifo_count_epx(ep_num);
	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);

	if (ep_num)
		ep_csr = UDC_READ8(MSB250X_USBCREG(csr_idx));
	else
		ep_csr = UDC_READ8(MSB250X_USBCREG(0x102));

	RX_MODE1_LOG("ep %d fifo_count: %x req.len %x \n", ep->bEndpointAddress & 0x7F, fifo_count, req->req.length);

	if(!ep->halted) {
		if (ep->bEndpointAddress == 0) {
			switch (ep->dev->ep0state) {
				case EP0_IN_DATA_PHASE:
					if (!(ep_csr & MSB250X_UDC_CSR0_TXPKTRDY) && msb250x_udc_write_fifo(ep, req)) {
						ep->dev->ep0state = EP0_IDLE;
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
#else
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
						MSB250X_UDC_SET_EP0_DE_OUT();
					}
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
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
		else if (using_dma && (ep->bEndpointAddress & USB_DIR_IN)) {
			if(check_dma_busy() == DMA_BUSY) {
				/* Double check to insure this EP is not DMAing */
				u8 endpoint = (u8)(UDC_READ16((uintptr_t)(DMA_CNTL_REGISTER(1))) & 0xf0) >> DMA_ENDPOINT_SHIFT;
				if(endpoint != (ep->bEndpointAddress &0x7f) && msb250x_udc_write_fifo(ep, req))
					DBG("Use fifo write mode\n");
			}
			else {
				if ((req->req.length > ep->ep.maxpacket) && (req->req.length > 8)) {
					u32 tx_dma_count = (u32)(req->req.length) - (u32)(req->req.actual);
					if(USB_Set_DMA(&ep->ep, req, tx_dma_count, DMA_TX_ONE_IRQ) != SUCCESS) {
						DBG("Use fifo write mode\n");
						msb250x_udc_write_fifo(ep, req);
					}
				}
				else {
					TX_LOG("[USB]do_request--write_fifo\n");
					msb250x_udc_write_fifo(ep, req);
				}
			}
		}
		else if (using_dma && !(ep->bEndpointAddress & USB_DIR_IN) && (ep->dev->DmaRxMode != DMA_RX_MODE_NULL)) {
			if(ep->dev->DmaRxMode == DMA_RX_MODE0) {
				if(ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY) {
					if (check_dma_busy() == DMA_NOT_BUSY) {
						if(fifo_count > ep->ep.maxpacket) {
							if((req->req.actual + fifo_count) > req->req.length)
								printk(KERN_ERR "usb req buffer is too small\n");
							if(USB_Set_DMA(&ep->ep, req, fifo_count,DMA_RX_ZERO_IRQ) != SUCCESS) {
								DBG("USB_CLASS_COMM: DMA fail use FIFO\n");
								if(msb250x_udc_read_fifo(ep, req))
									req = NULL;
							}
						} else {
							RX_MODE1_LOG("[USB]2FIFO_COUNT:%x\n",fifo_count);
							if(msb250x_udc_read_fifo(ep, req))
								req = NULL;
						}
					}
				}
			} else {
				/* DMA mode 1*/
				if((req->req.length-req->req.actual) > ep->ep.maxpacket) {
					RX_MODE1_LOG("[USB]length:%x,actual:%x\n",req->req.length,req->req.actual);
					if(check_dma_busy() != DMA_BUSY) {
						u32 rx_dma_count = 0;
						if(((u32)(req->req.actual)) > ((u32)(req->req.length))) {
							printk("actual[0x%x] > len[0x%x]\n", (u32)(req->req.actual), (u32)(req->req.length));
							msb250x_udc_done(ep, req, -EOVERFLOW);
							return req;
						} else
						rx_dma_count = (u32)(req->req.length) - (u32)(req->req.actual);
						if(USB_Set_DMA(&ep->ep, req, rx_dma_count, DMA_RX_ONE_IRQ) != SUCCESS)
							DBG(KERN_ERR "USB_CLASS_MASS_STORAGE: Set DMA fail FIFO\n");
					} else
						DBG(KERN_ERR "DMA busy.. just queue req \n");
				} else {
					csr2 = UDC_READ8((MSB250X_USBCREG(csr_idx)) + 1);
					UDC_WRITE8((csr2 & ~RXCSR2_MODE1), (MSB250X_USBCREG(csr_idx)) + 1);
					if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
						ms_AllowAck(ep->bEndpointAddress);
				}
			}
		}
#endif    /* CONFIG_USB_MSB250X_DMA */
		else if ((ep->bEndpointAddress & USB_DIR_IN) != 0 && (!(ep_csr & MSB250X_UDC_TXCSR1_TXPKTRDY))
					&& msb250x_udc_write_fifo(ep, req))
		{
			TX_LOG("[USB]TXPACKET_NOT_READY_FIFO_fifo_write\n");
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
#else
			req = NULL;
#endif
		} else if (!(ep->bEndpointAddress & USB_DIR_IN)) {
			RX_MODE1_LOG("[USB]OUT token packet\n");
			if ((ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY) && (msb250x_udc_read_fifo(ep, req))) {
				RX_MODE1_LOG("[USB]MSB250X_UDC_RXCSR1_RXPKTRDY_fifo_read\n");
				req = NULL;
			}
		}
	}
	else {
		RX_MODE1_LOG("[USB]halted, return\n");
	}

	return req;
}

s8 msb250x_udc_schedule_done(struct msb250x_ep *ep)
{
	DBG_fn("%s\n", __func__);
	if (likely (!list_empty(&ep->queue))) {
		struct msb250x_request *req = NULL;
		req = list_entry(ep->queue.next, struct msb250x_request, queue);
		msb250x_udc_do_request(ep, req);
	}
	return 0;
}

void msb250x_udc_done(struct msb250x_ep *ep, struct msb250x_request *req, int status)
{
	unsigned halted = ep->halted;
	struct msb250x_udc *dev = NULL;

	DBG("done %d actral %d length %x\n", ep->num, req->req.actual, req->req.length);
	dev = ep->dev;
	if(req == NULL) {
		printk("[USB]REQ NULL\n");
		return;
	}
	if (ep->num)
		DBG_TR("complete %u\n", ep->num);

	list_del_init(&req->queue);
	if(!(list_empty(&req->queue)))
		printk("[USB]queue not empty!!\n");

	if (likely (req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	ep->halted = 1;
	spin_unlock(&dev->lock);
	req->req.complete(&ep->ep, &req->req);
	req->req.status = 0;//complete req
	spin_lock(&dev->lock);
	ep->halted = halted;

	return;
}

static int msb250x_udc_get_status(struct msb250x_udc *dev, struct usb_ctrlrequest *crq)
{
	u8 ep_num = crq->wIndex & 0x7F;
	u8 is_in = crq->wIndex & USB_DIR_IN;
	u16 csr_idx, status = 0;

	DBG_fn("%s\n", __func__);
	switch (crq->bRequestType & USB_RECIP_MASK) {
		case USB_RECIP_INTERFACE:
			break;
		case USB_RECIP_DEVICE:
			status = dev->devstatus;
			break;
		case USB_RECIP_ENDPOINT:
			if (ep_num > 7 || crq->wLength > 2)
				return 1;
			if (ep_num == 0) {
				status = UDC_READ8(MSB250X_USBCREG(0x102));
				status = status & MSB250X_UDC_CSR0_SENDSTALL;
			} else {
				if (is_in) {
					csr_idx = 0x102 + (0x10*ep_num);
					status = UDC_READ8(MSB250X_USBCREG(csr_idx));
					status = status & MSB250X_UDC_TXCSR1_SENDSTALL;
				} else {
					csr_idx = 0x106 + (0x10*ep_num);
					status = UDC_READ8(MSB250X_USBCREG(csr_idx));
					status = status & MSB250X_UDC_RXCSR1_SENDSTALL;
				}
			}
			status = status ? 1 : 0;
			break;
		default:
			return 1;
	}

	udelay(5);
	UDC_WRITE8(status & 0xFF, MSB250X_UDC_EP0_FIFO_ACCESS_L);
	UDC_WRITE8(status >> 8, MSB250X_UDC_EP0_FIFO_ACCESS_L);
	MSB250X_UDC_SET_EP0_DE_IN();

    return 0;
}

static void msb250x_udc_handle_ep0_idle(struct msb250x_udc *dev,
					struct msb250x_ep *ep,
					struct usb_ctrlrequest *crq,
					uintptr_t ep0csr)
{
	int len = 0, ret = 0 , tmp = 0;

	DBG_fn("%s\n", __func__);

	if (!(ep0csr & MSB250X_UDC_CSR0_RXPKTRDY))
		return;

	msb250x_udc_nuke(dev, ep, -EPROTO);
	len = msb250x_udc_read_fifo_ctl_req(crq);

	if (len != sizeof(*crq)) {
		printk("len:%x,crq:%x\n", len, (unsigned int)(sizeof(*crq)));
		printk(KERN_ERR "setup begin: fifo READ ERROR"
			" wanted %d bytes got %d. Stalling out...\n", (unsigned int)(sizeof(*crq)), len);
		MSB250X_UDC_SET_EP0_SS();
		return;
	}

	DBG("bRequest = %x bRequestType %x wLength = %d\n",
		crq->bRequest, crq->bRequestType, crq->wLength);

	dev->req_std = (crq->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD;
	dev->req_config = 0;
	dev->req_pending = 1;

	if (dev->req_std) {
		switch (crq->bRequest) {
			case USB_REQ_SET_CONFIGURATION:
				DBG("USB_REQ_SET_CONFIGURATION ... \n");
				if (crq->bRequestType == USB_RECIP_DEVICE)
					dev->req_config = 1;
				break;
			case USB_REQ_SET_INTERFACE:
				DBG("USB_REQ_SET_INTERFACE ... \n");
				if (crq->bRequestType == USB_RECIP_INTERFACE)
					dev->req_config = 1;
				break;
			case USB_REQ_SET_ADDRESS:
				DBG("USB_REQ_SET_ADDRESS ... \n");
				if (crq->bRequestType == USB_RECIP_DEVICE) {
					tmp = crq->wValue & 0x7F;
					dev->address = tmp;
					UDC_WRITE8(tmp, MSB250X_UDC_FADDR_REG);
					MSB250X_UDC_SET_EP0_DE_OUT();
					return;
				}
				break;
				case USB_REQ_GET_STATUS:
				DBG("USB_REQ_GET_STATUS ... \n");
				MSB250X_UDC_CLEAR_EP0_OPR();
				if (dev->req_std) {
					if (!msb250x_udc_get_status(dev, crq))
						return;
				}
				break;
			case USB_REQ_CLEAR_FEATURE:
				DBG("USB_REQ_CLEAR_FEATURE ... \n");
				MSB250X_UDC_CLEAR_EP0_OPR();
				if (crq->bRequestType != USB_RECIP_ENDPOINT)
					break;
				if (crq->wValue != USB_ENDPOINT_HALT || crq->wLength != 0)
					break;
				msb250x_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 0);
				MSB250X_UDC_SET_EP0_DE_OUT();
				return;
			case USB_REQ_SET_FEATURE:
				DBG("USB_REQ_SET_FEATURE ... \n");
				MSB250X_UDC_CLEAR_EP0_OPR();
				if(crq->bRequestType == USB_RECIP_DEVICE) {
					if(crq->wValue==0x02) {
						//USB20_TEST_MODE
						//nUsb20TestMode=crq->wIndex;
					}
				}
				if (crq->bRequestType != USB_RECIP_ENDPOINT)
					break;
				if (crq->wValue != USB_ENDPOINT_HALT || crq->wLength != 0)
					break;

				msb250x_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 1);
				MSB250X_UDC_SET_EP0_DE_OUT();
				return;
			default:
				MSB250X_UDC_CLEAR_EP0_OPR();
				break;
		}
	} 
	else
		MSB250X_UDC_CLEAR_EP0_OPR();

	if (crq->bRequestType & USB_DIR_IN)
		dev->ep0state = EP0_IN_DATA_PHASE;
	else
		dev->ep0state = EP0_OUT_DATA_PHASE;

	if (dev->driver && dev->driver->setup) {
		spin_unlock (&dev->lock);
		ret = dev->driver->setup(&dev->gadget, crq);
		spin_lock (&dev->lock);
	}
	else
		ret = -EINVAL;

	if (ret < 0) {
		if (dev->req_config) {
			DBG("config change %02x fail %d?\n", crq->bRequest, ret);
			return;
		}
		if (ret == -EOPNOTSUPP)
			DBG("Operation not supported\n");
		else
			DBG("dev->driver->setup failed. (%d)\n", ret);

		udelay(5);
		MSB250X_UDC_SET_EP0_SS();
		dev->ep0state = EP0_IDLE;
	}
	else if (dev->req_pending) {
		DBG("dev->req_pending... what now?\n");
		dev->req_pending = 0;
	}
}

static void msb250x_udc_handle_ep0(struct msb250x_udc *dev)
{
	struct msb250x_ep    *ep = &dev->ep[0];
	struct msb250x_request    *req = NULL;
	struct usb_ctrlrequest    crq;
	uintptr_t ep0csr = 0;
	u32 rxcnt = 0;

	DBG_fn("%s\n", __func__);

	if (list_empty(&ep->queue))
		req = NULL;
	else
		req = list_entry(ep->queue.next, struct msb250x_request, queue);

	ep0csr = UDC_READ8(MSB250X_USBCREG(0x102));
	rxcnt =	UDC_READ8(MSB250X_USBCREG(0x108));

	/* clear stall status */
	if (ep0csr & MSB250X_UDC_CSR0_SENTSTALL) {
		DBG("... clear SENT_STALL ...\n");
		msb250x_udc_nuke(dev, ep, -EPIPE);
		MSB250X_UDC_CLEAR_EP0_SST();
		dev->ep0state = EP0_IDLE;
	}

	/* clear setup end */
	if (ep0csr & MSB250X_UDC_CSR0_SETUPEND) {
		DBG("... serviced SETUP_END ...\n");
		msb250x_udc_nuke(dev, ep, 0);
		MSB250X_UDC_CLEAR_EP0_SE();
		dev->ep0state = EP0_IDLE;
	}

	switch (dev->ep0state) {
		case EP0_IDLE:
			msb250x_udc_handle_ep0_idle(dev, ep, &crq, ep0csr);
			break;
		case EP0_IN_DATA_PHASE:
			/* GET_DESCRIPTOR etc */
			DBG("EP0_IN_DATA_PHASE ... what now?\n");
			if (!(ep0csr & MSB250X_UDC_CSR0_TXPKTRDY) && req)
				msb250x_udc_write_fifo(ep, req);
			break;
		case EP0_OUT_DATA_PHASE:
			/* SET_DESCRIPTOR etc */
			DBG("EP0_OUT_DATA_PHASE ... what now?\n");
			if ((ep0csr & MSB250X_UDC_CSR0_RXPKTRDY) && req)
				msb250x_udc_read_fifo(ep,req);
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

static void msb250x_udc_handle_ep(struct msb250x_ep *ep)
{
	struct msb250x_request    *req = NULL;
	int is_in = 0, tx_left_flag=0;
	u16 csr_idx;
	u32 ep_csr1 = 0, idx = 0;

	DBG_fn("%s\n", __func__);
	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);

	if (likely (!list_empty(&ep->queue))) {
		TX_LOG("find ep%x req\n",ep->bEndpointAddress);
		req = list_entry(ep->queue.next, struct msb250x_request, queue);
	} else {
		TX_LOG("no find ep%x req in queue\n",ep->bEndpointAddress);
		req = NULL;
	}

	idx = ep->bEndpointAddress & 0x7F;
	is_in = ep->bEndpointAddress & USB_DIR_IN;

	if (is_in) {
		TX_LOG("[USB]TX_INTTRUPT\n");

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
		if (req == NULL)
			printk("[USB]TX_REQ NULL\n");
		else {
			if((req->req.length) - ((unsigned)(req->req.actual)) > 0) {
				msb250x_udc_write_fifo(ep, req);
				tx_left_flag = 1;
			}
			else
				msb250x_udc_done(ep, req, 0);
		}
#endif

		ep_csr1 = UDC_READ8(MSB250X_USBCREG(csr_idx));
		DBG("ep%01d write csr:%02x %d\n", idx, ep_csr1, req ? 1 : 0);

		if (ep_csr1 & MSB250X_UDC_TXCSR1_SENTSTALL) {
			DBG("tx st\n");
			UDC_WRITE8(ep_csr1 & ~MSB250X_UDC_TXCSR1_SENTSTALL, MSB250X_USBCREG(ep_csr1));
			return;
		}
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
		/* DMA mode */
		if(tx_left_flag == 0) {
			msb250x_udc_schedule_done(ep);
		}
#else
		/* FIFO mode */
		if(req != NULL) {
			msb250x_udc_do_request(ep, req);
		}
#endif
	}
	else {
		RX_MODE1_LOG("[USB]RX_INTTRUPT:%x\n",ep->bEndpointAddress);
		if((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
			ms_NAKEnable(ep->bEndpointAddress);

		ep_csr1 = UDC_READ8(MSB250X_USBCREG(csr_idx));
		DBG("ep%01d rd csr:%02x\n", idx, ep_csr1);
		if (ep_csr1 & MSB250X_UDC_RXCSR1_SENTSTALL) {
			UDC_WRITE8(ep_csr1 & ~MSB250X_UDC_RXCSR1_SENTSTALL, MSB250X_USBCREG(csr_idx));
			return;
		}
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
		/* DMA mode */
		if(req != NULL)
			RxHandler(ep,req);
		else
			RX_MODE1_LOG("req is null\n");
#else
		/* FIFO mode */
		if(req != NULL){
			msb250x_udc_do_request(ep, req);
		}
		else
			;/*printk("req is null\n");*/
#endif
	}
}

static void reset_gadget(struct msb250x_udc *dev, struct usb_gadget_driver *driver)
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
#if defined(TIMER_PATCH)
	struct msb250x_ep *ep;
#endif
	int usb_status = 0, usb_intrx_status = 0, usb_inttx_status = 0, pwr_reg = 0, i = 0;
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	u8 dma_intr, ch;
#endif

	spin_lock(&dev->lock);

	/* Read status registers, note also that all active interrupts are cleared
	when this register is read. */
	usb_status = UDC_READ8(MSB250X_UDC_INTRUSB_REG);
	UDC_WRITE8(usb_status, MSB250X_UDC_INTRUSB_REG);

	usb_intrx_status = UDC_READ16(MSB250X_UDC_INTRRX_REG);
	usb_inttx_status = UDC_READ16(MSB250X_UDC_INTRTX_REG);
	UDC_WRITE16(usb_intrx_status, MSB250X_UDC_INTRRX_REG);
	UDC_WRITE16(usb_inttx_status, MSB250X_UDC_INTRTX_REG);

	pwr_reg = UDC_READ8(MSB250X_UDC_PWR_REG);
	DBG("usbs=%02x, usb_intrxs=%02x, usb_inttxs=%02x pwr=%02x\n",
		usb_status, usb_intrx_status, usb_inttx_status, pwr_reg);

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	dma_intr = UDC_READ8(M_REG_DMA_INTR);
	UDC_WRITE8(dma_intr, M_REG_DMA_INTR);
	if (dma_intr) {
		for (i = 0; i < MAX_USB_DMA_CHANNEL; i++) {
			if (dma_intr & (1 << i)) {
				ch = i + 1;
				USB_DMA_IRQ_Handler(ch, dev);
			}
		}
	}
#endif

	if (usb_status & MSB250X_UDC_INTRUSB_RESET)
	{
		printk("#######======>>>>>hello_bus_reset\n");
		if (dev->driver) {
			spin_unlock(&dev->lock);
			reset_gadget(dev, dev->driver);
			spin_lock(&dev->lock);
		}

		// clear function addr
		UDC_WRITE8(0, MSB250X_UDC_FADDR_REG);
		dev->address = 0;
		dev->ep0state = EP0_IDLE;
		if (UDC_READ8(MSB250X_UDC_PWR_REG)&MSB250X_UDC_PWR_HS_MODE) {
			dev->gadget.speed = USB_SPEED_HIGH;
		} else {
			dev->gadget.speed = USB_SPEED_FULL;
		}
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
		if(using_dma && dev->DmaRxMode == DMA_RX_MODE1)
		{
			DBG("DMA RX Mode1 ReSetting \n");
			*(DMA_CNTL_REGISTER(MAX_USB_DMA_CHANNEL)) = USB_Read_DMA_Control(MAX_USB_DMA_CHANNEL) & 0xfe;
			UDC_WRITE16(0, MSB250X_UDC_DMA_MODE_CTL);
			//short_mode
			UDC_WRITE8((UDC_READ8(MSB250X_UDC_USB_CFG6_H) | 0x20), MSB250X_UDC_USB_CFG6_H);
			//clear all autonak setting
			UDC_WRITE16((UDC_READ16(MSB250X_UDC_EP_BULKOUT) & 0xfff0), MSB250X_UDC_EP_BULKOUT);
			UDC_WRITE8((UDC_READ8(MSB250X_UDC_DMA_MODE_CTL1) & 0xf0), MSB250X_UDC_DMA_MODE_CTL1);
		}
#endif
	}
	/* RESUME */
	if (usb_status & MSB250X_UDC_INTRUSB_RESUME) {
		if (dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->resume)
			dev->driver->resume(&dev->gadget);
	}
	/* SUSPEND */
	if (usb_status & MSB250X_UDC_INTRUSB_SUSPEND) {
		for(i = 1; i < MSB250X_ENDPOINTS; i++) {
			if(dev->ep[i].bEndpointAddress & USB_DIR_IN) {
				int csr_idx = (dev->ep[i].bEndpointAddress & 0x0f)*0x10 + 0x100;
				dev->ep[i].wMaxPacketSize = UDC_READ16(MSB250X_USBCREG(csr_idx));
			} else {
				int csr_idx = (dev->ep[i].bEndpointAddress & 0x0f)*0x10 + 0x104;
				dev->ep[i].wMaxPacketSize = UDC_READ16(MSB250X_USBCREG(csr_idx));
			}
#if defined(TIMER_PATCH)
			ep = &dev->ep[i];
			ms_stop_timer(ep);
			Release_DMA_Channel(ep->ch);
			ep->DmaRxMode1 = 0;
#endif
		}

		if (dev->gadget.speed != USB_SPEED_UNKNOWN && dev->driver && dev->driver->suspend) {
			DBG("call gadget->suspend\n");
			dev->driver->suspend(&dev->gadget);
		}
		dev->ep0state = EP0_IDLE;
	}

	if (usb_inttx_status & MSB250X_UDC_INTRTX_EP0)
		msb250x_udc_handle_ep0(dev);

	for (i = 1; i < MSB250X_ENDPOINTS; i++) {
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

	spin_unlock(&dev->lock);

	return IRQ_HANDLED;
}

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

static int msb250x_udc_ep_enable(struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct msb250x_udc *dev = NULL;
	struct msb250x_ep *ep = NULL;
	u16 csr_idx;
	u32 max = 0, tmp = 0, csr1 = 0 ,csr2 = 0, int_en_reg = 0;
	unsigned long flags;

	DBG_fn("%s\n", __func__);

	ep = to_msb250x_ep(_ep);

	if (!_ep || !desc || ep->desc
			|| _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max = le16_to_cpu(desc->wMaxPacketSize) & 0x1fff;

	spin_lock_irqsave(&ep->dev->lock,flags);
	_ep->maxpacket = max & 0x7ff;
	ep->desc = desc;
	ep->halted = 0;
	ep->bEndpointAddress = desc->bEndpointAddress;
	ep->bmAttributes = desc->bmAttributes;
	ep->wMaxPacketSize = desc->wMaxPacketSize;

	/* set type, direction, address; reset fifo counters */
	if (desc->bEndpointAddress & USB_DIR_IN) {
		csr1 = MSB250X_UDC_TXCSR1_FLUSHFIFO | MSB250X_UDC_TXCSR1_CLRDATAOTG;
		csr2 = MSB250X_UDC_TXCSR2_MODE;

		if((_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC)
			csr2 |= MSB250X_UDC_TXCSR2_ISOC;

		csr_idx = 0x100 + (0x10*ep->num);
		UDC_WRITE16(_ep->maxpacket , MSB250X_USBCREG(csr_idx));

		csr_idx = 0x102 + (0x10*ep->num);
		UDC_WRITE8(csr1, MSB250X_USBCREG(csr_idx));
		UDC_WRITE8(csr2, (MSB250X_USBCREG(csr_idx)) + 1);
		/* enable irqs */
		int_en_reg = UDC_READ16(MSB250X_UDC_INTRTXE_REG);
		UDC_WRITE16(int_en_reg | (1 << ep->num), MSB250X_UDC_INTRTXE_REG);
	} else {
		/* enable the enpoint direction as Rx */
		csr1 = MSB250X_UDC_RXCSR1_FLUSHFIFO | MSB250X_UDC_RXCSR1_CLRDATATOG;
		csr2 = 0;

		if((_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_ISOC)
			csr2 |= MSB250X_UDC_RXCSR2_ISOC;
		else
			ms_autonak_set(ep->bEndpointAddress);

		csr_idx = 0x104 + (0x10*ep->num);
		UDC_WRITE16(_ep->maxpacket , MSB250X_USBCREG(csr_idx));

		csr_idx = 0x106 + (0x10*ep->num);
		UDC_WRITE8(csr1, MSB250X_USBCREG(csr_idx));
		UDC_WRITE8(csr2, (MSB250X_USBCREG(csr_idx)) + 1);

		//if ep is isoc mode, disable AUTONAK
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
		if(using_dma && (dev->DmaRxMode == DMA_RX_MODE1) && (ep->bmAttributes & USB_ENDPOINT_XFER_ISOC))
			ms_autonak_clear(ep->bEndpointAddress);
#else
		if((ep->bmAttributes & USB_ENDPOINT_XFER_ISOC))
			ms_autonak_clear(ep->bEndpointAddress);
#endif
		/* enable irqs */
		int_en_reg = UDC_READ16(MSB250X_UDC_INTRRXE_REG);
		UDC_WRITE16(int_en_reg | (1 << ep->num), MSB250X_UDC_INTRRXE_REG);
	}

	tmp = desc->bEndpointAddress;
	spin_unlock_irqrestore(&ep->dev->lock,flags);
	msb250x_udc_schedule_done(ep);

    return 0;
}

static int msb250x_udc_ep_disable(struct usb_ep *_ep)
{
	struct msb250x_ep *ep = to_msb250x_ep(_ep);
	struct msb250x_udc	*dev;
	u32 int_en_reg = 0;
	unsigned long flags;

	DBG_fn("%s\n", __func__);

	if (!_ep || !ep->desc) {
		DBG("%s not enabled\n",_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	dev = ep->dev;
	DBG("Entered %s\n", __FUNCTION__);
	DBG("ep_disable: %s\n", _ep->name);

	ep->desc = NULL;
	ep->halted = 1;

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	Release_DMA_Channel(ep->ch);
#if defined(TIMER_PATCH)
	ms_stop_timer(ep);
#endif
#endif
	spin_lock_irqsave(&dev->lock,flags);
	msb250x_udc_nuke(ep->dev, ep, -ESHUTDOWN);
	spin_unlock_irqrestore(&dev->lock,flags);

	if(((ep->bEndpointAddress & 0x80) == 0) && (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC)
		ms_autonak_clear(ep->bEndpointAddress);

	/* disable irqs */
	if(ep->bEndpointAddress & USB_DIR_IN) {
		int_en_reg = UDC_READ16(MSB250X_UDC_INTRTXE_REG);
		UDC_WRITE16(int_en_reg & ~(1<<ep->num), MSB250X_UDC_INTRTXE_REG);
	} else {
		int_en_reg = UDC_READ16(MSB250X_UDC_INTRRXE_REG);
		UDC_WRITE16(int_en_reg & ~(1<<ep->num), MSB250X_UDC_INTRRXE_REG);
	}

	return 0;
}

static struct usb_request *msb250x_udc_alloc_request(struct usb_ep *_ep, gfp_t mem_flags)
{
	struct msb250x_request *req = NULL;

	DBG("Entered %s(%p,%d)\n", __FUNCTION__, _ep, mem_flags);
	if (!_ep)
		return NULL;

	req = kzalloc (sizeof(struct msb250x_request), mem_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD (&req->queue);
	return &req->req;
}

static void msb250x_udc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct msb250x_ep *ep = to_msb250x_ep(_ep);
	struct msb250x_request    *req = to_msb250x_req(_req);
    int is_in = 0;
top:
	DBG("Entered %s(%p,%p)\n", __FUNCTION__, _ep, _req);

	if (!ep || !_req || (!ep->desc && _ep->name != ep0name))
		return;

	WARN_ON (!list_empty (&req->queue));
	
    is_in = ep->bEndpointAddress & USB_DIR_IN;
    //printk("free_status:%x, dir:%x\n", req->req.status, is_in);
    if((req->req.status != 0) && (is_in))
	{
		printk("still working\n");
		goto top;
	}
	else
		kfree(req);
}

static int msb250x_udc_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct msb250x_request    *req = to_msb250x_req(_req);
	struct msb250x_ep *ep = to_msb250x_ep(_ep);
	struct msb250x_udc     *dev = NULL;
	unsigned long        flags;

	DBG_fn("%s\n", __func__);

    if (unlikely (!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		DBG("%s: invalid args\n", __FUNCTION__);
		return -EINVAL;
	}

	dev = ep->dev;
	if (unlikely (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

	spin_lock_irqsave(&dev->lock,flags);

	if (unlikely(!_req || !_req->complete || !_req->buf || !list_empty(&req->queue))) {
		if (!_req)
			DBG("%s: 1 X X X\n", __FUNCTION__);
		else
			DBG("%s: 0 %01d %01d %01d\n", __FUNCTION__, !_req->complete,!_req->buf, !list_empty(&req->queue));

		spin_unlock_irqrestore(&dev->lock,flags);
		return -EINVAL;
	}

	req->req.status = -EINPROGRESS;
	_req->status = -EINPROGRESS;
	_req->actual = 0;
	RX_MODE1_LOG("QUEUE ep%x len %x buf:%p\n", ep->bEndpointAddress, _req->length,_req->buf);

	if(list_empty(&ep->queue))
		req = msb250x_udc_do_request(ep, req);

	/* pio or dma irq handler will advance the queue. */
	if (req != NULL) {
		list_add_tail(&req->queue, &ep->queue);
		TX_LOG("[USB]add_QUEUE:%x\n",ep->bEndpointAddress);
	}

	spin_unlock_irqrestore(&dev->lock,flags);
	DBG("%s ok and dev->ep0state=%d \n", __FUNCTION__, dev->ep0state);

	return 0;
}

static int msb250x_udc_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct msb250x_ep *ep = to_msb250x_ep(_ep);
	struct msb250x_udc	*dev;
	struct msb250x_request    *req = NULL;
	int retval = -EINVAL;
	unsigned long flags;

	DBG("Entered %s(%p,%p)\n", __FUNCTION__, _ep, _req);

	dev = ep->dev;
	if (!sg_udc_controller->driver)
		return -ESHUTDOWN;

	if(!_ep || !_req)
		return retval;

	spin_lock_irqsave(&dev->lock,flags);
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init (&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}

    if (retval == 0) {
		DBG("dequeued req %p from %s, len %d buf %p\n", req, _ep->name, _req->length, _req->buf);
		msb250x_udc_done(ep, req, -ECONNRESET);
	}
	spin_unlock_irqrestore(&dev->lock,flags);

	return retval;
}

int msb250x_udc_set_halt(struct usb_ep *_ep, int value)
{
	u16 csr_idx;
    u32 ep_csr = 0;
    struct msb250x_ep *ep = to_msb250x_ep(_ep);
    struct msb250x_request    *preq = NULL;

	DBG_fn("%s\n", __func__);

    if (unlikely (!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		DBG("%s: inval 2\n", __FUNCTION__);
		return -EINVAL;
	}

	if ((!list_empty(&ep->queue)))
		preq = list_entry(ep->queue.next, struct msb250x_request, queue);
	else
		preq = NULL;

	DBG("Entered %s\n", __FUNCTION__);

	csr_idx = GetCSR_EpIdx(ep->bEndpointAddress);
	ep->halted = value ? 1 : 0;

	if (ep->bEndpointAddress== 0)
		MSB250X_UDC_SET_EP0_SS();
	else {
		ep_csr = UDC_READ8(MSB250X_USBCREG(csr_idx));
		if ((ep->bEndpointAddress & USB_DIR_IN) != 0) {
			if (value) {
				if (ep_csr & MSB250X_UDC_TXCSR1_FIFONOEMPTY) {
					ep->halted = 0;
					return -EAGAIN;
				}
				UDC_WRITE8(ep_csr | MSB250X_UDC_TXCSR1_SENDSTALL, MSB250X_USBCREG(csr_idx));
			} else {
				UDC_WRITE8(MSB250X_UDC_TXCSR1_CLRDATAOTG, MSB250X_USBCREG(csr_idx));
				UDC_WRITE8(0,MSB250X_USBCREG(csr_idx));
				if (!(ep_csr & MSB250X_UDC_TXCSR1_TXPKTRDY) && preq)
					msb250x_udc_write_fifo(ep, preq);
			}
		}
		else {
			if (value)
				UDC_WRITE8(ep_csr | MSB250X_UDC_RXCSR1_SENDSTALL, MSB250X_USBCREG(csr_idx));
			else {
					ep_csr &= ~MSB250X_UDC_RXCSR1_SENDSTALL;
					UDC_WRITE8(ep_csr, MSB250X_USBCREG(csr_idx));
					ep_csr |= MSB250X_UDC_RXCSR1_CLRDATATOG;
					UDC_WRITE8(ep_csr, MSB250X_USBCREG(csr_idx));
				if ((ep_csr & MSB250X_UDC_RXCSR1_RXPKTRDY) && preq)
					msb250x_udc_read_fifo(ep, preq);
			}
		}
	}

	return 0;
}

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

static void msb250x_udc_pullup_i(int is_on)
{
	printk("[UDC]pullup_i:0x%d\n",is_on);
	if (is_on) {
		UTMI_REG_WRITE8(0x06*2, UTMI_REG_READ8(0x06*2) | BIT0 | BIT1);	//reset UTMI
		UTMI_REG_WRITE8(0x06*2, UTMI_REG_READ8(0x06*2) & ~(BIT1|BIT0));	//clear reset UTMI
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_PWR_REG) | MSB250X_UDC_PWR_HS_EN, MSB250X_UDC_PWR_REG);
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_PWR_REG) | MSB250X_UDC_PWR_SOFT_CONN, MSB250X_UDC_PWR_REG);
	} else {
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_PWR_REG) & ~MSB250X_UDC_PWR_HS_EN, MSB250X_UDC_PWR_REG);
		UDC_WRITE8(UDC_READ8(MSB250X_UDC_PWR_REG) & ~MSB250X_UDC_PWR_SOFT_CONN, MSB250X_UDC_PWR_REG);
	}
}

static int msb250x_udc_wakeup(struct usb_gadget *_gadget)
{
	DBG("Entered %s\n", __FUNCTION__);
	return 0;
}

static int msb250x_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct msb250x_udc *udc = to_msb250x_udc(gadget);

	DBG_fn("%s\n", __func__);
	if (udc->driver && udc->driver->disconnect)
		udc->driver->disconnect(&udc->gadget);

	msb250x_udc_pullup_i(is_on);

	return 0;
}

static int msb250x_udc_start(struct usb_gadget *g, struct usb_gadget_driver *driver)
{
	struct msb250x_udc *udc = sg_udc_controller;

	printk("==%s==> UDC start\n", DRIVER_VERSION_UDC);
	if (!g) {
		printk("[USB]ENODEV\n");
		return -ENODEV;
	}

	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;
	udc->conn_chg = 0;

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	udc->DmaRxMode = DMA_RX_MODE1;
	using_dma = 1;
#endif
	mdelay(1);
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

static const struct usb_gadget_ops sg_msb250x_gadget_ops =
{
	//.get_frame = msb250x_udc_get_frame,
	.wakeup = msb250x_udc_wakeup,
	//.set_selfpowered = msb250x_udc_set_selfpowered,
	.pullup = msb250x_udc_pullup,
	//.vbus_session = msb250x_udc_vbus_session,
	//.vbus_draw    = msb250x_vbus_draw,
	.udc_start = msb250x_udc_start,
	//.match_ep	= msb250x_udc_match_ep,
	.udc_stop = msb250x_udc_stop,
};

void msb250x_udc_disable(struct msb250x_udc *dev)
{
	DBG_fn("%s\n", __func__);
	/* Disable all interrupts */
	UDC_WRITE8(0x00, MSB250X_UDC_INTRUSBE_REG);
	UDC_WRITE16(0x00, MSB250X_UDC_INTRTXE_REG);
	UDC_WRITE16(0x00, MSB250X_UDC_INTRRXE_REG);

	/* Clear the interrupt registers */
	/* All active interrupts will be cleared when this register is read */
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_INTRUSB_REG), MSB250X_UDC_INTRUSB_REG);
	UDC_WRITE16(UDC_READ16(MSB250X_UDC_INTRTX_REG), MSB250X_UDC_INTRTX_REG);
	UDC_WRITE16(UDC_READ16(MSB250X_UDC_INTRRX_REG), MSB250X_UDC_INTRRX_REG);

	/* Good bye, cruel world */
	msb250x_udc_pullup_i(0);
	/* Set speed to unknown */
	dev->gadget.speed = USB_SPEED_UNKNOWN;

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	free_dma_channels=0x7f;
#endif
}

static void msb250x_udc_reinit(struct msb250x_udc *dev)
{
	u32 i = 0;

	DBG("Entered %s\n", __FUNCTION__);
	/* device/ep0 records init */
	INIT_LIST_HEAD (&dev->gadget.ep_list);
	INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);

	dev->gadget.ep0 = &dev->ep [0].ep;
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	for (i = 0; i < MSB250X_ENDPOINTS; i++)
    {
		struct msb250x_ep *ep = &dev->ep[i];

		ep->num = i;
		list_add_tail (&ep->ep.ep_list, &dev->gadget.ep_list);
		INIT_LIST_HEAD (&ep->queue);

		usb_ep_set_maxpacket_limit(&ep->ep, 1024);

		if (i == 0)
			ep->ep.caps.type_control = true;
		else {
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_int = true;
		}

		if(dev->ep[i].bEndpointAddress & 0x80) {
			ep->ep.caps.dir_in = true;
			ep->ep.caps.dir_out = false;
		}
		else {
			ep->ep.caps.dir_in = false;
			ep->ep.caps.dir_out = true;
		}
	}
	usb_ep_set_maxpacket_limit(&dev->ep[0].ep, 64);
	list_del_init (&dev->ep[0].ep.ep_list);
}

void do_soft_connect(void)
{
	DBG_fn("%s\n", __func__);
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_PWR_REG) | MSB250X_UDC_PWR_SOFT_CONN, MSB250X_UDC_PWR_REG);
}

static ssize_t get_connect_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 RegData = 0;

	if ((UDC_READ8(MSB250X_UDC_PWR_REG) & MSB250X_UDC_PWR_SOFT_CONN))
		RegData = 1;

	return scnprintf(buf, PAGE_SIZE, "%d\n", RegData);
}

static ssize_t set_connect_status(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		printk("[UDC]device disconnected\n");
		msb250x_udc_pullup_i(0);
	} else if (buf[0] == '1') {
		printk("[UDC]device connected\n");
		msb250x_udc_pullup_i(1);
	} else {
		printk("invalid input\n");
		return -1;
	}

	return count;
}
static DEVICE_ATTR(software_connect, S_IRUGO | S_IWUSR, get_connect_status, set_connect_status);


#if defined(ENABLE_USB_NEW_MIU_SLE)
void MIU_select_setting_udc(uintptr_t USBC_base)
{
	int miu_sel_reg;

	miu_sel_reg = readb((void*)(USBC_base + 0x19*2-1));
	//printk(KERN_CRIT"UDC USBC base %x miu sel reg:%x\n",USBC_base,miu_sel_reg);
	if (miu_sel_reg & 1)
		return;

	printk("[UDC] config miu select [%x] [%x] [%x] ][%x]\n", USB_MIU_SEL0, USB_MIU_SEL1, USB_MIU_SEL2, USB_MIU_SEL3);
	writeb(USB_MIU_SEL0, (void*)(USBC_base + 0x14*2));	//Setting MIU0 segment
	writeb(USB_MIU_SEL1, (void*)(USBC_base + 0x16*2));	//Setting MIU1 segment
	writeb(USB_MIU_SEL2, (void*)(USBC_base + 0x17*2-1));	//Setting MIU2 segment
	writeb(USB_MIU_SEL3, (void*)(USBC_base + 0x18*2));	//Setting MIU3 segment
	writeb(readb((void*)(USBC_base + 0x19*2-1)) | 0x1, (void*)(USBC_base + 0x19*2-1));	//Enable miu partition mechanism
}
#endif

void msb250x_udc_enable(struct msb250x_udc *dev)
{
	struct udc_utmi_param *u_param;
	u8 *eye_param;

	u_param = &dev->utmi_param;
	eye_param = u_param->eye_param;

	printk("+USBInit\r\n");
#if defined(ENABLE_USB_NEW_MIU_SLE)
	MIU_select_setting_udc(USBC_BASE_ADDR);
#endif

#if IS_ENABLED(CONFIG_USB_MS_OTG)
	// let OTG driver to handle the UTMI switch control
#else
	// Enable OTG controller
	USBC_REG_WRITE8(0x02*2, (USBC_REG_READ8(0x02*2) & ~(BIT0|BIT1)) | (BIT1));
#endif

	/* Init UTMI squelch level setting befor CA */
	writeb((readb((void*)(UTMI_BASE_ADDR+0x2a*2)) & 0xF0) | (UTMI_DISCON_LEVEL_2A & 0x0F), (void*)(UTMI_BASE_ADDR+0x2a*2));
	printk("[UDC] init squelch level 0x%x before CA\n", readb((void*)(UTMI_BASE_ADDR+0x2a*2)));

#if defined(ENABLE_MULTIPLE_SRAM_ACCESS_ECO)
	printk("[UDC]Multiple SRAM access on\n");
#endif

	UTMI_REG_WRITE8(0x3C*2, UTMI_REG_READ8(0x3C*2) | 0x1); // set CA_START as 1
	mdelay(10);
	UTMI_REG_WRITE8(0x3C*2, UTMI_REG_READ8(0x3C*2) & ~0x01); // release CA_START
	while ((UTMI_REG_READ8(0x3C*2) & 0x02) == 0);        // polling bit <1> (CA_END)

	// Reset OTG controllers(0, USBC_REG_READ8(0)|(BIT3 | BIT2));
	USBC_REG_WRITE8(0, USBC_REG_READ8(0)|(BIT3 | BIT2));

	USBC_REG_WRITE8(0, (USBC_REG_READ8(0) & ~(BIT2)) | BIT6);

	printk("+UTMI\n");
	UTMI_REG_WRITE8(0x06*2, (UTMI_REG_READ8(0x06*2) & 0x9F) | 0x40); //reg_tx_force_hs_current_enable
	UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) | 0x28); //Disconnect window select
	UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) & 0xef); //Disconnect window select
	UTMI_REG_WRITE8(0x07*2-1, UTMI_REG_READ8(0x07*2-1) & 0xfd); //Disable improved CDR
	UTMI_REG_WRITE8(0x09*2-1, UTMI_REG_READ8(0x09*2-1) | 0x81);  // UTMI RX anti-dead-loc, ISI effect improvement
	UTMI_REG_WRITE8(0x15*2-1, UTMI_REG_READ8(0x15*2-1) | 0x20);  // Chirp signal source select
	UTMI_REG_WRITE8(0x0b*2-1, UTMI_REG_READ8(0x0b*2-1) | 0x80);  // set reg_ck_inv_reserved[6] to solve timing problem

	UTMI_REG_WRITE8(0x2c*2,   eye_param[0]);
	UTMI_REG_WRITE8(0x2d*2-1, eye_param[1]);
	UTMI_REG_WRITE8(0x2e*2,   eye_param[2]);
	UTMI_REG_WRITE8(0x2f*2-1, eye_param[3]);
	printk("-UTMI\n");

#if IS_ENABLED(CONFIG_USB_MS_OTG)
	// let OTG driver to handle the UTMI switch control
#else
	// 2'b10: OTG enable
	USBC_REG_WRITE8(0x02*2, (USBC_REG_READ8(0x02*2)& ~(BIT0|BIT1)) | (BIT1));
#endif
	/* reset OTG */
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_USB_CFG0_L) & 0xFE, MSB250X_UDC_USB_CFG0_L);
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_USB_CFG0_L) | 0x01, MSB250X_UDC_USB_CFG0_L);

	//endpoint arbiter setting
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_USB_CFG7_L) & ~0x1, MSB250X_UDC_USB_CFG7_L);
	UDC_WRITE16(UDC_READ16(MSB250X_UDC_USB_CFG6_L) | 0x8000, MSB250X_UDC_USB_CFG6_L);
	UDC_WRITE16(UDC_READ16(MSB250X_UDC_USB_CFG6_L) & ~0x1100, MSB250X_UDC_USB_CFG6_L);
	UDC_WRITE16(UDC_READ16(MSB250X_UDC_USB_CFG6_L) | 0x4000, MSB250X_UDC_USB_CFG6_L);

	// Set FAddr to 0
	UDC_WRITE8(0, MSB250X_UDC_FADDR_REG);
	// Set Index to 0
	UDC_WRITE8(0, MSB250X_UDC_INDEX_REG);
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_USB_CFG6_H) | 0x08, MSB250X_UDC_USB_CFG6_H);
	UDC_WRITE8(UDC_READ8(MSB250X_UDC_USB_CFG6_H) | 0x40, MSB250X_UDC_USB_CFG6_H);

	printk("HIGH SPEED\n");
	UDC_WRITE8((UDC_READ8(MSB250X_UDC_PWR_REG) & ~MSB250X_UDC_PWR_ENSUSPEND) | MSB250X_UDC_PWR_HS_EN,
					MSB250X_UDC_PWR_REG);

	UDC_WRITE8(0, MSB250X_UDC_DEVCTL_REG);

	// Flush the next packet to be transmitted/ read from the endpoint 0 FIFO
	UDC_WRITE16(UDC_READ16(MSB250X_UDC_CSR0_REG) | MSB250X_UDC_CSR0_FLUSHFIFO, MSB250X_UDC_CSR0_REG);

	// Flush the latest packet from the endpoint Tx FIFO
	UDC_WRITE8(1, MSB250X_UDC_INDEX_REG);

	UDC_WRITE16(UDC_READ16(MSB250X_UDC_TXCSR_REG) | MSB250X_UDC_TXCSR1_FLUSHFIFO, MSB250X_UDC_TXCSR_REG);

	// Flush the next packet to be read from the endpoint Rx FIFO
	UDC_WRITE8(2, MSB250X_UDC_INDEX_REG);

	UDC_WRITE16(UDC_READ16(MSB250X_UDC_RXCSR_REG) | MSB250X_UDC_RXCSR1_FLUSHFIFO, MSB250X_UDC_RXCSR_REG);

	UDC_WRITE8(0, MSB250X_UDC_INDEX_REG);

	// Clear all control/status registers
	UDC_WRITE16(0, MSB250X_UDC_CSR0_REG);
	UDC_WRITE8(1, MSB250X_UDC_INDEX_REG);
	UDC_WRITE16(0, MSB250X_UDC_TXCSR_REG);
	UDC_WRITE8(2, MSB250X_UDC_INDEX_REG);
	UDC_WRITE16(0, MSB250X_UDC_RXCSR_REG);

	UDC_WRITE8(0, MSB250X_UDC_INDEX_REG);

	// Enable all endpoint interrupts
	UDC_WRITE8(0xF7, MSB250X_UDC_INTRUSBE_REG);
	UDC_WRITE16(0xFF, MSB250X_UDC_INTRTXE_REG);
	UDC_WRITE16(0xFF, MSB250X_UDC_INTRRXE_REG);
	UDC_READ8(MSB250X_UDC_INTRUSB_REG);
	UDC_READ16(MSB250X_UDC_INTRTX_REG);
	UDC_READ16(MSB250X_UDC_INTRRX_REG);
}


void usb_probe_driver(struct usb_gadget_driver *driver)
{
	struct msb250x_udc *udc = sg_udc_controller;

	printk("msb250x_udc--4.9v\n");
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	udc->DmaRxMode = DMA_RX_MODE1;
	using_dma = 1;
#endif
	mdelay(1);
	printk("end probe_driver\n");
}

static struct msb250x_udc sg_udc_config =
{
	.gadget = {
		.ops = &sg_msb250x_gadget_ops,
		.ep0 = &sg_udc_config.ep[0].ep,
		.name = sg_gadget_name,
		.dev = {
			//.bus_id = "gadget",
			.init_name = "gadget",
			//.release	= nop_release,
		},
	},
	.ep[0] = {
		.num = 0,
		.ep = {
			.name = ep0name,
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = EP0_FIFO_SIZE,
		},
		.dev = &sg_udc_config,
	},
	.ep[1] = {
		.num = 1,
		.ep = {
			.name = "ep1in-bulk",
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = 512,
		},
		.dev= &sg_udc_config,
		.fifo_size    = 512,
		.bEndpointAddress = USB_DIR_IN | 1,
		.bmAttributes    = USB_ENDPOINT_XFER_BULK,
	},
	.ep[2] = {
		.num = 2,
        .ep = {
			.name = "ep2out-bulk",
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = 512,
		},
		.dev = &sg_udc_config,
		.fifo_size    = 512,
		.bEndpointAddress = USB_DIR_OUT | 2,
		.bmAttributes    = USB_ENDPOINT_XFER_BULK,
	},
	.ep[3] = {
		.num = 3,
        .ep = {
			.name = "ep3in-int",
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = 512,
		},
		.dev = &sg_udc_config,
		.fifo_size    = 512,
		.bEndpointAddress = USB_DIR_IN | 3,
		.bmAttributes    = USB_ENDPOINT_XFER_INT,
	},
	.ep[4] = {
		.num = 4,
		.ep = {
			.name = "ep4out-bulk",
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = 512,
		},
		.dev = &sg_udc_config,
		.fifo_size    = 512,
		.bEndpointAddress = USB_DIR_OUT | 4,
		.bmAttributes    = USB_ENDPOINT_XFER_BULK,
	},
	.ep[5] = {
		.num = 5,
        .ep = {
			.name = "ep5in-bulk",
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = 512,
		},
		.dev= &sg_udc_config,
		.fifo_size    = 512,
		.bEndpointAddress = USB_DIR_IN | 5,
		.bmAttributes    = USB_ENDPOINT_XFER_BULK,
	},
	.ep[6] = {
		.num = 6,
		.ep = {
			.name = "ep6out-bulk",
			.ops = &sg_msb250x_ep_ops,
			.maxpacket = 512,
		},
		.dev = &sg_udc_config,
		.fifo_size    = 512,
		.bEndpointAddress = USB_DIR_OUT | 6,
		.bmAttributes    = USB_ENDPOINT_XFER_BULK,
	},
	.ep[7] = {
		.num = 7,
        .ep = {
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

int msb250x_udc_release(struct inode *inode, struct file *filp)
{
	DBG("release msb250x udc.\n");
	return 0;
}

struct file_operations msb250x_udc_fops = {
	.owner =    THIS_MODULE,
	//.ioctl =    msb250x_udc_ioctl,
	//.open =     msb250x_udc_open,
	.release =  msb250x_udc_release,
};

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
	int ret,irq=-1, retval = 0;

	DBG_fn("%s\n", __func__);

	spin_lock_init (&udc->lock);
	init_waitqueue_head(&(udc->event_q));
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;
	udc->pdev = pdev;
	udc->enabled = 0;
	udc->gadget.max_speed = USB_SPEED_HIGH;

	platform_set_drvdata(pdev, udc);
	msb250x_udc_disable(udc);
	msb250x_udc_reinit(udc);
	udc->active_suspend = 0;

#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
	using_dma = 1;
#endif

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
#if !defined(ENABLE_IRQ_REMAP)
	if(irq <= 0) {
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
	retval = request_irq(irq, msb250x_udc_irq, 0, sg_gadget_name, udc);
	if (retval != 0) {
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
	if(ret) {
		printk("error probe\n");
		while(1);
	}

	if (of_property_read_u8_array(pdev->dev.of_node, "utmi,eye-param",
				udc->utmi_param.eye_param, 4)) {
		udc->utmi_param.eye_param[0] = UTMI_EYE_SETTING_2C;
		udc->utmi_param.eye_param[1] = UTMI_EYE_SETTING_2D;
		udc->utmi_param.eye_param[2] = UTMI_EYE_SETTING_2E;
		udc->utmi_param.eye_param[3] = UTMI_EYE_SETTING_2F;
	}


	msb250x_udc_enable(udc);
#if IS_ENABLED(CONFIG_USB_MSB250X_DMA)
#if defined(TIMER_PATCH)
	ms_init_timer();
#endif
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

	/* software_connect create file */
	retval = device_create_file(&pdev->dev, &dev_attr_software_connect);
	if (retval < 0) {
		dev_dbg(&pdev->dev,
			"[UDC] Can't register sysfs attr file - software_connect: %d\n", retval);
	}

	return retval;
}

static int msb250x_udc_remove(struct platform_device *pdev)
{
	struct msb250x_udc *udc = platform_get_drvdata(pdev);
	dev_t devno = MKDEV(msb250x_udc_major, msb250x_udc_minor);
	int i, irq;

	DBG_fn("%s\n", __func__);

	if (udc->driver)
		return -EBUSY;

	/* software_connect function remove file */
	device_remove_file(&udc->pdev->dev, &dev_attr_software_connect);
	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);

	if(udc->got_irq) {
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
		device_destroy(msb250x_udc_class, devno);

	return 0;
}

#ifdef CONFIG_PM
static int msb250x_udc_suspend(struct platform_device *pdev, pm_message_t message)
{
	struct msb250x_udc *udc = platform_get_drvdata(pdev);

	printk("UDC suspend....\n");
	//backup M_Power status
	udc->reg_power = UDC_READ8(MSB250X_UDC_PWR_REG);
	// disable udc
	msb250x_udc_disable(udc);
	// mark suspend state
	udc->active_suspend = 1;

	return 0;
}

int msb250x_udc_resume(struct platform_device *pdev)
{
	struct msb250x_udc *udc = platform_get_drvdata(pdev);

	printk("UDC resume....\n");
	if (udc->active_suspend) {
		if (udc->driver) {
			msb250x_udc_enable(udc);
			//restore M_Power status
			UDC_WRITE8(udc->reg_power, MSB250X_UDC_PWR_REG);
			mdelay(1);
		}
		udc->active_suspend = 0;
	}

	return 0;
}
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
static int msb250x_udc_suspend_wrap(struct device *dev)
{
	struct platform_device *pdev;

	if (WARN_ON(!dev))
		return -ENODEV;

	pdev = to_platform_device(dev);

	if (waitfor.stage1_s_wait)
		wait_for_completion(&(waitfor.stage1_s_wait->power.completion));

	return msb250x_udc_suspend(pdev, dev->power.power_state);
}

static int msb250x_udc_resume_wrap(struct device *dev)
{
	struct platform_device *pdev;

	if (WARN_ON(!dev))
		return -ENODEV;

	pdev = to_platform_device(dev);

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
	.driver = {
		.name = "msb250x_udc",
		.owner = THIS_MODULE,
		.of_match_table = udc_of_match,
#ifdef CONFIG_MP_MSTAR_STR_OF_ORDER
		.pm = &msb250x_udc_pm_ops,
#endif
	},
};
#endif

static int __init msb250x_udc_init(void)
{
	int retval = 0;
	DBG("Entered %s: gadget_name=%s version=%s\n", __FUNCTION__, sg_gadget_name, DRIVER_VERSION);
#if defined(CONFIG_OF)
	retval= udc_init();
	return retval;
#else
	ms_udc_probe_usb_driver=usb_probe_driver;
	platform_device_register(&ms_udc_device);
	return platform_driver_register(&msb250x_udc_driver);
#endif
}

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
module_init(msb250x_udc_init);
module_exit(msb250x_udc_exit);
MODULE_AUTHOR(DRIVER_AUTHOR_UDC);
MODULE_DESCRIPTION(DRIVER_DESC_UDC);
MODULE_VERSION(DRIVER_VERSION_UDC);
MODULE_LICENSE("GPL");
