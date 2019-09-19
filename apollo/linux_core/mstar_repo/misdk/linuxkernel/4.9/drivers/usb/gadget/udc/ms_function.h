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
//      ms_function.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------
#ifndef _USB_FUNCTION_H_
#define _USB_FUNCTION_H_

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include "ms_drc.h"

#define M_FTR_TESTJ                     0x0100
#define M_FTR_TESTK                     0x0200
#define M_FTR_TESTSE0                   0x0300
#define M_FTR_TESTPKT                   0x0400

#ifdef  UVC_BULK_MODE
#define _TOTAL_CONF_LEN                 (CONFIG_DESC_LEN+INTRFASSOC_DESC_LEN+(INTRF_DESC_LEN*2)+CS_VC_TOTAL_LEN+ENDP_DESC_LEN+CS_VS_TOTAL_LEN)
#else
#define _TOTAL_CONF_LEN                 (CONFIG_DESC_LEN+INTRFASSOC_DESC_LEN+(INTRF_DESC_LEN*3)+CS_VC_TOTAL_LEN+ENDP_DESC_LEN+CS_VS_TOTAL_LEN)
#endif
//============== Communication Class Definition ===================

#define	_CONF_LEN_L				        _TOTAL_CONF_LEN & 0x00FF
#define	_CONF_LEN_H				        _TOTAL_CONF_LEN >> 8

/* Interface define */
#define CDC_COMM_INTF_ID            0x00
#define CDC_DATA_INTF_ID            0x01

/* Class-Specific Requests */
#define SEND_ENCAPSULATED_COMMAND   0x00
#define GET_ENCAPSULATED_RESPONSE   0x01
#define SET_COMM_FEATURE            0x02
#define GET_COMM_FEATURE            0x03
#define CLEAR_COMM_FEATURE          0x04
#define SET_LINE_CODING             0x20
#define GET_LINE_CODING             0x21
#define SET_CONTROL_LINE_STATE      0x22
#define SEND_BREAK                  0x23

#define CDC_HEADER_FN_DSC_LEN       0x05
#define CDC_ACM_FN_DSC_LEN          0x04
#define CDC_UNION_FN_DSC_LEN        0x05
#define CDC_CALL_MGT_FN_DSC_LEN     0x05

/* Notifications *
 * Note: Notifications are polled over
 * Communication Interface (Interrupt Endpoint)
 */
#define NETWORK_CONNECTION          0x00
#define RESPONSE_AVAILABLE          0x01
#define SERIAL_STATE                0x20

/* Device Class Code */
#define CDC_DEVICE                  0x02

/* Communication Interface Class Code */
#define COMM_INTF                   0x02

/* Communication Interface Class SubClass Codes */
#define ABSTRACT_CONTROL_MODEL      0x02

/* Communication Interface Class Control Protocol Codes */
#define V25TER                      0x01    // Common AT commands ("Hayes(TM)")

/* Data Interface Class Codes */
#define DATA_INTF                   0x0A

/* Data Interface Class Protocol Codes */
#define NO_PROTOCOL                 0x00    // No class specific protocol required

/* Communication Feature Selector Codes */
#define ABSTRACT_STATE              0x01
#define COUNTRY_SETTING             0x02

/* Functional Descriptors */
/* Type Values for the bDscType Field */
#define CS_INTERFACE                0x24
#define CS_ENDPOINT                 0x25

/* bDscSubType in Functional Descriptors */
#define DSC_FN_HEADER               0x00
#define DSC_FN_CALL_MGT             0x01
#define DSC_FN_ACM                  0x02    // ACM - Abstract Control Management
#define DSC_FN_DLM                  0x03    // DLM - Direct Line Managment
#define DSC_FN_TELEPHONE_RINGER     0x04
#define DSC_FN_RPT_CAPABILITIES     0x05
#define DSC_FN_UNION                0x06
#define DSC_FN_COUNTRY_SELECTION    0x07
#define DSC_FN_TEL_OP_MODES         0x08
#define DSC_FN_USB_TERMINAL         0x09
/* more.... see Table 25 in USB CDC Specification 1.1 */

