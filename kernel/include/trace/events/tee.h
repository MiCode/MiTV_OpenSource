/*
 * MediaTek Inc. (C) 2019. All rights reserved.
 *
 * Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tee

#if !defined(_TRACE_TEE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TEE_H
#include <linux/tracepoint.h>
#include <linux/time.h>
#include <linux/trace_events.h>

#define MTK_TRACE_TEE_START(buf, param, start) \
	do { \
		start = ktime_get(); \
		trace_tee_start(buf, param, ktime_to_ms(start)); \
	} while (0)

#define MTK_TRACE_TEE_END(buf, param, ret, start, end) \
	do { \
		s64 diff_ms; \
		end = ktime_get(); \
		diff_ms = ktime_to_ms(ktime_sub(end, start)); \
		trace_tee_end(buf, param, ret, ktime_to_ms(start), \
				ktime_to_ms(end), diff_ms); \
	} while (0)

/*
 * Tracepoint for calling tee_end:
 */
TRACE_EVENT(tee_end,

	TP_PROTO(char	*prefix,
		uint32_t param,
		uint32_t ret,
		s64	t1,
		s64	t2,
		s64 	diff),

	TP_ARGS(prefix, param, ret, t1, t2, diff),

	TP_STRUCT__entry(
		__array(	char,	prefix,		36	)
		__field(	s64,			t1	)
		__field(	s64,			t2	)
		__field(	s64,			diff	)
		__field(	uint32_t,		param	)
		__field(	uint32_t,		ret	)
	),

	TP_fast_assign(
		strcpy(__entry->prefix, prefix);
		__entry->t1	= t1;
		__entry->t2	= t2;
		__entry->diff	= diff;
		__entry->param	= param;
		__entry->ret	= ret;
	),

	TP_printk("%s param:%x ret:%x start:%lld end:%lld cost:%lld", __entry->prefix,
		__entry->param, __entry->ret,
		__entry->t1, __entry->t2, __entry->diff)
);

TRACE_EVENT(tee_start,

	TP_PROTO(char *prefix,
		uint32_t param,
		s64	t1),

	TP_ARGS(prefix, param, t1),

	TP_STRUCT__entry(
		__array(	char,	prefix,		36	)
		__field(	s64,			t1	)
		__field(	uint32_t,		param	)
	),

	TP_fast_assign(
		strcpy(__entry->prefix, prefix);
		__entry->t1	= t1;
		__entry->param	= param;
	),

	TP_printk("%s param:%x start:%lld", __entry->prefix,
		__entry->param, __entry->t1)
);
#endif
/* This part must be outside protection */
#include <trace/define_trace.h>
