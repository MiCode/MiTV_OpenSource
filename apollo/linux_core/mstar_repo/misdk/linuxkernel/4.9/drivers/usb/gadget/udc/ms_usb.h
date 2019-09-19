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

//------------------------------------------------------------------------------
// FILE
//      ms_usb.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------
#ifndef	_MSUSB_H_
#define	_MSUSB_H_

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include "ms_drc.h"
#include "ms_cpu.h"

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS		    0x00
#define USB_REQ_CLEAR_FEATURE		0x01
#define USB_REQ_SET_FEATURE		    0x03
#define USB_REQ_SET_ADDRESS		    0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		    0x0C

/*
 * USB Packet IDs (PIDs)
 */
#define USB_PID_OUT                 0xe1
#define USB_PID_ACK                 0xd2
#define USB_PID_IN                  0x69
#define USB_PID_STALL               0x1e

#define USB_ENDPOINT_DIR_MASK		0x80
#define USB_ENDPOINT_XFERTYPE_MASK	0x03	/* in bmAttributes */
#define USB_ENDPOINT_XFER_BULK		2
#define USB_ENDPOINT_XFER_INT		3

#define USB_ST_NOERROR			0
#define USB_ST_NORESPONSE		0xfc
#define USB_ST_STALL			0xec

#define USB_DIR_OUT			    0
#define USB_DIR_IN			    0x80

#define USB_TYPE_MASK			(0x03 << 5)
#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_RECIP_MASK			0x1f
#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

#define EP0C_SET_ADDRESS        1   /* 1st enum cmd is to set dev address */
#define EP0C_GET_MAX_EP0        2   /* fetch functions max EP0, 1st enum req */
#define EP0C_GET_STD_DEV_DESC   3   /* 2nd enum req, get std descriptor */
#define EP0C_GET_CONFIG         4   /* read config descriptor only */
#define EP0C_GET_FULL_CONFIG    5   /* read entire config descriptor */
#define EP0C_SET_CONFIG         6   /* set config 0 prior to probe */
#define EP0C_SET_FEATURE        7   /* set a feature, like OTG */
#define EP0C_CLEAR_FEATURE        8   /* set a feature, like OTG */

#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05
#define	USB_DT_DEVICE_QUALIFIER		0x06
#define	USB_DT_OTHER_SPEED		    0X07
#define	USB_DT_INTERFACE_POWER		0x08
#define	USB_DT_OTG			        0x09
#define USB_DT_DEVICE_SIZE		     18
#define USB_DT_DEVICE_QUALIFIER_SIZE 10
#define USB_DT_CONFIG_SIZE		      9
#define USB_DT_INTERFACE_SIZE		  9
#define USB_DT_ENDPOINT_SIZE		  7
#define	USB_DT_OTG_SIZE			      3

#define	USB_OTG_SRP			0x01	/* bit 0 of bmAttributes */
#define	USB_OTG_HNP			0x02	/* bit 1 of bmAttributes */

#define MAX_BITS_BYTE    (8)
#define MAX_BITS_SHORT   (16)
#define MAX_BITS_3BYTE   (24)
#define MAX_BITS_INT     (32)

#define POSITION_VALUE_8    (0x100)
#define POSITION_VALUE_16   (u32)(0x10000)
#define POSITION_VALUE_24   (u32)(0x1000000)

//#define BIT_MASK(n)    ( ~(~(0L)<<n) )
#undef BIT_MASK
	
#define	ENDPOINT_HALT			0x00

#define usb_pipeout(pipe)	((((pipe) >> PIPEDEF_DIR) & 1) ^ 1)
#define usb_pipein(pipe)	(((pipe) >> PIPEDEF_DIR) & 1)
#define usb_pipeendpoint(pipe)	(((pipe) >> PIPEDEF_EP) & 0xf)
#define usb_pipetype(pipe)	(((pipe) >> PIPEDEF_ATTR) & 3)
#define usb_pipeisoc(pipe)	(usb_pipetype((pipe)) == PIPE_ISOCHRONOUS)
#define usb_pipebulk(pipe)	(usb_pipetype((pipe)) == PIPE_BULK)

#define	usb_sndctrlpipe(endpoint)	((PIPE_CONTROL << PIPEDEF_ATTR) | \
	(PIPE_OUT << PIPEDEF_DIR) | (((endpoint) << PIPEDEF_EP) &0xf))

#define	usb_rcvctrlpipe(endpoint)	((PIPE_CONTROL << PIPEDEF_ATTR) | \
	(PIPE_IN  << PIPEDEF_DIR) | (((endpoint) << PIPEDEF_EP) &0xf))

#define	usb_sndisopipe(endpoint)	((PIPE_ISOCHRONOUS<< PIPEDEF_ATTR) | \
	(PIPE_OUT << PIPEDEF_DIR) | (((endpoint) << PIPEDEF_EP) &0xf))

