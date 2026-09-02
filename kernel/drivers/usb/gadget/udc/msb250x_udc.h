#ifndef _MSB250X_UDC_H
#define _MSB250X_UDC_H

/*
 * Include Files
 */
#include <linux/cdev.h>
#include <linux/stddef.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <udc-mstar-41929.h>

#define DRIVER_DESC_UDC		"MSB250x USB Device Controller Gadget"
#define DRIVER_VERSION_UDC	"20200110"
#define DRIVER_AUTHOR_UDC	"mstarsemi.com"

/* #define RX_mode1_log */
/* #define TX_log */

#if defined(RX_mode1_log)
#define RX_MODE1_LOG(x...)	printk(KERN_INFO x)
#else
#define RX_MODE1_LOG(X...)
#endif
#if defined(TX_log)
#define TX_LOG(x...)	printk(KERN_INFO x)
#else
#define TX_LOG(x...)
#endif

#define UDC_READ8(x) 			readb((void *)(x))
#define UDC_WRITE8(v, x) 		writeb((v), (void *)(x))
#define UDC_READ16(x) 			readw((void *)(x))
#define UDC_WRITE16(v, x) 		writew((v), (void *)(x))

#define UTMI_REG_READ8(r)		readb((void *)(UTMI_BASE_ADDR + (r)))
#define UTMI_REG_READ16(r)		readw((void *)(UTMI_BASE_ADDR + (r)))
#define UTMI_REG_WRITE8(r, v)	writeb(v, (void *)(UTMI_BASE_ADDR + r))
#define UTMI_REG_WRITE16(r, v)	writew(v, (void *)(UTMI_BASE_ADDR + r))

#define USBC_REG_READ8(r)		readb((void *)(USBC_BASE_ADDR + (r)))
#define USBC_REG_READ16(r)		readw((void *)(USBC_BASE_ADDR + (r)))
#define USBC_REG_WRITE8(r, v)	writeb(v, (void *)(USBC_BASE_ADDR + r))
#define USBC_REG_WRITE16(r, v)	writew(v, (void *)(USBC_BASE_ADDR + r))

/*
 * Constants
 */
#define OffShift	1
#define SUCCESS		true
#define FAIL		false

#define EP0_FIFO_SIZE 64

#define MSB250X_UDC_MAJOR 	0  	/* dynamic major by default */
#define MSB250X_UDC_NR_DEVS 1	/* msb250x_udc0 */

#if defined(TIMER_PATCH)
#define TIMEOUT_MS 1000
#endif

/*
 * Register definitions
 */

/* new mode1 in Peripheral mode */
#define M_Mode1_P_BulkOut_EP	0x0002
#define M_Mode1_P_BulkOut_EP_4	0x0004
#define M_Mode1_P_OK2Rcv		0x8000
#define M_Mode1_P_AllowAck		0x4000
#define M_Mode1_P_Enable		0x2000
#define M_Mode1_P_NAK_Enable	0x2000
#define M_Mode1_P_NAK_Enable_1	0x10
#define M_Mode1_P_AllowAck_1	0x20
#define M_Mode1_P_OK2Rcv_1		0x40



#define MSB250X_USBCREG(y)			((y * 2) + OTG0_BASE_ADDR)

/* 00h ~ 0Fh */
#define MSB250X_UDC_FADDR_REG		MSB250X_USBCREG(0x00)
#define MSB250X_UDC_PWR_REG			(MSB250X_UDC_FADDR_REG + 1)
#define MSB250X_UDC_INTRTX_REG		MSB250X_USBCREG(0x02)
/* 03h reserved */
#define MSB250X_UDC_INTRRX_REG		MSB250X_USBCREG(0x04)
/* 05h reserved */
#define MSB250X_UDC_INTRTXE_REG		MSB250X_USBCREG(0x06)
#define MSB250X_UDC_INTRTX1E_REG	MSB250X_USBCREG(0x06)
#define MSB250X_UDC_INTRTX2E_REG	(MSB250X_UDC_INTRTX1E_REG + 1)
/* 07h reserved */
#define MSB250X_UDC_INTRRXE_REG		MSB250X_USBCREG(0x08)
#define MSB250X_UDC_INTRRX1E_REG	MSB250X_USBCREG(0x08)
#define MSB250X_UDC_INTRRX2E_REG	(MSB250X_UDC_INTRRX1E_REG + 1)
/* 09h reserved */
#define MSB250X_UDC_INTRUSB_REG		MSB250X_USBCREG(0x0A)
#define MSB250X_UDC_INTRUSBE_REG	(MSB250X_UDC_INTRUSB_REG + 1)