/* CDC Bulk IN transfer states */
#define CDC_TX_READY                0
#define CDC_TX_BUSY                 1
#define CDC_TX_BUSY_ZLP             2       // ZLP: Zero Length Packet
#define CDC_TX_COMPLETING           3

//============== video Class Definition ===================

/*#define CS_VC_TOTAL_LEN                 VC_INTRF_DESC_LEN+INPUTTERMCCD_DESC_LEN+EXTENSION_DESC_LEN+VIDEO_PU_DESC_LEN+\
                                        OUTPUTTERM_DESC_LEN*/
#define CS_VC_TOTAL_LEN                 VC_INTRF_DESC_LEN+INPUTTERMCCD_DESC_LEN+OUTPUTTERM_DESC_LEN+VIDEO_PU_DESC_LEN
//#define CS_VS_TOTAL_LEN                 VS_HEADER_DESC_LEN+VS_FORMAT_DESC_LEN+VS_FRAME_DESC_LEN+VS_STILL_DESC
#define CS_VS_TOTAL_LEN                 VS_HEADER_DESC_LEN+VS_FORMAT_DESC_LEN+(VS_FRAME_DESC_LEN*3)+0x06

// Descriptor Length Define
#define DEVICE_DESC_LEN     		    0x12
#define CONFIG_DESC_LEN     		    0x09
#define INTRF_DESC_LEN         		    0x09
#define ENDP_DESC_LEN       			0x07
#define INTRFASSOC_DESC_LEN		        0x08
#define VC_INTRF_DESC_LEN		        0x0D
#define INPUTTERMCCD_DESC_LEN           0x12//0x11
#define EXTENSION_DESC_LEN              0x1A
#define VIDEO_PU_DESC_LEN               0x0B
#define OUTPUTTERM_DESC_LEN             0x09
#define VC_INTER_ENDP_DESC_LEN          0x05
#define VS_HEADER_DESC_LEN	            0x0E
#ifdef UNCOMPRESSED_UVC
#define VS_FORMAT_DESC_LEN              0x1B
#define VS_FRAME_DESC_LEN               0x22
#else
#define VS_FORMAT_DESC_LEN              0x0B
#define VS_FRAME_DESC_LEN               0x1E
#endif
#define VS_STILL_DESC                   0x0E

#define ASYNCHRONOUS                    0x04
#define ADAPTIVE                        0x05
#define SYNCHRONOUS                     0x06

#define UVC_DEVICE                      0xEF
#define CDC_DEVICE                      0x02

// Video Interface Class Code
#define	CC_VIDEO						0x0E

// Video Interface Subclass Codes
#define	SC_UNDEFINED					0x00
#define	SC_VIDEOCONTROL					0x01
#define	SC_VIDEOSTREAMING				0x02
#define	SC_VIDEO_INTERFACE_COLLECTION	0x03

// Video Class-Specific Descriptor Types
#define	CS_UNDEFINED					0x20
#define	CS_DEVICE						0x21
#define	CS_CONFIGURATION				0x22
#define	CS_STRING						0x23
#define	CS_INTERFACE					0x24
#define	CS_ENDPOINT						0x25

// Video Class-Specific VC Interface Descriptor Subtypes
#define	VC_DESCRIPTOR_UNDEFINED			0x00
#define	VC_HEADER						0x01
#define	VC_INPUT_TERMINAL				0x02
#define	VC_OUTPUT_TERMINAL				0x03
#define	VC_SELECTOR_UNIT				0x04
#define	VC_PROCESSING_UNIT				0x05
#define	VC_EXTENSION_UNIT				0x06

// Video Class-Specific VS Interface Descriptor Subtypes
#define	VS_INPUT_HEADER					0x01
#define	VS_FORMAT_UNCOMPRESSED			0x04
#define	VS_FRAME_UNCOMPRESSED			0x05
#define	VS_FORMAT_MJPEG					0x06
#define	VS_FRAME_MJPEG 					0x07

enum{
        FrameID640x480 = 1,
        FrameID320x240,
        FrameID160x120,
    };

#define FrameIDNum  3