#define	usb_sndbulkpipe(endpoint)	((PIPE_BULK << PIPEDEF_ATTR) | \
	(PIPE_OUT << PIPEDEF_DIR) | (((endpoint) << PIPEDEF_EP) &0xf))

#define	usb_rcvbulkpipe(endpoint)	((PIPE_BULK << PIPEDEF_ATTR) | \
	(PIPE_IN << PIPEDEF_DIR) | (((endpoint) << PIPEDEF_EP) &0xf))

#define  MSD_BOT_CSW_LENGTH        0x0D
#define  MSD_BOT_CBW_LENGTH        0x1F
#define  MSD_BOT_CBW_CB_LENGTH     0x1F

/* All standard descriptors have these 2 fields in common */
typedef	struct {
	s8  bLength;
	s8  bDescriptorType;
} usb_descriptor_header;


/* Device descriptor */
struct usb_device_descriptor {
	u8  bLength;
	u8  bDescriptorType;
	u16 bcdUSB;
	u8  bDeviceClass;
	u8  bDeviceSubClass;
	u8  bDeviceProtocol;
	u8  bMaxPacketSize0;
	u16 idVendor;
	u16 idProduct;
	u16 bcdDevice;
	u8  iManufacturer;
	u8  iProduct;
	u8  iSerialNumber;
	u8  bNumConfigurations;
};

/* Endpoint descriptor */
typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u8  bEndpointAddress;
	u8  bmAttributes;
	u16 wMaxPacketSize;
	u8  bInterval;
	u8  bRefresh;
	u8  bSynchAddress;
   	u8  *extra;   /* Extra descriptors */
	s32 extralen;
} usb_endpoint_descriptor;

typedef	struct {
        u8  bLength;
        u8  bDescriptorType;
        u8  bEndpointAddress;
        u8  bmAttributes;
        u16 wMaxPacketSize;
        u8  bInterval;
} usb_function_endpoint_descriptor;

/* Interface descriptor */
typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u8  bInterfaceNumber;
	u8  bAlternateSetting;
	u8  bNumEndpoints;
	u8  bInterfaceClass;
	u8  bInterfaceSubClass;
	u8  bInterfaceProtocol;
	u8  iInterface;
  	usb_endpoint_descriptor *endpoint;
   	u8  *extra;   /* Extra descriptors */
	s32 extralen;
} usb_interface_descriptor;

typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u8  bInterfaceNumber;
	u8  bAlternateSetting;
	u8  bNumEndpoints;
	u8  bInterfaceClass;
	u8  bInterfaceSubClass;
	u8  bInterfaceProtocol;
	u8  iInterface;
} usb_function_interface_descriptor;

typedef	struct {
	usb_interface_descriptor *altsetting;
	s32 act_altsetting;		/* active alternate setting */
	s32 num_altsetting;		/* number of alternate settings */
	s32 max_altsetting;             /* total memory allocated */
 	//struct usb_driver *driver;	/* driver */
	void *private_data;
} usb_interface;

/* Configuration descriptor information.. */
typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u16 wTotalLength;
	u8  bNumInterfaces;
	u8  bConfigurationValue;
	u8  iConfiguration;
	u8  bmAttributes;
	u8  MaxPower;
	//usb_interface *interface;
   	u8 *extra;   /* Extra descriptors */
	s32 extralen;
} usb_config_descriptor;

typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u16 wTotalLength;
	u8  bNumInterfaces;
	u8  bConfigurationValue;
	u8  iConfiguration;
	u8  bmAttributes;
	u8  bMaxPower;
} usb_function_config_descriptor;

/* String descriptor */
typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u16 wData[1];
} usb_string_descriptor;

typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u8  bmAttributes;	/* bit 0=SRP; bit 1=HNP */
} usb_otg_descriptor;

typedef	struct {
	u8  bLength;
	u8  bDescriptorType;
	u16 bcdHID;
	u8  bCountryCode;
	u8  bNumDescriptors;
	u8  bClassDescriptorType;
	u16 wDescriptorLength;
} usb_hid_descriptor;

typedef	struct {
	u8  bDescriptorType;
	u16 wDescriptorLength;
} hid_optional_descriptor;


typedef struct {
    u8  bLength;
    u8  bDescriptorType;

    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u8  bNumConfigurations;
    u8  bRESERVED;
} usb_qualifier_descriptor;

struct usb_device{
	u8	core;
	struct	usb_device_descriptor descriptor;/* Descriptor */
	usb_config_descriptor *config;	/* All of the configs */
	usb_config_descriptor *actconfig;/* the active configuration */
	s8 **rawdescriptors;		/* Raw descriptors for each config */
};

void USB_SWOP_Setup(USB_DEVICE_REQUEST* pUdr);
void USB_Change_USB_State(u8 toUSB, USB_INFO_st *pUsbInfo);


#ifdef __cplusplus
}
#endif  //__cplusplus

#endif	/* _MSUSB_H_	*/