#define MSB250X_UDC_INDEX_REG		MSB250X_USBCREG(0x0E)
#define MSB250X_UDC_CSR0_REG		MSB250X_USBCREG(0x12)
#define MSB250X_UDC_TXCSR_REG		MSB250X_UDC_CSR0_REG
#define MSB250X_UDC_RXCSR_REG		MSB250X_USBCREG(0x16)
#define MSB250X_UDC_DEVCTL_REG		MSB250X_USBCREG(0x60)

#define MSB250X_UDC_USB_CFG0_L		MSB250X_USBCREG(0x80)
#define MSB250X_UDC_USB_CFG1_L		MSB250X_USBCREG(0x82)
#define MSB250X_UDC_USB_CFG3_L		MSB250X_USBCREG(0x86)
#define MSB250X_UDC_USB_CFG5_L		MSB250X_USBCREG(0x8A)
#define MSB250X_UDC_USB_CFG6_L		MSB250X_USBCREG(0x8C)
#define MSB250X_UDC_USB_CFG6_H		(MSB250X_UDC_USB_CFG6_L + 1)
#define MSB250X_UDC_USB_CFG7_L		MSB250X_USBCREG(0x8E)
#define MSB250X_UDC_USB_CFG7_H		(MSB250X_UDC_USB_CFG7_L + 1)

#define MSB250X_UDC_DMA_MODE_CTL	MSB250X_UDC_USB_CFG5_L
#define MSB250X_UDC_DMA_MODE_CTL1	(MSB250X_UDC_USB_CFG0_L + 1)
#define MSB250X_UDC_EP_BULKOUT		MSB250X_UDC_USB_CFG3_L


/* MSB250X_UDC_PWR_REG */
#define MSB250X_UDC_PWR_ISOUP			(1 << 7)
#define MSB250X_UDC_PWR_SOFT_CONN		(1 << 6)
#define MSB250X_UDC_PWR_HS_EN			(1 << 5)
#define MSB250X_UDC_PWR_HS_MODE			(1 << 4)
#define MSB250X_UDC_PWR_RESET			(1 << 3)
#define MSB250X_UDC_PWR_RESUME			(1 << 2)
#define MSB250X_UDC_PWR_SUSPEND			(1 << 1)
#define MSB250X_UDC_PWR_ENSUSPEND		(1 << 0)

/* MSB250X_UDC_INTRTX_REG */
#define MSB250X_UDC_INTRTX_EP0			(1 << 0)

/* MSB250X_UDC_INTRUSB_REG */
#define MSB250X_UDC_INTRUSB_VBUS_ERR	(1 << 7)
#define MSB250X_UDC_INTRUSB_SESS_REQ	(1 << 6)
#define MSB250X_UDC_INTRUSB_DISCONN		(1 << 5)
#define MSB250X_UDC_INTRUSB_CONN		(1 << 4)
#define MSB250X_UDC_INTRUSB_SOF			(1 << 3)
#define MSB250X_UDC_INTRUSB_RESET		(1 << 2)
#define MSB250X_UDC_INTRUSB_RESUME		(1 << 1)
#define MSB250X_UDC_INTRUSB_SUSPEND		(1 << 0)

/* MSB250X_UDC_CSR0_REG */
#define MSB250X_UDC_CSR0_FLUSHFIFO		(1 << 8)
#define MSB250X_UDC_CSR0_SSETUPEND		(1 << 7)
#define MSB250X_UDC_CSR0_SRXPKTRDY		(1 << 6)
#define MSB250X_UDC_CSR0_SENDSTALL		(1 << 5)
#define MSB250X_UDC_CSR0_SETUPEND		(1 << 4)
#define MSB250X_UDC_CSR0_DATAEND		(1 << 3)
#define MSB250X_UDC_CSR0_SENTSTALL		(1 << 2)
#define MSB250X_UDC_CSR0_TXPKTRDY		(1 << 1)
#define MSB250X_UDC_CSR0_RXPKTRDY		(1 << 0)


/* MSB250X_UDC_TXCSR1_REG */
#define MSB250X_UDC_TXCSR1_CLRDATAOTG	(1 << 6)
#define MSB250X_UDC_TXCSR1_SENTSTALL	(1 << 5)
#define MSB250X_UDC_TXCSR1_SENDSTALL	(1 << 4)
#define MSB250X_UDC_TXCSR1_FLUSHFIFO	(1 << 3)
//#define MSB250X_UDC_TXCSR1_UNDERRUN		(1 << 2)
#define MSB250X_UDC_TXCSR1_FIFONOEMPTY	(1 << 1)
#define MSB250X_UDC_TXCSR1_TXPKTRDY		(1 << 0)


