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

//-----------------------------------------------------------------------------
// FILE
//      ms_usbmain.c
//
// DESCRIPTION
//
//
// HISTORY
//      2008.6.7    Baker Chang     Initial Version
//      2008.11.7   Baker Chang     coding convention
//      2009.2.4    Calvin Hung     Modify UTMI init setting for eye diagram
//      2009.4.22   Calvin Hung     Modify UTMI setting for stability
//
//-----------------------------------------------------------------------------

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------

//#include <windows.h>
//#include <types.h>
//#include <ceddk.h>
//#include <memory.h>
//#include <columbus.h>
#include "ms_config.h"
#include "ms_cdc.h"
#include "ms_otg.h"
#include "ms_function.h"
#include "ms_usbmain.h"
#include "ms_usb.h"
//#include "ms_dma.h"
//#include <args.h>

//------------------------------------------------------------------------------
//  Variables
//------------------------------------------------------------------------------

//USB_INFO_st pg_USBInfo; //= (USB_INFO_st *)(COLUMBUS_BASE_RAM_UA + 0x8000);
//------------------------------------------------------------------------------
//  Functions
//------------------------------------------------------------------------------
void mdelay(unsigned int msec)
{

    //Delay(1000*msec);
    //Sleep(msec);

}

//------------------------------------------------------------------------------
//
//  Function:    InitUSBVar
//
//    Description
//        This function initialize the USB related global variables.
//
//    Parameters
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
#if 0
void InitUSBVar(USB_INFO_st *pUsbInfo)
{
    unsigned char u8EpNum = 0;

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
#endif
//------------------------------------------------------------------------------
//
//  Function:    USB_SetDRCInterrupts
//
//    Description
//        This function will set the USB interrupt.
//
//    Parameters
//        None.
//
//    Return Value
//        None.
//
void USB_SetDRCInterrupts(void)
{
    USB_REG_WRITE8(M_REG_INTRUSBE, 0xf7);
    USB_REG_WRITE16(M_REG_INTRTXE, 0xff);
    USB_REG_WRITE16(M_REG_INTRRXE, 0xff);
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Clear_DRC_Interrupts
//
//    Description
//        This function will clear the USB interrupt.
//
//    Parameters
//        None.
//
//    Return Value
//        None.
//
void USB_Clear_DRC_Interrupts(void)
{
    USB_REG_READ8(M_REG_INTRUSB);
    USB_REG_READ16(M_REG_INTRTX);
    USB_REG_READ16(M_REG_INTRRX);
}

//------------------------------------------------------------------------------
//
//  Function:    UTM_Init
//
//    Description
//        This function will initialize UTMI configuration.
//
//    Parameters
//        None.
//
//    Return Value
//        None.
//
#if 1
#if 0
static void UTM_Init(void)
{

 	UTMI_REG_WRITE8(0x06*2, (UTMI_REG_READ8(0x06*2) & 0x9F) | 0x40); //reg_tx_force_hs_current_enable

	UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) | 0x28); //Disconnect window select
	UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) & 0xef); //Disconnect window select

	UTMI_REG_WRITE8(0x07*2-1, UTMI_REG_READ8(0x07*2-1) & 0xfd); //Disable improved CDR
	UTMI_REG_WRITE8(0x09*2-1, UTMI_REG_READ8(0x09*2-1) |0x81);  // UTMI RX anti-dead-loc, ISI effect improvement
	UTMI_REG_WRITE8(0x15*2-1, UTMI_REG_READ8(0x15*2-1) |0x20);  // Chirp signal source select

	UTMI_REG_WRITE8(0x0b*2-1, UTMI_REG_READ8(0x0b*2-1) |0x80);  // set reg_ck_inv_reserved[6] to solve timing problem

	UTMI_REG_WRITE8(0x2c*2,   UTMI_REG_READ8(0x2c*2) |0x10);
	UTMI_REG_WRITE8(0x2d*2-1, UTMI_REG_READ8(0x2d*2-1) |0x02);
	UTMI_REG_WRITE8(0x2f*2-1, UTMI_REG_READ8(0x2f*2-1) |0x81);


#if 0
    USBC_REG_WRITE8(0x4, (USBC_REG_READ8(0x4)&~0x1) | 0x02);  //enable OTG

    //USBC_REG_WRITE8(0x180, USBC_REG_READ8(0x180) & ~0x10);

    // soft reset
    USB_REG_WRITE8(M_REG_CFG0_L, USB_REG_READ8(M_REG_CFG0_L)&~0x01);
    USB_REG_WRITE8(M_REG_CFG0_L, USB_REG_READ8(M_REG_CFG0_L)|0x01);

    // Miu_mode AFIFO bug fixed ECO enable
    USB_REG_WRITE8(M_REG_MIU_MODE, USB_REG_READ8(M_REG_MIU_MODE)|0x01);
#endif
}
#endif

#else
static void UTM_Init(void)
{
    U16   u16Reg;
    u16Reg = UTMI_REG_READ16(0x04) & ~0x3800 | 0x2800;
    UTMI_REG_WRITE16(0x04, u16Reg); // 0x3, bit<5:3> set 101

    u16Reg = UTMI_REG_READ16(0x0C)|(BIT_6|BIT_2);
    u16Reg &= ~(BIT_9|BIT_5);
    UTMI_REG_WRITE16(0x0C, u16Reg); // [9]=0,[6:5]=10,[2]=1

    UTMI_REG_WRITE8(0x11, UTMI_REG_READ8(0x11) | 0xE0); // [7:6:5]=111,

    UTMI_REG_WRITE8(0x29, UTMI_REG_READ8(0x29)|0x20); // [5] = 1

    UTMI_REG_WRITE8(0x4d, (UTMI_REG_READ8(0x4d)&~0x04)|0x08); // [3:2] = 10

    UTMI_REG_WRITE8(0x51, UTMI_REG_READ8(0x51) | 0x08); // [3]=1

    UTMI_REG_WRITE8(0x54, UTMI_REG_READ8(0x54) & ~0x0F); // [3:0]=0000

    UTMI_REG_WRITE16(0x58, (UTMI_REG_READ16(0x58) & ~0x2) | 0x03C1);

    UTMI_REG_WRITE16(0x5C, UTMI_REG_READ16(0x5C) | 0x2500);

    // Wait for UTMI
    while((UTMI_REG_READ8(0x60)&0x01)==0);

    USBC_REG_WRITE8(0x4, (USBC_REG_READ8(0x4)&~0x1) | 0x02);

    //USBC_REG_WRITE8(0x180, USBC_REG_READ8(0x180) & ~0x10);

    // soft reset
    USB_REG_WRITE8(M_REG_CFG0_L, USB_REG_READ8(M_REG_CFG0_L)&~0x01);
    USB_REG_WRITE8(M_REG_CFG0_L, USB_REG_READ8(M_REG_CFG0_L)|0x01);

    // Miu_mode AFIFO bug fixed ECO enable
    USB_REG_WRITE8(M_REG_MIU_MODE, USB_REG_READ8(M_REG_MIU_MODE)|0x01);
}
#endif


