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
//      ms_config.h
//
// DESCRIPTION
//
// HISTORY
//
//------------------------------------------------------------------------------
#ifndef _MS_CONFIG_H_
#define _MS_CONFIG_H_

//#include <mach/hardware.h>
#include "msb250x_udc_reg.h"

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

#if defined(CONFIG_ARCH_CEDRIC)
#define MSB250X_BASE_REG_RIU_PA		0xfd200000
#define usbcRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x0700*2)
#define otgRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x51000*2)
#define utmiRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x3A80*2)
#endif
#if 0//defined(CONFIG_ARCH_CHICAGO)
#define MSB250X_BASE_REG_RIU_PA		0xfd000000
#define usbcRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x2500*2)
#define otgRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x2600*2)
#define utmiRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x1F00*2)
#endif
#if defined(CONFIG_ARCH_INFINITY) || defined(CONFIG_ARCH_INFINITY3)
#define MSB250X_BASE_REG_RIU_PA		0xfd200000
#define usbcRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x42300*2)
#define otgRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x42500*2)
#define utmiRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x42100*2)
#endif
#if defined(CONFIG_ARCH_CLEVELAND)
#define MSB250X_BASE_REG_RIU_PA		0xfd000000
#define usbcRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x2500*2)
#define otgRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x2600*2)
#define utmiRegAddress   			(MSB250X_BASE_REG_RIU_PA+0x10200*2)
#endif
#define OffShift        					1
#define otgNumEPDefs    				4
#define SCSI_BLOCK_NUM  			5000
#define SCSI_BLOCK_SIZE 			512
#define EVB_Board
#define MASS_BUFFER_SIZE 			(4 * 1024)
#define MAX_DMA_CHANNEL  			1

#define MASS_BUFFER_SIZE2 			0x10000
#define MASS_TRN_LEN 				8
#define Enable_Read_Write_Test
#define Force_Host_Mode
#define UVC_BULK_MODE

#ifdef __cplusplus
}
#endif  //__cplusplus

#endif  /* _MS_CONFIG_H_ */

