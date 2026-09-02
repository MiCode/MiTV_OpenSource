/*
 * kernel/power/wakeup_reason.c
 *
 * Logs the reasons which caused the kernel to resume from
 * the suspend mode.
 *
 * Copyright (C) 2020 Google, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/wakeup_reason.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#if defined(CONFIG_MSTAR_PM)
#include <linux/input.h>
#include <mdrv_pm.h>
#endif
#include <linux/slab.h>

/*
 * struct wakeup_irq_node - stores data and relationships for IRQs logged as
 * either base or nested wakeup reasons during suspend/resume flow.
 * @siblings - for membership on leaf or parent IRQ lists
 * @irq      - the IRQ number
 * @irq_name - the name associated with the IRQ, or a default if none
 */
struct wakeup_irq_node {
	struct list_head siblings;
	int irq;
	const char *irq_name;
};

static DEFINE_SPINLOCK(wakeup_reason_lock);

static LIST_HEAD(leaf_irqs);   /* kept in ascending IRQ sorted order */
static LIST_HEAD(parent_irqs); /* unordered */

static struct kmem_cache *wakeup_irq_nodes_cache;

static const char *default_irq_name = "(unnamed)";

static struct kobject *kobj;

static bool capture_reasons;
static bool suspend_abort;
static bool abnormal_wake;
static char non_irq_wake_reason[MAX_SUSPEND_ABORT_LEN];
static bool bWakeUpCEC;

static ktime_t last_monotime; /* monotonic time before last suspend */
static ktime_t curr_monotime; /* monotonic time after last suspend */
static ktime_t last_stime; /* monotonic boottime offset before last suspend */
static ktime_t curr_stime; /* monotonic boottime offset after last suspend */

#if defined(CONFIG_MSTAR_PM)
static struct pwrkey_handle_t {
	int count;
	int nfx_key_cnt;
	unsigned int keycode;
} pwrkey_handle;

// save the wakeup source.
static int s_wakeup_source = -1;

#define PWRKEYSIZE 5
u32 powerkey[PWRKEYSIZE];
EXPORT_SYMBOL(powerkey);

#define NFXKEYSIZE 5
u32 netflixkey[NFXKEYSIZE];
EXPORT_SYMBOL(netflixkey);

#define ADDHOTKEYSIZE 32
struct hot_key {
	char name[20];
	u8 index;
	int key_cnt;
	unsigned int keycode;
};
struct hot_key mtkrc_hotkey[ADDHOTKEYSIZE]={0};
EXPORT_SYMBOL(mtkrc_hotkey);
extern u8 MDrv_PM_GetPowerOnKey(void);
extern void MDrv_PM_CleanPowerOnKey(void);
extern u8 MDrv_PM_GetWakeupSource(void);

int ir_PM51_PWR_profile_parse_num = 0;
EXPORT_SYMBOL(ir_PM51_PWR_profile_parse_num);

int ir_PM51_NFX_profile_parse_num = 0;
EXPORT_SYMBOL(ir_PM51_NFX_profile_parse_num);

int ir_PM51_ADDHOTKEY_profile_parse_num = 0;
EXPORT_SYMBOL(ir_PM51_ADDHOTKEY_profile_parse_num);
#define CUSTOM_POWERKEY_SET   (1L << 31)
int mstar_cust_pwrkey_handler(unsigned int keycode)
{
	int i=0;
    int match = 0;

	/* if CUST_POWERKEY_SET bit is set. always store the keycode */
	if (CUSTOM_POWERKEY_SET & keycode) {
		pwrkey_handle.keycode = keycode ^ CUSTOM_POWERKEY_SET;
		return 0;
	}

	switch (keycode) {
#if 0
		/* poweron key */
		case 0x46:
			pwrkey_handle.keycode = keycode;
			break;
			/* netflix key */
		case 0x03:
			pwrkey_handle.nfx_key_cnt ++;
			pwrkey_handle.keycode = keycode;
			break;
#endif
		default:
			for(i=0;i<ir_PM51_PWR_profile_parse_num;i++) {
				if(keycode==powerkey[i]) {
					pwrkey_handle.keycode = keycode;
                    match = 1;
				}
			}
			for(i=0;i<ir_PM51_NFX_profile_parse_num;i++) {
				if(keycode==netflixkey[i]) {
					pwrkey_handle.nfx_key_cnt ++;
					pwrkey_handle.keycode = keycode;
                    match = 1;
				}
			}
			for(i=0;i<ir_PM51_ADDHOTKEY_profile_parse_num;i++) {
				if(keycode==mtkrc_hotkey[i].keycode) {
					mtkrc_hotkey[i].key_cnt ++;
					pwrkey_handle.keycode = keycode;
                    match = 1;
				}
			}

            if (!match) {  // if no netflix key, clear netflix pwrkey_handle.keycode, otherwise remain value
                for(i=0;i<ir_PM51_NFX_profile_parse_num;i++) {
                    if(pwrkey_handle.keycode==netflixkey[i]) {
                        pwrkey_handle.keycode = 0;
                        pr_info("pwrkey_handle clear netflixkey \n");
                    }
                }
            }
			break;
	}

	return 0;
}
EXPORT_SYMBOL(mstar_cust_pwrkey_handler);

