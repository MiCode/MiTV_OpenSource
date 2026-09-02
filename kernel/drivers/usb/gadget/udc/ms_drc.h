/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/******************************************************************************
 *
 * This file is provided under a dual license.  When you use or
 * distribute this software, you may choose to be licensed under
 * version 2 of the GNU General Public License ("GPLv2 License")
 * or BSD License.
 *
 * GPLv2 License
 *
 * Copyright(C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 *
 * BSD LICENSE
 *
 * Copyright(C) 2019 MediaTek Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

//-----------------------------------------------------------------------------
// FILE
//      ms_drc.h
//
// DESCRIPTION
//
//
// HISTORY
//
//-----------------------------------------------------------------------------
#ifndef _MS_MSD_DRC_H_
#define _MS_MSD_DRC_H_

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------
#include "ms_gvar.h"
#include "ms_cpu.h"

#define M_REG_FADDR			(0x00)
#define M_REG_POWER			(0x01)
#define M_REG_CFG6_H		((0x8C << OffShift) + 1)
#define M_REG_INTRTX		(0x02 << OffShift)
#define M_REG_INTRRX		(0x04 << OffShift)
#define M_REG_INTRTXE		(0x06 << OffShift)
#define M_REG_INTRRXE		(0x08 << OffShift)
#define M_REG_INTRUSB		(0x0A << OffShift)
#define M_REG_INTRUSBE		((0x0A << OffShift) + 1)
#define M_REG_FRAME			(0x0C << OffShift)
#define M_REG_INDEX			(0x0E << OffShift)
#define M_REG_TESTMODE		((0x0E << OffShift) + 1)
#define M_REG_TXMAXP		(0x10 << OffShift)
#define M_REG_CSR0			(0x12 << OffShift)
#define M_REG_TXCSR			(0x12 << OffShift)
#define M_REG_RXMAXP		(0x14 << OffShift)
#define M_REG_RXCSR			(0x16 << OffShift)
#define M_REG_COUNT0		(0x18 << OffShift)
#define M_REG_RXCOUNT		(0x18 << OffShift)
#define	M_REG_DEVCTL		(0x60 << OffShift)

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
#endif  //_MS_MSD_DRC_H_