#define	VC_INTRF					    0x00
#define	VS_INTRF					    0x01
/*
#define	IT_ID						    0x01
#define EX_ID                           0x02
#define	PU_ID						    0x03
#define	OT_ID						    0x04
*/
#define	IT_ID						0x01
#define	PU_ID						0x02
#define	OT_ID						0x03

// Terminal types
#define	VIDEO_USB_TERMINAL    			0x01
#define	VIDEO_IN_TERMINAL     			0x02
#define	VIDEO_OUT_TERMINAL    			0x03
#define	VIDEO_EXTERNAL_TERMINAL			0x04
// USB Terminal Types
#define  TT_VENDOR_SPECIFIC				0x00  // I/O
#define  TT_STREAMING					0x01  // I/O
// Input Terminal Types
#define  ITT_VENDEO_SPECIFIC			0x00
#define  ITT_CAMERA						0x01
#define  ITT_MEDIA_TRANSPORT_INPUT		0x02
// Output Terminal Types
#define  OTT_VENDEO_SPECIFIC			0x00
#define  OTT_DISPLAY					0x01  // LCD,CRT
#define  OTT_MEDIA_TRANSPORT_INPUT		0x02
// External Terminal Types
#define  EXTERNAL_VENDOR_SPECIFIC		0x00  // I/O
#define  COMPOSITE_CONNECTOR			0x01  // I/O
#define  SVIDEO_CONNECTOR				0x02  // I/O
#define  COMPONENT_CONNECTOR			0x03  // I/O

// Descriptor type definition
#define DEVICE            		        0x01
#define CONFIG            		        0x02
#define STRING            		        0x03
#define INTRF             		        0x04
#define ENDPT             		        0x05
#define QUILIFIER         		        0x06
#define OTHERSPEEDCONFIG  	            0x07
#define INTERFASSOC		                0x0B
#define HID               			    0x21
#define REPORT            		        0x22

/* MSD BOT Functional Characteristics */
#define MSDFN_BOT_RESET                 0xFF
#define MSDFN_BOT_GET_MAXLUN            0xFE
/*
 * USB Set Features
 */
#define	ENDPOINT_HALT			        0x00
#define	DEVICE_REMOTE_WAKEUP		    0x01
#define	TEST_MODE			            0x02

#define USB_SUCCESS                         0x00
#define USB_FAILURE                         -1

/* PitcBridge MSD BOT Functional Characteristics */
#define STILL_IMAGE_RESET               0x66
#define STILL_IMAGE_GET_DEVSTS          0x67
#define STILL_STSTUS_OK                 0x2001
#define STILL_STSTUS_BUSY               0x2019
#define STILL_STSTUS_CANSELED           0x201F
/* PitcBridge MSD BOT Functional Characteristics */


#define DEV_DESC_EP0_SIZE_OFFSET    8   /* where to find max ep0 in dscrptor*/
#define SETLSB(x,y) (x) = (((x) & 0xff00) | ((y) & 0xff))
#define SETMSB(x,y) (x) = (((x) & 0xff) | (((y) & 0xff) << 8))
#define GETLSB(x)   ((x) & 0xff)
#define GETMSB(x)   GETLSB((x)>>8)

#define FILL_SETUP(a,b,c,d,e,f) \
    do {\
        (a).bmRequestType = b; \
        (a).bRequest = c; \
        (a).wValue = d; \
        (a).wIndex = e; \
        (a).wLength = f; \
    } while(0)

typedef	u8	pipe_t;

#define SETURB_BUFFER(u,b,l) \
    do {\
        (u)->transfer_buffer = (b);\
        (u)->transfer_buffer_length = (l);\
    }   while (0)

void USB_DRCParseIntPeripheral(DRC_INTR_st* pDrcIntr, USB_INFO_st *pUsbInfo);
s32 USB_Parse_Received_Setup(USB_INFO_st *pUsbInfo);
u8 USB_FnLoadFifo(u8 u8EpNum,USB_INFO_st *pUsbInfo);
void USB_Function_Initial(USB_INFO_st *pUsbInfo);

#ifdef __cplusplus
}
#endif  //__cplusplus

#endif  /* _USB_FUNCTION_H_ */

