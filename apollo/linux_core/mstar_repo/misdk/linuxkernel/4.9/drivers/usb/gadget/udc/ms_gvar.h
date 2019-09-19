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
//      ms_gvar.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------

#ifndef _MS_USB_GLOBAL_VARIABLE
#define _MS_USB_GLOBAL_VARIABLE

#include <linux/kernel.h>


#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#define CB2
//
//for USB device mode driver
//

typedef enum _usb_dev_mode
{
    E_USB_Unknown= 0,
    E_USB_MassStorage = 1,
    E_USB_VirtCOM = 2,
    E_USB_Modem = 3,
    E_USB_PCCAM = 4,
    E_USB_PictureBridge = 5,
    E_USB_OTG=6
} USB_DEVICE_MODE;

typedef enum _usb_dev_class
{
    E_Class_Unknown = 0,
    E_Class_KITL,
    E_Class_Serial
} USB_DEVICE_CLASS_e;

typedef struct _USB_DEVICE_REQUEST {
    u8  bmRequestType;
    u8  bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} USB_DEVICE_REQUEST, *PUSB_DEVICE_REQUEST;

typedef struct _endpoint
{
    u16   FIFOSize; // no use
    u16   MaxEPSize;
    u32   FifoRemain;
    u32   BytesRequested;
    u32   BytesProcessed;
    u8    DRCInterval; // no use
    u8    intr_flag; // no use
    u8    pipe;
    u8    BltEP;
    u8    DRCDir;
    u8    LastPacket;
    u8    IOState;
    u8    Halted;
    u8    Infnum; // no use
    u32   transfer_buffer;  // Transfer buffer address
    u32   transfer_buffer_length;
} ENDPOINT_st;

typedef struct USB_VAR_
{
    u16   volatile  otgTestMode;
    u8    volatile  otgSetFaddr;
    u8    volatile  otgClearedEP; // no use
    u16   volatile  otgGetStatusResponse;
    u8    volatile  otgMyDeviceHNPSupport;
    u8    volatile  free_dma_channels;
    u32   volatile  otgCSW_Addr; // no use
    u32   volatile  otgSetupCmdBuf_Addr; // no use
    u32   volatile  otgCBWCB_Addr; // no use
    u8    volatile  otgRegPower;
    u8    volatile  otgIntStatus;
    u8    volatile  otgDMARXIntStatus;
    u8    volatile  otgDMATXIntStatus;
    u8    volatile  otgDataPhaseDir;
    u8    volatile  otgMassCmdRevFlag; // no use
    u8    volatile  otgMassRxDataReceived; // no use
    u8    volatile  otgReqOTGState; // no use
    u8    volatile  otgCurOTGState;
    u8    volatile  otgSuspended; // no use
    u8    volatile  otgRemoteWakeup;
    u8    volatile  otgHNPEnabled;
    u8    volatile  otgHNPSupport; // no use
    u8    volatile  otgSelfPower;
    u8    volatile  otgConfig;
    u8    volatile  otgInterface;
    u8    volatile  otgUSBState;
    u8    volatile  otgcid; // 0 = CID_A_DEVICE, 1 = CID_B_DEVICE, 2 = CID_UNKNOWN
    u8    volatile  otgFaddr;
    u8    volatile  otgRegDevCtl;
    u8    volatile  otgSpeed;
    u8    volatile  otgResetComplete; // no use
    u16   volatile  otgSOF_1msCount; // no use
    u8    volatile  otgIsNonOSmodeEnable; // no use
    u32   volatile  otgUDPAddress; // no use
    u32   volatile  otgUDPTxPacketCount; // no use
    u32   volatile  otgUDPRxPacketCount; // no use
    u8    volatile  bDownloadCode; // no use
    u8    volatile  u8USBDeviceMode;
    u8    volatile  DeviceConnect; // no use
    u8    volatile  u8USBDevMode;
    u16   volatile  gu16UplinkStart; // no use
    u32   volatile  gu32UplinkSize; // no use
    u8    volatile  otgSelectROMRAM; // no use
    u8    volatile  bHIF_GetUplinkDataStatus; // no use
    u16   volatile  PPB_One_CB; // no use
    u16   volatile  PPB_Two_CB; // no use
    u8    volatile  UploadResume; // no use
    u16   volatile  gu16BBErrorCode; // no use
    u32   volatile  nTransferLength; // no use
    u32   volatile  NonOS_UsbDeviceDataBuf_CB; // no use
    u32   volatile  SizeofUSB_Msdfn_Dscr; // no use
    u8    volatile  otgFSenseKey; // no use
    u8    volatile  otgFASC; // no use
    u8    volatile  otgFASCQ; // no use
    s32   volatile  otgfun_residue;
    u32   volatile  otgactualXfer_len;
    u8    volatile  otgdataXfer_dir;
    ENDPOINT_st otgUSB_EP[4];
    USB_DEVICE_REQUEST otgEP0Setup;
    u8  volatile UsbSetupCmdBuf[32];
    u8  volatile UsbCSWBuf[(13+3)]; // no use
    u8  volatile UsbCBWBuf[(31+1)]; // no use
    u8  volatile *PPB_Two; // no use
    u8  volatile *NonOS_UsbDeviceDataBuf; // no use
    u8  volatile *USB_Msdfn_Dscr; // no use
    u8  volatile USB_PB_CONNECTED;
    u8  volatile USB_CREATEPORT_COUNTER; // no use
    u8  u8DeviceClass;
    u8  u8DeviceCap;

} USB_INFO_st, *PUSB_INFO_st;

#ifdef __cplusplus
}
#endif  //__cplusplus

#endif  //_MS_USB_GLOBAL_VARIABLE
