/*
 *  kdebugd/sec_workq.h
 *
 *  Copyright (C) 2009 Samsung Electronics
 *
 *  2010-12-23  Created by Namit Gupta (gupta.namit@samsung.com)
 *
 */
#ifndef __SEC_WORKQ_H__
#define __SEC_WORKQ_H__

#include <linux/completion.h>

/* Work Structure */
struct kdbg_work_t {
	void *data;
	void (*pwork) (void *data);
};

/* Information Of Work */
struct kdbg_work_info_t {
	struct completion *pdone;
	struct kdbg_work_t work;
};

#define WORK_Q_MAX_EVENT 10

struct kdbg_work_queue {
	unsigned int event_head;
	unsigned int event_tail;
	struct kdbg_work_info_t events[WORK_Q_MAX_EVENT];
};

void kdbg_workq_add_event(struct kdbg_work_t event, struct completion *pcomp);

int kdbg_work_queue_init(void);
#endif /*__SEC_WORKQ_H__*/