/* MSB250X_UDC_TXCSR2_REG */
#define MSB250X_UDC_TXCSR2_AUTOSET		(1 << 7)
#define MSB250X_UDC_TXCSR2_ISOC			(1 << 6)
#define MSB250X_UDC_TXCSR2_MODE			(1 << 5)
#define MSB250X_UDC_TXCSR2_DMAREQENAB	(1 << 4)
//#define MSB250X_UDC_TXCSR2_FRCDATAOG	(1 << 3)
#define MSB250X_UDC_TXCSR2_DMAREQMODE	(1 << 2)

/* MSB250X_UDC_RXCSR1_REG */
#define MSB250X_UDC_RXCSR1_CLRDATATOG	(1 << 7)
#define MSB250X_UDC_RXCSR1_SENTSTALL	(1 << 6)
#define MSB250X_UDC_RXCSR1_SENDSTALL	(1 << 5)
#define MSB250X_UDC_RXCSR1_FLUSHFIFO	(1 << 4)
//#define MSB250X_UDC_RXCSR1_DATAERROR	(1 << 3)
//#define MSB250X_UDC_RXCSR1_OVERRUN		(1 << 2)
//#define MSB250X_UDC_RXCSR1_FIFOFULL		(1 << 1)
#define MSB250X_UDC_RXCSR1_RXPKTRDY		(1 << 0)

/* MSB250X_UDC_RXCSR2_REG */
#define MSB250X_UDC_TXCSR2_AUTOSET		(1 << 7)
#define MSB250X_UDC_TXCSR2_ISOC			(1 << 6)
#define MSB250X_UDC_TXCSR2_MODE			(1 << 5)
#define MSB250X_UDC_TXCSR2_DMAREQENAB	(1 << 4)
#define MSB250X_UDC_TXCSR2_FRCDATAOG	(1 << 3)
//#define MSB250X_UDC_TXCSR2_DMAREQMODE	(1 << 2)

/* MSB250X_UDC_RXCSR2_REG */
#define MSB250X_UDC_RXCSR2_AUTOCLR		(1 << 7)
#define MSB250X_UDC_RXCSR2_ISOC			(1 << 6)
#define MSB250X_UDC_RXCSR2_DMAREQEN		(1 << 5)
#define MSB250X_UDC_RXCSR2_DISNYET		(1 << 4)
#define MSB250X_UDC_RXCSR2_DMAREQMD		(1 << 3)

#define MSB250X_UDC_RXCOUNT_L_EPX_REG(x)	(MSB250X_USBCREG((0x108 + (x * 0x10))))

#define MSB250X_UDC_EP0_FIFO_ACCESS_L		MSB250X_USBCREG(0x20)

#define MSB250X_UDC_EPX_FIFO_ACCESS_L(x) 	(MSB250X_USBCREG((0x20 + (x * 0x04))))

#define M_REG_DMA_INTR				((OTG0_BASE_ADDR) + (0x200<<OffShift))
#define DMA_CNTL_REGISTER(channel)	(uintptr_t volatile*)(M_REG_DMA_INTR + ((0x10 * (channel - 1) + 4)<<OffShift))
#define DMA_ADDR_REGISTER(channel)	(uintptr_t volatile*)(M_REG_DMA_INTR + ((0x10 * (channel - 1) + 8)<<OffShift))
#define DMA_COUNT_REGISTER(channel)	(uintptr_t volatile*)(M_REG_DMA_INTR + ((0x10 * (channel - 1) + 0xc)<<OffShift))


/*
 * Variables
 */
enum ep0_state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_END_XFER,
	EP0_STALL,
};

struct msb250x_ep {
	struct list_head queue;
	struct usb_gadget *gadget;
	struct msb250x_udc *dev;
	const struct usb_endpoint_descriptor *desc;
	struct usb_ep ep;
	u8 num;

	unsigned short fifo_size;
	u8 bEndpointAddress;
	u8 bmAttributes;
	u8 DmaRxMode1;
	u8 RxShort;
	u16 wMaxPacketSize;
	u8 ch;
	u8 dma_flag;	//dma_map, dma_unmap flag
	uintptr_t pa_addr;
#if defined(TIMER_PATCH)
	//for mode1 dma sw trigger irq
	u16 wtd_dma_count;
	u16 wtd_rx_count;
	int sw_ep_irq;
#endif
	unsigned halted : 1;
};

struct msb250x_request {
	struct list_head		queue;
	struct usb_request		req;
};

struct udc_utmi_param {
	u8	disconnect;
	u8	squelch;
	u8	chirp;
	u8	eye_param[4];
	u32	dpdm_swap;
	u32	otg_enable;
};

struct msb250x_udc {
	spinlock_t lock;