//------------------------------------------------------------------------------
//
//  Function:    USBInit
//
//    Description
//        This function will initialize USB registers.
//
//    Parameters
//        u8DeviceClass:    [in] USB Device class.
//        u8DeviceCap:      [in] USB Device capability.
//
//    Return Value
//        None.
//
#if 1
#if 0
#define _MSTAR_USB_BASEADR 0xfd200000
#define ENABLE	1
#define DISABLE	0
#define BIT0  0x01
#define BIT1  0x02
#define BIT2  0x04
#define BIT3  0x08
#define BIT4  0x10
#define BIT5  0x20
#define BIT6  0x40
#define BIT7  0x80
unsigned char MDrv_USB_ReadByte(unsigned int addr)
{
    if(addr & 1)
        return readb((void*)(_MSTAR_USB_BASEADR+((addr-1)<<1)+1));
    else
        return readb((void*)(_MSTAR_USB_BASEADR+(addr<<1)));
}

void MDrv_USB_WriteByte(unsigned int addr, unsigned char value)
{
    if(addr & 1)
        writeb(value, (void*)(_MSTAR_USB_BASEADR+((addr-1)<<1)+1));
    else
        writeb(value, (void*)(_MSTAR_USB_BASEADR+(addr<<1)));
}

void MDrv_USB_WriteRegBit(unsigned int addr, bool bEnable, unsigned char value)
{

    if (bEnable)
        MDrv_USB_WriteByte(addr, MDrv_USB_ReadByte(addr)|value);
    else
        MDrv_USB_WriteByte(addr, MDrv_USB_ReadByte(addr)&(~value));

}

void UTMI_Init(void)
{
	printk("+UTMI\n");
    UTMI_REG_WRITE8(0x06*2, (UTMI_REG_READ8(0x06*2) & 0x9F) | 0x40); //reg_tx_force_hs_current_enable
    UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) | 0x28); //Disconnect window select
    UTMI_REG_WRITE8(0x03*2-1, UTMI_REG_READ8(0x03*2-1) & 0xef); //Disconnect window select
    UTMI_REG_WRITE8(0x07*2-1, UTMI_REG_READ8(0x07*2-1) & 0xfd); //Disable improved CDR
    UTMI_REG_WRITE8(0x09*2-1, UTMI_REG_READ8(0x09*2-1) |0x81);  // UTMI RX anti-dead-loc, ISI effect improvement
    UTMI_REG_WRITE8(0x15*2-1, UTMI_REG_READ8(0x15*2-1) |0x20);  // Chirp signal source select
    UTMI_REG_WRITE8(0x0b*2-1, UTMI_REG_READ8(0x0b*2-1) |0x80);  // set reg_ck_inv_reserved[6] to solve timing problem
#if 1//defined(CONFIG_MSTAR_CEDRIC)
    UTMI_REG_WRITE8(0x2c*2,   0x10);
    UTMI_REG_WRITE8(0x2d*2-1, 0x02);
    UTMI_REG_WRITE8(0x2f*2-1, 0x81);
#else
    UTMI_REG_WRITE8(0x2c*2,   UTMI_REG_READ8(0x2c*2) |0x98);
    UTMI_REG_WRITE8(0x2d*2-1, UTMI_REG_READ8(0x2d*2-1) |0x02);
    UTMI_REG_WRITE8(0x2e*2,   UTMI_REG_READ8(0x2e*2) |0x10);
    UTMI_REG_WRITE8(0x2f*2-1, UTMI_REG_READ8(0x2f*2-1) |0x01);
#endif
	printk("-UTMI\n");
}

