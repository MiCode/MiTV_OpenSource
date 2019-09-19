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
//      ms_otg.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------
#ifndef _MS_OTG_H_
#define _MS_OTG_H_

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus
#ifdef CONFIG_USB_MSB250X_DMA
#define TX_modify
#define RX_modify_mode1
#define NAK_MODIFY
//#define RX_mode1_log
//#define TX_log
#endif
//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include "ms_gvar.h"

#define	AB_IDLE		    0x00
#define WAIT_VRISE      0x01

#define A_PERIPHERAL    0x21
#define A_WAIT_BCON     0x22
#define A_HOST          0x23
#define A_SUSPEND       0x24

#define B_PERIPHERAL    0x11
#define B_WAIT_ACON     0x12
#define B_HOST          0x13
#define B_SRP_INIT      0x14
/*
 *  OTG State timers, per OTG spec Chapter 5
 *  Unless noted otherwise, constants expressed in milliseconds
 */
#define TA_AIDL_BDIS        250     /* Min */

#define CID_UNKNOWN    2
#define CID_A_DEVICE   0
#define CID_B_DEVICE   1

#define LOW_SPEED       3
#define FULL_SPEED      1
#define HIGH_SPEED      2

#ifndef YES
#define YES 1
#define NO  0
#endif
/*
#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif
*/
#ifndef SUCCESS
#define SUCCESS 1
#define FAILURE -1
#endif

#ifndef NULL
#define NULL    0
#endif

#define AB_PERIPHERAL           0x1
#define AB_WAIT_CON             0x2
#define AB_HOST                 0x3

#define USB_POWERED     1   /* CONNECT int in, Reset pending */
#define USB_DEFAULT     2   /* CONNECT in, Reset completed */
#define USB_ADDRESS     3   /* Above,+ peripheral SET_ADDRESS completed */
#define USB_CONFIGURED  4   /* Above,+ enumeration completed. OK for traffic*/

#define TM_PERIPHERAL_RESUME    10  /* MS for signalling RESUME */
#define TM_HOST_RESUME          30  /* ditto for host */
#define	TS_SESSREQ	    6	/* SRP, WAIT_A_BCON, WAIT_B_ACON */
#define TB_SRP_FAIL         6   /* from B_SRP_INIT to B_PERIPHERAL, 5 ~ 30 sec */

#define EP_EQ_TX(x) (!((x) & 0x01)) /* test for TX mode of endpoint */
#define EP_EQ_RX(x) ((x) & 0x01)    /* test for RX mode of endpoint */
#define EP_EQ_ST(x) ((x) & 0x02)    /* test for ST mode of endpoint */
#define EP_TX       0x00            /* set endpoint to TX only, no status */
#define EP_RX       0x01            /* set endpoint to RX only, no status */
#define EP_TXST     0x02            /* set status in TX direction */
#define EP_RXST     0x03            /* set status in RX direction */
#define EP_IDLE     0x02            /* peripheral only! 2nd bit set is idle */

/* INTRUSB, INTRUSBE */
#define M_INTR_VBUSERROR    0x80   /* only valid when A device */
#define M_INTR_SESSREQ      0x40   /* only valid when A device */
#define M_INTR_DISCONNECT   0x20
#define M_INTR_CONNECT      0x10   /* only valid in Host mode */
#define M_INTR_SOF          0x08
#define M_INTR_RESET        0x04
#define M_INTR_BABBLE       0x04
#define M_INTR_RESUME       0x02
#define M_INTR_SUSPEND      0x01   /* only valid in Peripheral mode */

#define	B_HNP_ENABLE			0x03
#define	A_HNP_SUPPORT			0x04
#define	A_ALT_HNP_SUPPORT		0x05

#define AB_MODE(x)  ((x) & 0xf) /* mask off A-B indicator */
#define DRCHOST(x)          ((x) & M_DEVCTL_HM)
#define DRCPERIPHERAL(x)    (!((x) & M_DEVCTL_HM))
#define DRC_HIGHSPEED(x)    ((x) & M_POWER_HSMODE)


#ifdef __cplusplus
}
#endif  //__cplusplus

#endif  /* define _MS_OTG_H_ */