static ssize_t mstar_resume_reason_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	int buf_offset = 0;
	int sel;
	int i=0;
	unsigned int keycode = pwrkey_handle.keycode;
	bool gethotkey=false;
	const char *reason[] = {
		"POWER",
		"NETFLIX",
		"OTHERS",
	};

	if (keycode == 0)
		keycode = MDrv_PM_GetPowerOnKey();

	switch (keycode) {
/*
		case 0x46:
			sel = 0;
			break;
		case 0x03:
			sel = 1;
			break;
*/
		default:
			sel = 2;
			for(i=0;i<ir_PM51_PWR_profile_parse_num;i++) {
				if(keycode==powerkey[i]) {
					sel = 0;
					break;
				}
			}
			for(i=0;i<ir_PM51_NFX_profile_parse_num;i++) {
				if(keycode==netflixkey[i]) {
					sel = 1;
					break;
				}
			}
			for(i=0;i<ir_PM51_ADDHOTKEY_profile_parse_num;i++) {
				if(keycode==mtkrc_hotkey[i].keycode) {
					gethotkey=true;
					break;
				}
			}
	}

	buf_offset += sprintf(buf + buf_offset,
			"power on keycode: %x, reason: %s, resume: %d, netflix_key_count = %d\n",
			(unsigned int)keycode, (gethotkey)?mtkrc_hotkey[i].name:reason[sel], pwrkey_handle.count,
			pwrkey_handle.nfx_key_cnt);

	for(i=0;i<ir_PM51_ADDHOTKEY_profile_parse_num;i++) {
		buf_offset += sprintf(buf + buf_offset,"%s_key_count = %d\n",
				mtkrc_hotkey[i].name,mtkrc_hotkey[i].key_cnt);
	}
	return buf_offset;
}

static ssize_t wakeup_source_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", s_wakeup_source);
}
#endif

static void init_node(struct wakeup_irq_node *p, int irq)
{
	struct irq_desc *desc;

	INIT_LIST_HEAD(&p->siblings);

	p->irq = irq;
	desc = irq_to_desc(irq);
	if (desc && desc->action && desc->action->name)
		p->irq_name = desc->action->name;
	else
		p->irq_name = default_irq_name;
}

static struct wakeup_irq_node *create_node(int irq)
{
	struct wakeup_irq_node *result;

	result = kmem_cache_alloc(wakeup_irq_nodes_cache, GFP_ATOMIC);
	if (unlikely(!result))
		pr_warn("Failed to log wakeup IRQ %d\n", irq);
	else
		init_node(result, irq);

	return result;
}

static void delete_list(struct list_head *head)
{
	struct wakeup_irq_node *n;

	while (!list_empty(head)) {
		n = list_first_entry(head, struct wakeup_irq_node, siblings);
		list_del(&n->siblings);
		kmem_cache_free(wakeup_irq_nodes_cache, n);
	}
}

static bool add_sibling_node_sorted(struct list_head *head, int irq)
{
	struct wakeup_irq_node *n;
	struct list_head *predecessor = head;

	if (unlikely(WARN_ON(!head)))
		return NULL;

	if (!list_empty(head))
		list_for_each_entry(n, head, siblings) {
			if (n->irq < irq)
				predecessor = &n->siblings;
			else if (n->irq == irq)
				return true;
			else
				break;
		}

	n = create_node(irq);
	if (n) {
		list_add(&n->siblings, predecessor);
		return true;
	}

	return false;
}

static struct wakeup_irq_node *find_node_in_list(struct list_head *head,
						 int irq)
{
	struct wakeup_irq_node *n;

	if (unlikely(WARN_ON(!head)))
		return NULL;

	list_for_each_entry(n, head, siblings)
		if (n->irq == irq)
			return n;

	return NULL;
}

