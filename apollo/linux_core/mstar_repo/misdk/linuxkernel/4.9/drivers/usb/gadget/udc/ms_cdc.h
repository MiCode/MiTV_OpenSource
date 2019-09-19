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
//      ms_cdc.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include "ms_gvar.h"

/* Line Coding Structure */
#define LINE_CODING_LENGTH		    0x07
#define dummy_length    			0x08

extern u32 volatile  cdcRxBufAlen;  //Benson Add
extern u32 volatile  cdcRxBufBlen;  //Benson Add

typedef union _LINE_CODING
{
    struct
    {
        unsigned char _byte[LINE_CODING_LENGTH];
    }line_coding1;
    struct
    {
        unsigned int       dwDTERate;          // Complex data structure
        unsigned char    bCharFormat;
        unsigned char    bParityType;
        unsigned char    bDataBits1;
    }line_coding2;
    struct
    {
        unsigned short   wDTERate1;          // Complex data structure
        unsigned short   wDTERate2;          // Complex data structure
        unsigned short   Format_Parit;
        unsigned char    bDataBits2;
    }line_coding3;
} LINE_CODING;

typedef union _CONTROL_SIGNAL_BITMAP
{
    unsigned char _byte;
    struct
    {
        unsigned DTE_PRESENT;       // [0] Not Present  [1] Present
        unsigned CARRIER_CONTROL;   // [0] Deactivate   [1] Activate
    };
} CONTROL_SIGNAL_BITMAP;

typedef union _CDC_REQUEST_FLAG
{
    unsigned char _byte;
    struct
    {
        unsigned LINE_CODING : 1;
        unsigned LINE_STATE  : 1;
        unsigned BUFFA_FULL  : 1;
        unsigned BUFFB_FULL  : 1;
    }cdc_req_flag;
} CDC_REQUEST_FLAG;

s32 USBCheckCDCRequest(USB_INFO_st *pUsbInfo);

extern unsigned char dummy_encapsulated_cmd_response[dummy_length];
extern LINE_CODING volatile line_coding;    // Buffer to store line coding information
extern CONTROL_SIGNAL_BITMAP volatile control_signal_bitmap;
extern CDC_REQUEST_FLAG volatile cdc_request_flag;



#ifdef __cplusplus
}
#endif  //__cplusplus
