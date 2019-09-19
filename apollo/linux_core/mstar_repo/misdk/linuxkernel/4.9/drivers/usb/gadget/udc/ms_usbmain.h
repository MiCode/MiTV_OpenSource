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
//      ms_usbmain.h
//
// DESCRIPTION
//      This header file define the exported main functions for MStar USB low level controls
//
// HISTORY
//      2008.8.1        Baker Chang     Initial Version
//
//------------------------------------------------------------------------------

#ifndef __MS_USBMAIN_H__
#define __MS_USBMAIN_H__

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include <asm/io.h>
#include "ms_drc.h"
#include "ms_cpu.h"

//------------------------------------------------------------------------------
//  Macros
//------------------------------------------------------------------------------
#define SET_CONTROL_LINE_STATE  0x22

//UTMI power control register
//
#define UTMI_REG_IREF_PDN       0x4000
#define UTMI_REG_SUSPEND_PDN    0xFF04

// Descriptor Types
#define DEVICE          0x01
#define CONFIGURATION   0x02
#define STRING          0x03
#define INTERFACE       0x04
#define ENDPOINT        0x05

#define CFGLEN 32
#ifdef EBOOT
#define USB_DMA_BUF_ADDR		0xA0040000
#else // !EBOOT
#define USB_DMA_BUF_ADDR		0xA0040000
#endif //EBOOT
#define USB_DMA_BUF_SIZE		(64*1024)
#define USB_EP0_MAX_PACKET_SIZE 64
#define USB_EP2_MAX_PACKET_SIZE 512
#define USB_DMA_MODE0   0
#define USB_DMA_MODE1   1

// Endpoint number
typedef enum{
    USB_EP_CONTROL=0,
    USB_EP_TX,
    USB_EP_RX,
} IMAGE_TYPE_et;

//------------------------------------------------------------------------------
//  Variables
//------------------------------------------------------------------------------
extern USB_INFO_st g_USBInfo;

//------------------------------------------------------------------------------
//  Functions
//------------------------------------------------------------------------------
PUSB_INFO_st USBInit(u8 u8DeviceClass, u8 u8DeviceCap);
bool USBPollInterrupt(void);
bool USBPollTx(void);
bool USBPollRx(u8 *pData, u16 *size);
bool USBPollRxDMA(u8 *pData, u32 *pu32Size);

void USBKITLWaitForConnect(void);
void USB_ParseDRCIntrUsb(u16 intrusb,USB_INFO_st *pUsbInfo);

#ifdef __cplusplus
}
#endif  //__cplusplus

#endif
