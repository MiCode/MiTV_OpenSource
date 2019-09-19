/*
 *
 *  kdebugd/sec_workq.c
 *
 *  Copyright (C) 2009 Samsung Electronics
 *
 *  2010-12-23  Created by Namit Gupta (gupta.namit@samsung.com)
 *
 */

#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <kdebugd.h>
#include "sec_workq.h"
#define MAX_COUNTER_MONITOR	10

static DEFINE_SPINLOCK(kdbg_workq_lock);

struct kdbg_work_queue kdbg_work_q;

wait_queue_head_t kdbg_workq_head;

int kdbg_usage_core_init(void);

struct kdbg_work_info_t q_get_event(void);
struct kdbg_work_info_t kdbg_workq_get_event(void);

static inline int kdbg_workq_empty(void);

/*
 * Name: usage_work
 * Desc:
 * */
static int kdbg_workq(void *p)
{
	static int run = 1;

	/* worker thread expecting event from the queue
	 * it will be interupted till the new event.
	 * */
	while (run) {		/* Wait for the Event in Queue -- */
		struct kdbg_work_info_t kdbg_work;
		kdbg_work = kdbg_workq_get_event();

		/* Check wthere work is assigned or not */
		if (kdbg_work.work.pwork)
			kdbg_work.work.pwork(kdbg_work.work.data);

		/* Sync required for the work */
		if (kdbg_work.pdone)
			complete(kdbg_work.pdone);
	}
	BUG();

	return 0;
}

static inline int kdbg_workq_empty()
{
	return kdbg_work_q.event_head == kdbg_work_q.event_tail;
}

struct kdbg_work_info_t q_get_event()
{
	kdbg_work_q.event_tail =
	    (kdbg_work_q.event_tail + 1) % WORK_Q_MAX_EVENT;
	return kdbg_work_q.events[kdbg_work_q.event_tail];
}

struct kdbg_work_info_t kdbg_workq_get_event(void)
{
	struct kdbg_work_info_t event = { 0};
	wait_event_interruptible(kdbg_workq_head,
				 !kdbg_workq_empty() || kthread_should_stop());
	spin_lock_irq(&kdbg_workq_lock);
	if (!kdbg_workq_empty())
		event = q_get_event();
	spin_unlock_irq(&kdbg_workq_lock);
	return event;
}

void kdbg_workq_add_event(struct kdbg_work_t event, struct completion *pcomp)
{
	unsigned long flags;

	spin_lock_irqsave(&kdbg_workq_lock, flags);
	kdbg_work_q.event_head =
	    (kdbg_work_q.event_head + 1) % WORK_Q_MAX_EVENT;
	if (kdbg_work_q.event_head == kdbg_work_q.event_tail) {
		static int notified;

		if (notified == 0) {
			PRINT_KD("\n");
			PRINT_KD("kdebugd: core event queue overflowed\n");
			notified = 1;
		}
		kdbg_work_q.event_tail =
		    (kdbg_work_q.event_tail + 1) % WORK_Q_MAX_EVENT;
	}

	kdbg_work_q.events[kdbg_work_q.event_head].work = event;
	kdbg_work_q.events[kdbg_work_q.event_head].pdone = pcomp;

	spin_unlock_irqrestore(&kdbg_workq_lock, flags);
	wake_up_interruptible(&kdbg_workq_head);

}

/*
 * Name: kdbg_usage_core_init
 * Desc: kusage_work
 * */
int kdbg_work_queue_init(void)
{

	struct task_struct *kdbg_work_tsk;
	int ret = 0;

	init_waitqueue_head(&kdbg_workq_head);

	PRINT_KD(" Creating worker thread\n");
	kdbg_work_tsk = kthread_create(kdbg_workq, NULL, "Kdbg_worker");

	if (IS_ERR(kdbg_work_tsk)) {
		ret = PTR_ERR(kdbg_work_tsk);
		kdbg_work_tsk = NULL;
		PRINT_KD("Failed: Kdebugd Worker Thread Creation\n");
		return ret;
	}

	PRINT_KD(" Succsesfuly Created\n");
	kdbg_work_tsk->flags |= PF_NOFREEZE;
	wake_up_process(kdbg_work_tsk);

	return 0;
}