PUSB_INFO_st USBInit(unsigned char u8DeviceClass, unsigned char u8DeviceCap)
{
    printk("+USBInit\r\n");
	InitUSBVar(&pg_USBInfo);

    pg_USBInfo.u8DeviceClass = u8DeviceClass;
    pg_USBInfo.u8DeviceCap = u8DeviceCap;
    pg_USBInfo.u8USBDevMode = E_USB_VirtCOM;

    // Disable UHC and OTG controllers
    USBC_REG_WRITE8(0x4, USBC_REG_READ8(0x4)& (~0x3));

#ifdef USB_ENABLE_UPLL
    //UTMI_REG_WRITE16(0, 0x4000);
    UTMI_REG_WRITE16(0, 0x6BC3); // Turn on UPLL, reg_pdn: bit<9> reg_pdn: bit<15>, bit <2> ref_pdn
    mdelay(1);
    UTMI_REG_WRITE8(1, 0x69);      // Turn on UPLL, reg_pdn: bit<9>
    mdelay(2);
    UTMI_REG_WRITE16(0, 0x0001); //Turn all (including hs_current) use override mode
                                 // Turn on UPLL, reg_pdn: bit<9>
    mdelay(3);
#endif
	UTMI_REG_WRITE8(0x3C*2, UTMI_REG_READ8(0x3C*2) | 0x1); // set CA_START as 1
	mdelay(10);
	UTMI_REG_WRITE8(0x3C*2, UTMI_REG_READ8(0x3C*2) & ~0x01); // release CA_START
    while ((UTMI_REG_READ8(0x3C*2) & 0x02) == 0);        // polling bit <1> (CA_END)

	// Reset OTG controllers
	USBC_REG_WRITE8(0, 0xC);

	// Unlock Register R/W functions  (RST_CTRL[6] = 1)
	// Enter suspend  (RST_CTRL[3] = 1)
	USBC_REG_WRITE8(0, 0x48);

	UTMI_Init();

    // 2'b10: OTG enable
    //USBC_REG_WRITE8(0x4, (USBC_REG_READ8(0x4)&~0x1) | 0x02);
	//USBC_REG_WRITE8(0, USBC_REG_READ8(0)|OTG_BIT6);

	USBC_REG_WRITE8(0x00, 0x68);	//enable OTG and UHC XIU
	//USBC_REG_WRITE8(0x02*2, 0x02);	//Enable OTG MAC
	USBC_REG_WRITE8(0x02*2, USBC_REG_READ8(0x02*2) | 0x02);	//Enable OTG MAC

	USB_REG_WRITE8(0x100, USB_REG_READ8(0x100)&0xFE); // Reset OTG
	USB_REG_WRITE8(0x100, USB_REG_READ8(0x100)|0x01);
	USB_REG_WRITE16(0x100, USB_REG_READ16(0x100)|0x8000); /* Disable DM pull-down */

    // Set FAddr to 0
    USB_REG_WRITE8(M_REG_FADDR, 0);
	// Set Index to 0
	USB_REG_WRITE8(M_REG_INDEX, 0);
	USB_REG_WRITE8(M_REG_CFG6_H, USB_REG_READ8(M_REG_CFG6_H) | 0x08);
	USB_REG_WRITE8(M_REG_CFG6_H, USB_REG_READ8(M_REG_CFG6_H) | 0x40);
	//while(0x18 != (USB_REG_READ8(M_REG_DEVCTL) & 0x18));

	//Pull up D+
    if (u8DeviceCap==FULL_SPEED)
    {
    	printk("FULL SPEED\n");
        USB_REG_WRITE8(M_REG_POWER, M_POWER_SOFTCONN);
    }
	else if (u8DeviceCap==HIGH_SPEED)
	{
		printk("HIGH SPEED\n");
        USB_REG_WRITE8(M_REG_POWER, (USB_REG_READ8(M_REG_POWER) & ~M_POWER_ENSUSPEND) | M_POWER_SOFTCONN | M_POWER_HSENAB);
	}

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
	USB_SetDRCInterrupts();
	USB_Clear_DRC_Interrupts();

	return &pg_USBInfo;
}
#endif
#else

PUSB_INFO_st USBInit(U8 u8DeviceClass, U8 u8DeviceCap)
{
    RETAILMSG(1, (_T("+USBInit\r\n")));


    InitUSBVar(pg_USBInfo);

    pg_USBInfo->u8DeviceClass = u8DeviceClass;
    pg_USBInfo->u8DeviceCap = u8DeviceCap;
    pg_USBInfo->u8USBDevMode = E_USB_VirtCOM;
#ifndef FPGA
    //UTMI_REG_WRITE16(0, 0x4000);
    UTMI_REG_WRITE16(0, UTMI_REG_READ16(0)|0x01);
    UTMI_REG_WRITE16(0, UTMI_REG_READ16(0)|0x4000);
    UTMI_REG_WRITE16(0, UTMI_REG_READ16(0)|0x0200); // reset PLL
    UTMI_REG_WRITE16(0, UTMI_REG_READ16(0)&~0x0200);
    UTMI_REG_WRITE16(0, UTMI_REG_READ16(0)&~0x01); // enable IREF

    //UTMI_REG_WRITE16(0x10, 0x800);
    UTMI_REG_WRITE16(0x20, 0x40); // Disable UTMI interrupt
#endif

    // Reset OTG, UHC and USB controllers
    USBC_REG_WRITE16(0, 0x7);

#ifdef FPGA
    // Unlock Register R/W functions  (RST_CTRL[6] = 1)
    // Enter suspend  (RST_CTRL[3] = 0)
    USBC_REG_WRITE16(0, 0x40);
#else
    // Unlock Register R/W functions  (RST_CTRL[6] = 1)
    // Enter suspend  (RST_CTRL[3] = 1)
    USBC_REG_WRITE16(0, 0x48);

    UTM_Init();

    while(!(UTMI_REG_READ8(0x60) & 0x01));
#endif

    // 2'b10: OTG enable
    USBC_REG_WRITE8(0x4, (USBC_REG_READ8(0x4)&~0x1) | 0x02);

    // Write 0 to LSB to reset OTG IP, active low
    USB_REG_WRITE16(M_REG_CFG0_L, USB_REG_READ16(M_REG_CFG0_L) & ~0x1);

    //Disable USB device reset
    USB_REG_WRITE16(M_REG_CFG0_L, USB_REG_READ16(M_REG_CFG0_L) | 0x1);

#ifdef FPGA
    //Enter USB device mode
    USB_REG_WRITE16(M_REG_CFG0_L, 0x41);

    // Pull up D+
    USB_REG_WRITE8(M_REG_POWER, (USB_REG_READ8(M_REG_POWER) & ~M_POWER_ENSUSPEND) | M_POWER_SOFTCONN);
#else
    //Override UTMI interface
    USB_REG_WRITE16(M_REG_CFG0_L, 0x01);
    USB_REG_WRITE16(M_REG_MIU_MODE, 0x16);

    while(0x18 != (USB_REG_READ8(M_REG_DEVCTL) & 0x18));

    // Pull up D+
    if (u8DeviceCap==FULL_SPEED)
    {
        USB_REG_WRITE8(M_REG_POWER, M_POWER_SOFTCONN);
    }
    else if (u8DeviceCap==HIGH_SPEED)
    {
        USB_REG_WRITE8(M_REG_POWER, (USB_REG_READ8(M_REG_POWER) & ~M_POWER_ENSUSPEND) | M_POWER_SOFTCONN | M_POWER_HSENAB);
    }
#endif

    USB_REG_WRITE8(M_REG_DEVCTL,0);

    /*     Reset actions    */

    // Set FAddr to 0
    USB_REG_WRITE8(M_REG_FADDR, 0);

    // Set Index to 0
    USB_REG_WRITE8(M_REG_INDEX, 0);

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
    USB_SetDRCInterrupts();
    USB_Clear_DRC_Interrupts();

//    RETAILMSG(1, (_T("-USBInit\r\n")));
    return pg_USBInfo;
}
#endif
#if 0
void USBKITLWaitForConnect()
{
    RETAILMSG(1, (_T("Wait for Cable Connected\r\n")));
    //while (!USBPollInterrupt());

    while (pg_USBInfo->otgUSBState!=USB_CONFIGURED)//(!g_USBInfo.USB_PB_CONNECTED) // use (otgUSBState==USB_CONFIGURED) instead
    {
        USBPollInterrupt();
    }
    RETAILMSG(1, (_T("Cable has been connected!!!\r\n")));

    //while (!(USB_REG_READ16(M_REG_COUNT0)))
    {
        USBPollInterrupt();
    }
    RETAILMSG(1, (_T("Exit KITL Polling\r\n")));
}