void log_irq_wakeup_reason(int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	if (!capture_reasons) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (find_node_in_list(&parent_irqs, irq) == NULL)
		add_sibling_node_sorted(&leaf_irqs, irq);

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

void log_threaded_irq_wakeup_reason(int irq, int parent_irq)
{
	struct wakeup_irq_node *parent;
	unsigned long flags;

	/*
	 * Intentionally unsynchronized.  Calls that come in after we have
	 * resumed should have a fast exit path since there's no work to be
	 * done, any any coherence issue that could cause a wrong value here is
	 * both highly improbable - given the set/clear timing - and very low
	 * impact (parent IRQ gets logged instead of the specific child).
	 */
	if (!capture_reasons)
		return;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	if (!capture_reasons || (find_node_in_list(&leaf_irqs, irq) != NULL)) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	parent = find_node_in_list(&parent_irqs, parent_irq);
	if (parent != NULL)
		add_sibling_node_sorted(&leaf_irqs, irq);
	else {
		parent = find_node_in_list(&leaf_irqs, parent_irq);
		if (parent != NULL) {
			list_del_init(&parent->siblings);
			list_add_tail(&parent->siblings, &parent_irqs);
			add_sibling_node_sorted(&leaf_irqs, irq);
		}
	}

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

void __log_abort_or_abnormal_wake(bool abort, const char *fmt, va_list args)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	/* Suspend abort or abnormal wake reason has already been logged. */
	if (suspend_abort || abnormal_wake) {
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	suspend_abort = abort;
	abnormal_wake = !abort;
	vsnprintf(non_irq_wake_reason, MAX_SUSPEND_ABORT_LEN, fmt, args);

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

void log_suspend_abort_reason(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__log_abort_or_abnormal_wake(true, fmt, args);
	va_end(args);
}

void log_abnormal_wakeup_reason(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__log_abort_or_abnormal_wake(false, fmt, args);
	va_end(args);
}

void clear_wakeup_reasons(void)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	delete_list(&leaf_irqs);
	delete_list(&parent_irqs);
	suspend_abort = false;
	abnormal_wake = false;
	capture_reasons = true;

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

static void print_wakeup_sources(void)
{
	struct wakeup_irq_node *n;
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	capture_reasons = false;

	if (suspend_abort) {
		pr_info("Abort: %s\n", non_irq_wake_reason);
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return;
	}

	if (!list_empty(&leaf_irqs))
		list_for_each_entry(n, &leaf_irqs, siblings)
			pr_info("Resume caused by IRQ %d, %s\n", n->irq,
				n->irq_name);
	else if (abnormal_wake)
		pr_info("Resume caused by %s\n", non_irq_wake_reason);
	else
		pr_info("Resume cause unknown\n");

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}

static ssize_t last_resume_reason_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	ssize_t buf_offset = 0;
	struct wakeup_irq_node *n;
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	if (suspend_abort) {
		buf_offset = scnprintf(buf, PAGE_SIZE, "Abort: %s",
				       non_irq_wake_reason);
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);
		return buf_offset;
	}

	if (!list_empty(&leaf_irqs))
		list_for_each_entry(n, &leaf_irqs, siblings)
			buf_offset += scnprintf(buf + buf_offset,
						PAGE_SIZE - buf_offset,
						"%d %s\n", n->irq, n->irq_name);
	else if (abnormal_wake)
		buf_offset = scnprintf(buf, PAGE_SIZE, "-1 %s",
				       non_irq_wake_reason);

#if defined(CONFIG_MSTAR_PM)
	int sel;
	int i=0;
	unsigned int keycode = pwrkey_handle.keycode;
	bool gethotkey=false;
	const char *reason[] = {
		"POWER",
		"NETFLIX",
		"OTHERS",
	};

	if (keycode == 0)
		keycode = MDrv_PM_GetPowerOnKey();

	switch (keycode) {
		case 0x46:
			sel = 0;
			break;
		case 0x03:
			sel = 1;
			break;
		default:
			sel = 2;
			for(i=0;i<ir_PM51_PWR_profile_parse_num;i++) {
				if(keycode==powerkey[i]) {
					sel = 0;
					break;
				}
			}
			for(i=0;i<ir_PM51_NFX_profile_parse_num;i++) {
				if(keycode==netflixkey[i]) {
					sel = 1;
					break;
			}
		}
		for(i=0;i<ir_PM51_ADDHOTKEY_profile_parse_num;i++) {
			if(keycode==mtkrc_hotkey[i].keycode) {
				gethotkey=true;
				break;
			}
		}
	}

	buf_offset += sprintf(buf + buf_offset,
			"power on keycode: %x, reason: %s, resume: %d, netflix_key_count = %d\n",
			bWakeUpCEC?0x46:(unsigned int)keycode, (gethotkey)?mtkrc_hotkey[i].name:(bWakeUpCEC?reason[0]:reason[sel]), pwrkey_handle.count,
			pwrkey_handle.nfx_key_cnt);

	for(i=0;i<ir_PM51_ADDHOTKEY_profile_parse_num;i++) {
			buf_offset += sprintf(buf + buf_offset,"%s_key_count = %d\n",
			mtkrc_hotkey[i].name,mtkrc_hotkey[i].key_cnt);
	}

#endif

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	return buf_offset;
}

static ssize_t last_suspend_time_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct timespec64 sleep_time;
	struct timespec64 total_time;
	struct timespec64 suspend_resume_time;

	/*
	 * total_time is calculated from monotonic bootoffsets because
	 * unlike CLOCK_MONOTONIC it include the time spent in suspend state.
	 */
	total_time = ktime_to_timespec64(ktime_sub(curr_stime, last_stime));

	/*
	 * suspend_resume_time is calculated as monotonic (CLOCK_MONOTONIC)
	 * time interval before entering suspend and post suspend.
	 */
	suspend_resume_time =
		ktime_to_timespec64(ktime_sub(curr_monotime, last_monotime));

	/* sleep_time = total_time - suspend_resume_time */
	sleep_time = timespec64_sub(total_time, suspend_resume_time);

	/* Export suspend_resume_time and sleep_time in pair here. */
	return sprintf(buf, "%llu.%09lu %llu.%09lu\n",
			suspend_resume_time.tv_sec, suspend_resume_time.tv_nsec,
			sleep_time.tv_sec, sleep_time.tv_nsec);
}