	struct msb250x_ep ep[MSB250X_ENDPOINTS];
	struct usb_gadget gadget;
	struct usb_gadget_driver	*driver;
	struct msb250x_request	fifo_req;
	struct platform_device	*pdev;
	u8 reg_power;
	u16 devstatus;
	int address;
	int ep0state;
	unsigned got_irq : 1;
	unsigned req_std : 1;
	unsigned req_config : 1;
	unsigned req_pending : 1;
	unsigned enabled:1;
	u8 DmaRxMode;
	struct semaphore sem;	/* Mutual exclusion semaphore     */
	struct cdev cdev;		/* Char device structure */
	wait_queue_head_t event_q;	/* Wait event queue. Now, only connection status change event. */
	int conn_chg;	/* flag for connect status change event. */
	unsigned active_suspend : 1;
#if defined(TIMER_PATCH)
	int remap_irq;
#endif

	struct udc_utmi_param	utmi_param;
	struct device			dev;
	struct list_head		list;
	unsigned int	irq;
};

/*
 * Function definition
 */
void msb250x_udc_done(struct msb250x_ep *ep, struct msb250x_request *req, int status);
void ms_NAKEnable(u8 bEndpointAddress);
int msb250x_udc_fifo_count_epx(u8 ep_idx);

s8 msb250x_udc_schedule_done(struct msb250x_ep *);
u16 GetCSR_EpIdx(u8 bEndpointAddress);
u32 Get32BitsReg(u32 volatile* Reg);
struct msb250x_request * msb250x_udc_do_request(struct msb250x_ep *ep, struct msb250x_request *req);
#if defined(CONFIG_OF)
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);
int msb250x_udc_set_halt(struct usb_ep *_ep, int value);
#endif




#define DMA_TX				0x2
#define DMA_RX				0x0

#define DMA_MODE_ZERO		0x0
#define DMA_MODE_ONE		0x4

#define DMA_IRQ_ENABLE		0x8
#define DMA_IRQ_DISABLE		0x0

#define DMA_MODE_MASK		(DMA_TX | DMA_MODE_ONE)

#define DMA_TX_ZERO_IRQ		(DMA_TX | DMA_MODE_ZERO | DMA_IRQ_ENABLE)
#define DMA_RX_ZERO_IRQ		(DMA_RX | DMA_MODE_ZERO | DMA_IRQ_ENABLE)

#define DMA_TX_ONE_IRQ		(DMA_TX | DMA_MODE_ONE | DMA_IRQ_ENABLE)
#define DMA_RX_ONE_IRQ		(DMA_RX | DMA_MODE_ONE | DMA_IRQ_ENABLE)

#define DMA_BurstMode		0x03

#define RXCSR2_MODE1		(MSB250X_UDC_RXCSR2_AUTOCLR | MSB250X_UDC_RXCSR2_DMAREQEN | MSB250X_UDC_RXCSR2_DMAREQMD)
#define TXCSR2_MODE1		(MSB250X_UDC_TXCSR2_DMAREQENAB | MSB250X_UDC_TXCSR2_AUTOSET | MSB250X_UDC_TXCSR2_DMAREQMODE)

#define DMA_ENABLE_BIT		0x0001
#define DMA_BUSERROR_BIT	0x0100
#define DMA_ENDPOINT_SHIFT	4

#define EP_IRQ_ENABLE		1
#define EP_IRQ_DISABLE		0
#define EP_IRQ_RX			0
#define EP_IRQ_TX			2

#define Enable_TX_EP_Interrupt(endpoint) \
		Control_EP_Interrupt(endpoint, (EP_IRQ_ENABLE | EP_IRQ_TX))

#define Enable_RX_EP_Interrupt(endpoint) \
		Control_EP_Interrupt(endpoint, (EP_IRQ_ENABLE | EP_IRQ_RX))

#define Disable_TX_EP_Interrupt(endpoint) \
		Control_EP_Interrupt(endpoint, (EP_IRQ_DISABLE | EP_IRQ_TX))

#define Disable_RX_EP_Interrupt(endpoint) \
		Control_EP_Interrupt(endpoint, (EP_IRQ_DISABLE | EP_IRQ_RX))

#define DMA_BUSY		1
#define DMA_NOT_BUSY	0

enum DMA_RX_MODE_TYPE {
	DMA_RX_MODE1,
	DMA_RX_MODE0,
	DMA_RX_MODE_NULL,
};

#if defined(TIMER_PATCH)
void dma_init_timer(struct timer_list *dma_polling_timer);
void ms_stop_timer(struct msb250x_ep *ep);
void ms_init_timer(void);
#endif
void Control_EP_Interrupt(s8 ep, u32 mode);
void USB_Set_ClrRXMode1(void);
void USB_DMA_IRQ_Handler(u8 ch, struct msb250x_udc *dev);
void Release_DMA_Channel(s8 channel);
s8 USB_Set_DMA(struct usb_ep *_ep, struct msb250x_request *req, u32 count, u32 mode);
s8 check_dma_busy(void);
u16 USB_Read_DMA_Control(s8);
#endif /* _MSB250X_UDC_H */
