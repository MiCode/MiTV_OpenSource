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
//      ms_cpu.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------
#ifndef _MS_CPU_H_
#define _MS_CPU_H_

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus
//#include "mach/platform.h"

#define OTG_BIT0                    0x00000001
#define OTG_BIT1                    0x00000002
#define OTG_BIT2                    0x00000004
#define OTG_BIT3                    0x00000008
#define OTG_BIT4                    0x00000010
#define OTG_BIT5                    0x00000020
#define OTG_BIT6                    0x00000040
#define OTG_BIT7                    0x00000080
#define OTG_BIT8                    0x00000100
#define OTG_BIT9                    0x00000200
#define OTG_BIT10                   0x00000400
#define OTG_BIT11                   0x00000800
#define OTG_BIT12                   0x00001000
#define OTG_BIT13                   0x00002000
#define OTG_BIT14                   0x00004000
#define OTG_BIT15                   0x00008000
#define OTG_BIT16                   0x00010000
#define OTG_BIT17                   0x00020000
#define OTG_BIT18                   0x00040000
#define OTG_BIT19                   0x00080000
#define OTG_BIT20                   0x00100000
#define OTG_BIT21                   0x00200000
#define OTG_BIT22                   0x00400000
#define OTG_BIT23                   0x00800000
#define OTG_BIT24                   0x01000000
#define OTG_BIT25                   0x02000000
#define OTG_BIT26                   0x04000000
#define OTG_BIT27                   0x08000000
#define OTG_BIT28                   0x10000000
#define OTG_BIT29                   0x20000000
#define OTG_BIT30                   0x40000000
#define OTG_BIT31                   0x80000000

#define USB_REG_READ8(r)                readb((void *)(OTG0_BASE_ADDR + (r)))
#define USB_REG_READ16(r)               readw((void *)(OTG0_BASE_ADDR + (r)))
#define USB_REG_WRITE8(r,v)             writeb(v,(void *)(OTG0_BASE_ADDR + r))//OUTREG8(otgRegAddress + r, v)
#define USB_REG_WRITE16(r,v)            writew(v,(void *)(OTG0_BASE_ADDR + r))//OUTREG16(otgRegAddress + r, v)

#define UTMI_REG_READ8(r)               readb((void *)(UTMI_BASE_ADDR + (r)))
#define UTMI_REG_READ16(r)              readw((void *)(UTMI_BASE_ADDR + (r)))
#define UTMI_REG_WRITE8(r,v)            writeb(v,(void *)(UTMI_BASE_ADDR + r))//OUTREG8(utmiRegAddress + r, v)
#define UTMI_REG_WRITE16(r,v)           writew(v,(void *)(UTMI_BASE_ADDR + r))//OUTREG16(utmiRegAddress + r, v)

#define USBC_REG_READ8(r)               readb((void *)(USBC_BASE_ADDR + (r)))
#define USBC_REG_READ16(r)              readw((void *)(USBC_BASE_ADDR + (r)))
#define USBC_REG_WRITE8(r,v)            writeb(v,(void *)(USBC_BASE_ADDR + r))//OUTREG8(usbcRegAddress + r, v)
#define USBC_REG_WRITE16(r,v)           writew(v,(void *)(USBC_BASE_ADDR + r))//OUTREG16(usbcRegAddress + r, v)

#define FIFO_ADDRESS(e) (OTG0_BASE_ADDR + (e<<3) + M_FIFO_EP0)
#define FIFO_DATA_PORT  (OTG0_BASE_ADDR + M_REG_FIFO_DATA_PORT)


#ifdef BIG_ENDIAN
#define SWOP(X) ((X) = (((X)<<8)+((X)>>8)))
#define SWAP4(X) ((X) = ((X)<<24) + ((X)>>24) + (((X)>>8)&0x0000FF00) + (((X)<<8)&0x00FF0000) )
#else
#define SWAP4(X) (X = X)
#define SWOP(X)  (X = X)
#endif

#define RETAILMSG(a, b) printk(b)
#define _T(a) (KERN_INFO a)
#define TEXT
#define TRUE	1
#define FALSE	0

#ifdef __cplusplus
}
#endif  //__cplusplus

#endif  /* _MS_CPU_H_ */