static struct kobj_attribute resume_reason = __ATTR_RO(last_resume_reason);
static struct kobj_attribute suspend_time = __ATTR_RO(last_suspend_time);
#if defined(CONFIG_MSTAR_PM)
static struct kobj_attribute mstar_resume_reason = __ATTR_RO(mstar_resume_reason);
static struct kobj_attribute wakeup_source = __ATTR_RO(wakeup_source);
#endif

static struct attribute *attrs[] = {
	&resume_reason.attr,
	&suspend_time.attr,
#if defined(CONFIG_MSTAR_PM)
	&mstar_resume_reason.attr,
	&wakeup_source.attr,
#endif
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

/* Detects a suspend and clears all the previous wake up reasons*/
static int wakeup_reason_pm_event(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		/* monotonic time since boot */
		last_monotime = ktime_get();
		/* monotonic time since boot including the time spent in suspend */
		last_stime = ktime_get_boottime();
#if defined(CONFIG_MSTAR_PM)
		MDrv_PM_CleanPowerOnKey();
		pwrkey_handle.keycode = 0;
#endif
		clear_wakeup_reasons();
		break;
	case PM_POST_SUSPEND:
		/* monotonic time since boot */
		curr_monotime = ktime_get();
		/* monotonic time since boot including the time spent in suspend */
		curr_stime = ktime_get_boottime();
#if defined(CONFIG_MSTAR_PM)
		if (!suspend_abort) {
			unsigned int key_code = MDrv_PM_GetPowerOnKey();
			/* If wakeup from CEC, ignore keycode handler */
			unsigned int wakeup_source = MDrv_PM_GetWakeupSource();
			if (wakeup_source != PM_WAKEUPSRC_CEC
				&& wakeup_source != PM_WAKEUPSRC_CEC_PORT1
				&& wakeup_source != PM_WAKEUPSRC_CEC_PORT2
				&& wakeup_source != PM_WAKEUPSRC_CEC_PORT3) {
				mstar_cust_pwrkey_handler(key_code);
				bWakeUpCEC=0;
			}
			else
			{
			    bWakeUpCEC=1;
			}

			// save the wakup source.
			s_wakeup_source = wakeup_source;
			pr_crit("[%s] wakeup_source: %d\n", __func__, wakeup_source);
			pwrkey_handle.count ++;
		}
#endif
		print_wakeup_sources();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wakeup_reason_pm_notifier_block = {
	.notifier_call = wakeup_reason_pm_event,
};

int __init wakeup_reason_init(void)
{

#if defined(CONFIG_MSTAR_PM)
	pwrkey_handle.count = 0;
#endif
	if (register_pm_notifier(&wakeup_reason_pm_notifier_block)) {
		pr_warn("[%s] failed to register PM notifier\n", __func__);
		goto fail;
	}

	kobj = kobject_create_and_add("wakeup_reasons", kernel_kobj);
	if (!kobj) {
		pr_warn("[%s] failed to create a sysfs kobject\n", __func__);
		goto fail_unregister_pm_notifier;
	}

	if (sysfs_create_group(kobj, &attr_group)) {
		pr_warn("[%s] failed to create a sysfs group\n", __func__);
		goto fail_kobject_put;
	}

	wakeup_irq_nodes_cache =
		kmem_cache_create("wakeup_irq_node_cache",
				  sizeof(struct wakeup_irq_node), 0, 0, NULL);
	if (!wakeup_irq_nodes_cache)
		goto fail_remove_group;

	return 0;

fail_remove_group:
	sysfs_remove_group(kobj, &attr_group);
fail_kobject_put:
	kobject_put(kobj);
fail_unregister_pm_notifier:
	unregister_pm_notifier(&wakeup_reason_pm_notifier_block);
fail:
	return 1;
}

late_initcall(wakeup_reason_init);
