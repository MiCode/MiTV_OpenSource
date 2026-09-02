#ifndef _MI_NOTIFIER_H_
#define _MI_NOTIFIER_H_

#include <linux/notifier.h>

/*
 * Event Notifier
 **/
#define MI_EVENT_NOTIFY_MASK         (0xFFFF0000)
#define MI_EVENT_NOTIFY_SHIFT        (16)
#define MI_EVENT_NOTIFY_INIT_BOE_PMIC     (MI_EVENT_NOTIFY_MASK&(0x1<<(MI_EVENT_NOTIFY_SHIFT+0)))
#define MI_EVENT_NOTIFY_INIT_CSOT_PMIC    (MI_EVENT_NOTIFY_MASK&(0x1<<(MI_EVENT_NOTIFY_SHIFT+1)))

/*
 * Event Type
 **/
#define MI_EVENT_TYPE_MASK           (0x0000FFFF)
#define MI_EVENT_TYPE_SHIFT          (0)

struct mi_notifier {
	struct notifier_block notifier;
	void *data;
};

struct amp_master_volume {
	int vol_main;
	int vol_top;
	int vol_lfe;
};

int mi_register_client(struct notifier_block *nb);
int mi_unregister_client(struct notifier_block *nb);
int mi_notify_client(unsigned long event, void *val);

#endif