//------------------------------------------------------------------------------
//
//  Function:    USB_DRCReadPower
//
//    Description
//        This function will get USB power setting.
//
//    Parameters
//        u8USBDeviceMode:    [in] USB Device mode.
//
//    Return Value
//        None.
//
void USB_DRCReadPower(USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    pUsbInfo->otgRegPower = USB_REG_READ8(M_REG_POWER);
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Read_DRC_Devctl
//
//    Description
//        This function will read USB DEVCTL register.
//
//    Parameters
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Read_DRC_Devctl(USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    pUsbInfo->otgRegDevCtl = USB_REG_READ8(M_REG_DEVCTL);
}

//------------------------------------------------------------------------------
//
//  Function:    USB_RefreshDRCPower
//
//    Description
//        This function will refresh USB registers.
//
//    Parameters
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_RefreshDRCPower(USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    USB_DRCReadPower(pUsbInfo);
    USB_Read_DRC_Devctl(pUsbInfo);
}

//------------------------------------------------------------------------------
//
//  Function:   USB_Read_CID
//
//    Description
//        This function will read USB CID.
//
//    Parameters
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Read_CID(USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    USB_Read_DRC_Devctl(pUsbInfo);

    if (pUsbInfo->otgRegDevCtl & M_DEVCTL_SESSION) //determine the device
        pUsbInfo->otgcid = (pUsbInfo->otgRegDevCtl & M_DEVCTL_BDEVICE) >> 7;
    else
        pUsbInfo->otgcid = CID_UNKNOWN;
}

//------------------------------------------------------------------------------
//
//  Function:   USB_Clear_DRC_Power
//
//  Description
//      This function will clear USB power related registers.
//
//  Parameters
//      regupdate:      [in] M_REG_POWER register value.
//      pUsbInfo:     [in/out] Pointer to the structure of USB variables.
//
//  Return Value
//      None.
//
void USB_Clear_DRC_Power(U8 u8Regupdate, USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    if  (!u8Regupdate)
        pUsbInfo->otgRegPower = M_POWER_SOFTCONN | M_POWER_HSENAB;
    else
        pUsbInfo->otgRegPower &= ~u8Regupdate;

    USB_REG_WRITE8(M_REG_POWER,pUsbInfo->otgRegPower);
    pUsbInfo->otgRegPower = USB_REG_READ8(M_REG_POWER);
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Clear_DRC_Power
//
//    Description
//        This function will clear USB power related registers.
//
//    Parameters
//        regupdate:    [in] DEVCTL register value.
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Clear_DRC_Devctl(U8 regupdate,USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    if  (!regupdate)
        pUsbInfo->otgRegDevCtl = 0;
    else
        pUsbInfo->otgRegDevCtl &= ~regupdate;

    USB_REG_WRITE8(M_REG_DEVCTL, pUsbInfo->otgRegDevCtl);
    pUsbInfo->otgRegDevCtl = USB_REG_READ8(M_REG_DEVCTL);
}

//------------------------------------------------------------------------------
//
//  Function:    USB_VBus_Status
//
//    Description
//        This function will get USB VBus status.
//
//    Parameters
//        None.
//
//    Return Value
//        None.
//
S32 USB_VBus_Status(void)
{
    U8 u8devctl;

    u8devctl = USB_REG_READ8(M_REG_DEVCTL);

    switch ((u8devctl & VBUS_MASK) >> 3)
    {
        case 0:
            return VBUS_BELOW_SESSION_END;
        case 1:
            return VBUS_ABOVE_SESSION_END;
        case 2:
            return VBUS_ABOVE_AVALID;
        case 3:
            return VBUS_ABOVE_VBUS_VALID;
    }
    return FAILURE;
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Change_OTG_State
//
//    Description
//        This function will change USB OTG state.
//
//    Parameters
//        toOTG:        [in] OTG state.
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Change_OTG_State(U8 u8ToOTG,USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

    USB_RefreshDRCPower(pUsbInfo);
    pUsbInfo->otgCurOTGState = u8ToOTG;

    switch(u8ToOTG)
    {
        case AB_IDLE:
            USB_Clear_DRC_Power(0,pUsbInfo);     /* clear all pending or residue reqs*/
            USB_Clear_DRC_Devctl(0,pUsbInfo); /* ends session */
            break;

        case A_PERIPHERAL:
        case B_PERIPHERAL:
            if (USB_VBus_Status() < VBUS_ABOVE_AVALID)
                USB_Change_OTG_State(AB_IDLE,pUsbInfo);
            USB_DRCReadPower(pUsbInfo);
            if (pUsbInfo->otgRegPower & M_POWER_HSMODE)
                pUsbInfo->otgSpeed = HIGH_SPEED;
            else
                pUsbInfo->otgSpeed = FULL_SPEED;
            break;

        default:
            return;
            break;
    }
}

//------------------------------------------------------------------------------
//
//  Function:    USB_ResetAllEPIoVars
//
//    Description
//        This function will reset all USB all endpoints.
//
//    Parameters
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void  USB_ResetAllEPIoVars(USB_INFO_st *pUsbInfo)
{
    U8 u8Index = 0;

    if (NULL == pUsbInfo)
        return;

    for (u8Index = 0; u8Index < otgNumEPDefs; u8Index++)
    {
        if (DRCHOST(pUsbInfo->otgRegDevCtl))
        {
            pUsbInfo->otgUSB_EP[u8Index].IOState = EP_TX;
        }
        else
        {
            pUsbInfo->otgUSB_EP[u8Index].IOState = EP_IDLE;
        }
        pUsbInfo->otgUSB_EP[u8Index].FifoRemain = 0;
        pUsbInfo->otgUSB_EP[u8Index].BytesRequested = 0;
        pUsbInfo->otgUSB_EP[u8Index].BytesProcessed = 0;
        pUsbInfo->otgUSB_EP[u8Index].LastPacket = 0;
        pUsbInfo->otgUSB_EP[u8Index].Halted = 0;
    }
}

//------------------------------------------------------------------------------
//
//  Function:    USB_ParseDRCIntrUsb
//
//    Description
//        This function will parse USB interrupt.
//
//    Parameters
//        intrusb:            [in] USB Interrupt reasons.
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_ParseDRCIntrUsb(U16 u16Intrusb, USB_INFO_st *pUsbInfo)
{
    if (NULL == pUsbInfo)
        return;

//    RETAILMSG(1, (_T("intrusb = 0x%x\r\n"), u16Intrusb));

    if  (u16Intrusb & M_INTR_VBUSERROR)
    {
//        RETAILMSG(1, (_T("INTERRUPT: M_INTR_VBUSERROR\r\n")));
        USB_Change_OTG_State(AB_IDLE, pUsbInfo);
        USB_REG_WRITE8(M_REG_DEVCTL, USB_REG_READ8(M_REG_DEVCTL) | 0x01); //enable session
    }

    if  (u16Intrusb & M_INTR_CONNECT)
    {
//        RETAILMSG(1, (_T("INTERRUPT: M_INTR_CONNECT\r\n")));
        pUsbInfo->otgFaddr=0;
        USB_REG_WRITE8(M_REG_FADDR, pUsbInfo->otgFaddr);
        USB_Read_CID(pUsbInfo);
        USB_Change_OTG_State(A_HOST, pUsbInfo);
    }

    if  (u16Intrusb & M_INTR_DISCONNECT)
    {
//        RETAILMSG(1, (_T("INTERRUPT: M_INTR_DISCONNECT\r\n")));
        pUsbInfo->otgSuspended = 0;
        pUsbInfo->otgUSBState=0;
        pUsbInfo->u8USBDeviceMode = 0;
        pUsbInfo->DeviceConnect = 0;               //benson add for disconnect
    }

    if  (u16Intrusb & M_INTR_RESET)
    {
//        RETAILMSG(1, (_T("INTERRUPT: M_INTR_RESET\r\n")));

        USB_Read_CID(pUsbInfo);

        if (DRCPERIPHERAL(pUsbInfo->otgRegDevCtl))
        {
            USB_ResetAllEPIoVars(pUsbInfo);//Reset all epoint to idle and zero state.
            pUsbInfo->otgFaddr = 0;
            USB_Change_USB_State(USB_DEFAULT, pUsbInfo);

            if (USB_VBus_Status() < VBUS_ABOVE_AVALID)
            {
//                RETAILMSG(TRUE, (TEXT("OTG otgCurOTGState = %#08X\r\n"), pUsbInfo->otgCurOTGState));
                USB_Change_OTG_State(AB_IDLE, pUsbInfo);
            }
            else if(pUsbInfo->otgCurOTGState == AB_IDLE)
            {
//                RETAILMSG(1, (_T("OTG otgCurOTGState = AB_IDLE\r\n")));
                USB_Change_OTG_State(B_PERIPHERAL, pUsbInfo); //change otg state to B device.
            }
            USB_DRCReadPower(pUsbInfo);

            if (pUsbInfo->otgRegPower & M_POWER_HSMODE)
                pUsbInfo->otgSpeed = HIGH_SPEED;
            else
            {
 //               RETAILMSG(1, (_T("otgSpeed = FULL_SPEED\r\n")));
                pUsbInfo->otgSpeed = FULL_SPEED;
            }
        }
    }

    if  (u16Intrusb & M_INTR_SUSPEND)
    {
         ;//RETAILMSG(1, (_T("INTERRUPT: M_INTR_SUSPEND\r\n")));
    }
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Parse_DRC_Int
//
//    Description
//        This function will check interrupt source.
//
//    Parameters
//        pDrcIntr: [in] Pointer to the structure of DRC_INTR_st.
//        pUsbInfo: [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Parse_DRC_Int(DRC_INTR_st* pDrcIntr, USB_INFO_st *pUsbInfo)
{
    if (NULL == pDrcIntr || NULL == pUsbInfo)
        return;

    USB_RefreshDRCPower(pUsbInfo);

    //KITLOutputDebugString("\r\nUSB_Parse_DRC_Int()........\r\n");

    if  (pDrcIntr->u8IntCause == IRC_INTRUSB)
    {
        //KITLOutputDebugString("OTG Interrupt......................\r\n");
        USB_ParseDRCIntrUsb(pDrcIntr->u16IntReg,pUsbInfo);
    }
    else if (DRCPERIPHERAL(pUsbInfo->otgRegDevCtl))    // Make sure it's an OTG
    {
        //KITLOutputDebugString("EPx Interrupt......................\r\n");
        USB_DRCParseIntPeripheral(pDrcIntr,pUsbInfo);
    }
}

//------------------------------------------------------------------------------
//
//  Function:    USBPollInterrupt
//
//    Description
//        This function will detect USB interrupt source (Polling).
//
//    Parameters
//        None.
//
//    Return Value
//        None.
//
BOOL USBPollInterrupt()
{
    DRC_INTR_st dintItem = {0};
    U8          u8IndexSave = 0;
    U16         u16Usbreg = 0, u16Reg = 0;
    U16         u16Intreg[2] = {0, 0};
    BOOL        bRet = FALSE;

    u8IndexSave = USB_REG_READ8(M_REG_INDEX);  // we have local copy

    u16Usbreg = USB_REG_READ8(M_REG_INTRUSB);  // Get which USB interrupts are currently active (read=clear active interrupts)

    if  (u16Usbreg) // If there's any interrupt of INTRUSB Reg actived
    {
        dintItem.u16IntReg = u16Usbreg;
        dintItem.u8IntCause = IRC_INTRUSB;    // The reason of interrupt is within INTRUSB register
        USB_Parse_DRC_Int(&dintItem, pg_USBInfo);
        bRet = TRUE;
    }

    // Save the which interrupts are currently active for [EP0/Tx EP1-15] and [Rx EP1-15]
    u16Intreg[0] = USB_REG_READ16(M_REG_INTRTX);
    //u16Intreg[1] = USB_REG_READ16(M_REG_INTRRX);

    // Parse EP0 interrupt
    if  (u16Intreg[0] & (1<<USB_EP_CONTROL))
    {
        USB_REG_WRITE8(M_REG_INDEX, 0);  // Select EP0 before accessing an EP's control/status registers at 10h-19h
        u16Reg = USB_REG_READ8(M_REG_CSR0); // Get the cocontent of  EP0's control/ status

        dintItem.u8IntSrc = EP_TX;
        dintItem.u8EpNum = 0;              // EP0 is the source of interrupt
        dintItem.u16IntReg = u16Reg;
        dintItem.u8IntCause = IRC_EP;      // The reason of interrupt is an EP interrupt

        // If M_CSR0_RXPKTRDY bit is set, a data packet has been received
        if  (u16Reg & M_CSR0_RXPKTRDY)
        {
            dintItem.u16RxCount = USB_REG_READ16(M_REG_RXCOUNT); // Read the number of received data bytes in the EP0 FIFO
        }
        USB_Parse_DRC_Int(&dintItem,pg_USBInfo);
        u16Intreg[0] &= ~(1<<USB_EP_CONTROL);  // Finish to parse EP0 TX interrupt
        bRet = TRUE;
    }

    USB_REG_WRITE8(M_REG_INDEX, u8IndexSave);

    USB_SetDRCInterrupts();

    return bRet;
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Parse_Tx
//
//    Description
//        This function will parse USB Endpoint TX interrupt.
//
//    Parameters
//        pDrcIntr: [in/out] Pointer to the USB interrupt variables.
//        pUsbInfo:  [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Parse_Tx(DRC_INTR_st* pDrcIntr, USB_INFO_st *pUsbInfo)
{
    U8 u8Ep;

    if (NULL == pDrcIntr || NULL == pUsbInfo)
        return;

    u8Ep = pDrcIntr->u8EpNum;
    pUsbInfo->otgIntStatus = 0xff;        // unused URB status code
    USB_DRCSelectIndex(u8Ep);             // get our idx in window
    do                                    // while INDEX held ...
    {
        //     3. Handle Tx except EP0
        if (pDrcIntr->u16IntReg & M_TXCSR1_P_SENTSTALL)
        {
//            RETAILMSG(1, (_T("TX_SENTSTALL\r\n")));
            pUsbInfo->otgIntStatus = USB_ST_STALL;
            USB_REG_WRITE8(M_REG_TXCSR1, 0);
            return;
        }

        if  (pUsbInfo->otgUSB_EP[u8Ep].LastPacket)
        {
            // This message will cause KITL fail.
            //RETAILMSG(1, (_T("TX_LastPacket, TXCSR=%#x\r\n"), pDrcIntr->u16IntReg));
            pUsbInfo->otgIntStatus = USB_ST_NOERROR;
            break;
        }

        if  (pUsbInfo->otgUSB_EP[u8Ep].Halted)
        {
//            RETAILMSG(1, (_T("TX_Halt\r\n")));
            USB_REG_WRITE8(M_REG_TXCSR1, M_TXCSR1_P_SENDSTALL);
            break;
        }

        USB_DRCWriteFifo((U8*)pUsbInfo->otgUSB_EP[u8Ep].transfer_buffer, u8Ep, pUsbInfo);
        USB_REG_WRITE8(M_REG_TXCSR1, M_TXCSR1_TXPKTRDY);

//        RETAILMSG(1, (_T("TX_UNKNOWN\r\n")));
    } while (0);

    if (pUsbInfo->otgIntStatus != 0xff)
    {
        if (!(usb_pipeisoc(pUsbInfo->otgUSB_EP[u8Ep].pipe)))
        {
            pUsbInfo->otgUSB_EP[u8Ep].IOState = 0;
            if (pUsbInfo->otgUSB_EP[u8Ep].FifoRemain)
            {
//                RETAILMSG(1, (_T("FifoRemain: %#x\r\n"), pUsbInfo->otgUSB_EP[u8Ep].FifoRemain));
                USB_DRCReadFifo((U8*)pUsbInfo->otgUSB_EP[u8Ep].transfer_buffer, u8Ep, pUsbInfo);
            }
        }
    }
}



//------------------------------------------------------------------------------
//
//  Function:    USBPollTx
//
//    Description
//        This function will poll USB TX Endpoint.
//
//    Parameters
//        None.
//
//    Return Value
//        None.
//
BOOL USBPollTx(void)
{
    DRC_INTR_st dintItem = {0};
    U8          u8IndexSave = 0;
    U8          u8Epnum = 0;
    U16         u16Usbreg = 0, u16Reg = 0;
    U16         u16Index = 0;
    U16         u16Intreg = 0;
    BOOL        bRet = FALSE;

    u8IndexSave = USB_REG_READ8(M_REG_INDEX);  // we have local copy

    u16Usbreg = USB_REG_READ8(M_REG_INTRUSB);  // Get which USB interrupts are currently active (read=clear active interrupts)

    if  (u16Usbreg) // If there's any interrupt of INTRUSB Reg actived
    {
        dintItem.u16IntReg = u16Usbreg;
        dintItem.u8IntCause = IRC_INTRUSB;    // The reason of interrupt is within INTRUSB register
        USB_Parse_DRC_Int(&dintItem, pg_USBInfo);
        //bRet = TRUE;
    }

    // Save which interrupts are currently active for [EP0/Tx EP1-15] and [Rx EP1-15]
    u16Intreg = USB_REG_READ16(M_REG_INTRTX);

    if (u16Intreg)
//        RETAILMSG(0, (_T("M_REG_INTRTX: %#x\r\n"), u16Intreg));
    // Parse EP0 interrupt
    if  (u16Intreg & (1<<USB_EP_CONTROL))
    {
        USB_REG_WRITE8(M_REG_INDEX, 0);  // Select EP0 before accessing an EP's control/status registers at 10h-19h
        u16Reg = USB_REG_READ8(M_REG_CSR0); // Get the cocontent of  EP0's control/ status

        dintItem.u8IntSrc = EP_TX;
        dintItem.u8EpNum = 0;              // EP0 is the source of interrupt
        dintItem.u16IntReg = u16Reg;
        dintItem.u8IntCause = IRC_EP;      // The reason of interrupt is an EP interrupt

        // If M_CSR0_RXPKTRDY bit is set, a data packet has been received
        if  (u16Reg & M_CSR0_RXPKTRDY)
        {
            dintItem.u16RxCount = USB_REG_READ16(M_REG_RXCOUNT); // Read the number of received data bytes in the EP0 FIFO
        }
        USB_Parse_DRC_Int(&dintItem, pg_USBInfo);
        u16Intreg &= ~(1<<USB_EP_CONTROL);  // Finish to parse EP0 TX interrupt
    }

    // Parse all EP TX interrupt except EP0
    dintItem.u8IntCause = IRC_EP;
    u8Epnum = USB_EP_TX;  u16Index = 1<<u8Epnum;

    if (u16Intreg & u16Index)
    {
        USB_REG_WRITE8(M_REG_INDEX, u8Epnum);
        dintItem.u16IntReg = USB_REG_READ8(M_REG_TXCSR1);
        dintItem.u8IntSrc = EP_TX;
        dintItem.u8EpNum = u8Epnum;
        u16Intreg &= ~u16Index;
        USB_RefreshDRCPower(pg_USBInfo);
        USB_Parse_Tx(&dintItem, pg_USBInfo);

        bRet = TRUE;
    }

    USB_REG_WRITE8(M_REG_INDEX, u8IndexSave);

    return bRet;
}

//------------------------------------------------------------------------------
//
//  Function:    USB_Parse_RX
//
//    Description
//        This function will parse USB Endpoint RX interrupt.
//
//    Parameters
//        pUsbInfo:    [in/out] Pointer to the USB interrupt variables.
//        pUsbInfo:    [in/out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
void USB_Parse_RX(DRC_INTR_st *pDrcIntr, USB_INFO_st *pUsbInfo)
{
    U8 u8Ep;

    if (NULL == pDrcIntr || NULL == pUsbInfo)
        return;

    u8Ep = pDrcIntr->u8EpNum;
    pUsbInfo->otgIntStatus = 0xff;        // unused URB status code
    USB_DRCSelectIndex(u8Ep);            // get our idx in window
    do                                    // while INDEX held ...
    {
        // Handle Rx except EP0
        if  (pDrcIntr->u8IntSrc == EP_RX)
        {
            if  ((pDrcIntr->u16IntReg & M_RXCSR1_P_SENTSTALL)||!(pDrcIntr->u16IntReg & M_RXCSR1_RXPKTRDY))
            {
                pUsbInfo->otgIntStatus = USB_ST_STALL;
//                RETAILMSG(1, (_T("RX_STALL\r\n")));
                USB_REG_WRITE8(M_REG_RXCSR1, 0);
                break;
            }
            else if (pUsbInfo->otgUSB_EP[u8Ep].Halted)
            {
//                RETAILMSG(1, (_T("RX_Halt\r\n")));
                USB_REG_WRITE8(M_REG_RXCSR1, M_RXCSR1_P_SENDSTALL);
                break;
            }
            else  //normal condition
            {
                //KITLOutputDebugString("RX_NORMAL\r\n");
                pUsbInfo->otgUSB_EP[u8Ep].FifoRemain = pDrcIntr->u16RxCount;
                //KITLOutputDebugString("RX Count = %d\r\n", pUsbInfo->otgUSB_EP[ep].FifoRemain);

                if ((pUsbInfo->u8USBDevMode == E_USB_VirtCOM)||(pUsbInfo->u8USBDevMode == E_USB_Modem))
                {
                    //KITLOutputDebugString("RX_E_USB_VirtCOM\r\n");
                    pUsbInfo->otgUSB_EP[u8Ep].BytesProcessed = 0;
                    pUsbInfo->otgUSB_EP[u8Ep].BytesRequested = pDrcIntr->u16RxCount;
                    pUsbInfo->otgUSB_EP[u8Ep].transfer_buffer_length = 0;
                    pUsbInfo->otgUSB_EP[u8Ep].FifoRemain = USB_REG_READ16(M_REG_RXCOUNT);
                    //KITLOutputDebugString("Read_Bef = 0x%x\r\n", pUsbInfo->otgUSB_EP[ep].FifoRemain);

                    USB_DRCReadFifo((U8*)(pUsbInfo->otgUSB_EP[u8Ep].transfer_buffer), u8Ep, pUsbInfo);
                    pUsbInfo->otgUSB_EP[u8Ep].FifoRemain = USB_REG_READ16(M_REG_RXCOUNT);

                    ////KITLOutputDebugString("Read_Aft = 0x%x\r\n", pUsbInfo->otgUSB_EP[ep].FifoRemain);

                    USB_REG_WRITE8(M_REG_RXCSR1, 0);
                    ////KITLOutputDebugString("Receive OK!!!\r\n");
                }

                break;
            }
        }
    } while (0);

    if (pUsbInfo->otgIntStatus != 0xff)
    {
        if (!(usb_pipeisoc(pUsbInfo->otgUSB_EP[u8Ep].pipe)))
        {
            pUsbInfo->otgUSB_EP[u8Ep].IOState = 0;
            if (pUsbInfo->otgUSB_EP[u8Ep].FifoRemain)
            {
                USB_DRCReadFifo((U8*)pUsbInfo->otgUSB_EP[u8Ep].transfer_buffer, u8Ep, pUsbInfo);
            }
        }
    }
}

//------------------------------------------------------------------------------
//
//  Function:    USBPollRx
//
//    Description
//        This function will poll USB RX Endpoint.
//
//    Parameters
//        pData:    [out] Pointer to the structure of USB variables.
//        pData:    [out] Pointer to the structure of USB variables.
//
//    Return Value
//        None.
//
BOOL USBPollRx(U8 *pu8Data, U16 *pu16Size)
{
    DRC_INTR_st dintItem = {0};
    U8          u8IndexSave = 0;
    U8          u8Epnum = 0;
    U16         u16Usbreg = 0;
    U16         u16Index = 0;
    U16         u16Intreg = 0;
    BOOL        bRet = FALSE;

    if (NULL == pu8Data || NULL == pu16Size)
        return FALSE;

    u8IndexSave = USB_REG_READ8(M_REG_INDEX);  // we have local copy

    u16Usbreg = USB_REG_READ8(M_REG_INTRUSB);  // Get which USB interrupts are currently active (read=clear active interrupts)

    if  (u16Usbreg) // If there's any interrupt of INTRUSB Reg actived
    {
        dintItem.u16IntReg = u16Usbreg;
        dintItem.u8IntCause = IRC_INTRUSB;    // The reason of interrupt is within INTRUSB register
        USB_Parse_DRC_Int(&dintItem, pg_USBInfo);
    }

    // Save the which interrupts are currently active for [EP0/Tx EP1-15] and [Rx EP1-15]
    u16Intreg = USB_REG_READ16(M_REG_INTRRX);
    if (u16Intreg)
        RETAILMSG(0, (_T("M_REG_INTRRX: %#x\r\n"), u16Intreg));

    //Parse all EP RX interrupt except EP0
    u8Epnum = USB_EP_RX;  u16Index = 1<<u8Epnum;
    if (u16Intreg & u16Index)
    {
        USB_REG_WRITE8(M_REG_INDEX, u8Epnum);

        dintItem.u16IntReg = USB_REG_READ8(M_REG_RXCSR1);
        dintItem.u8IntSrc = EP_RX;
        dintItem.u8EpNum = u8Epnum;
        dintItem.u16RxCount = USB_REG_READ16(M_REG_RXCOUNT);
        USB_RefreshDRCPower(pg_USBInfo);
        pg_USBInfo->otgUSB_EP[dintItem.u8EpNum].transfer_buffer = TYPE_CAST(UINT8*, U32, pu8Data);
        USB_Parse_RX(&dintItem,pg_USBInfo);
        u16Intreg &= ~u16Index;

        *pu16Size = dintItem.u16RxCount;
        bRet = TRUE;
    }
    USB_REG_WRITE8(M_REG_INDEX, u8IndexSave);

    return bRet;

}

BOOL USBPollRxDMA(U8 *pData, U32 *pu32Size)
{
    DRC_INTR_st dintItem = {0};
    U8          u8IndexSave;
    U8          u8EpNum;
    U16         u16UsbReg;
    U16         u16Index;
    U16         u16Intreg = 0;
    BOOL        bRet = FALSE;

    //static ULONG lastPacketLen = 0;

    u8IndexSave = USB_REG_READ8(M_REG_INDEX);  // we have local copy

    u16UsbReg = USB_REG_READ8(M_REG_INTRUSB);  // Get which USB interrupts are currently active (read=clear active interrupts)

    if (u16UsbReg) // If there's any interrupt of INTRUSB Reg actived
    {
        dintItem.u16IntReg = u16UsbReg;
        dintItem.u8IntCause = IRC_INTRUSB; // The reason of interrupt is within INTRUSB register
        USB_Parse_DRC_Int(&dintItem, pg_USBInfo);
    }

    // Save the which interrupts are currently active for [EP0/Tx EP1-15] and [Rx EP1-15]
    u16Intreg = USB_REG_READ16(M_REG_INTRRX);
    if (u16Intreg)
        RETAILMSG(0, (_T("M_REG_INTRRX DMA: %#x\r\n"), u16Intreg));

    //Parse all EP RX interrupt except EP0
    u8EpNum = USB_EP_RX;  u16Index = 1<<u8EpNum;
    if (u16Intreg & u16Index)
    {
        USB_REG_WRITE8(M_REG_INDEX, u8EpNum);
        dintItem.u16IntReg = USB_REG_READ8(M_REG_RXCSR1);
        dintItem.u8IntSrc = EP_RX;
        dintItem.u8EpNum = u8EpNum;
        dintItem.u16RxCount = USB_REG_READ16(M_REG_RXCOUNT);
        RETAILMSG(0, (_T("M_REG_RXCSR1: %#x M_REG_RXCOUNT: %#x\r\n"), dintItem.u16IntReg, dintItem.u16RxCount));
        USB_RefreshDRCPower(pg_USBInfo);
        if (dintItem.u16RxCount)
        {
            USB2SDRAM_BulkDMA(USB_EP_RX, TYPE_CAST(U8*, U32, pData), dintItem.u16RxCount, pg_USBInfo, USB_DMA_MODE0);      // DMA mode 0
        }
        else
        {
            USB_REG_WRITE8(M_REG_RXCSR1, (dintItem.u16IntReg & ~M_RXCSR1_RXPKTRDY));
        }
        u16Intreg &= ~u16Index;
        bRet = TRUE;
        *pu32Size = dintItem.u16RxCount;
    }

    USB_REG_WRITE8(M_REG_INDEX, u8IndexSave);

    return bRet;

}
#endif
