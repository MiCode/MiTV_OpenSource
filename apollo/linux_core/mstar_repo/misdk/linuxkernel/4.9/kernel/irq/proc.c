/*
 * linux/kernel/irq/proc.c
 *
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains the /proc/irq/ handling code.
 */

#include <linux/irq.h>
#include <linux/gfp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <mstar/mpatch_macro.h>

#if (MP_PLATFORM_ARCH_GENERAL == 1)
#include <linux/poll.h>
#include <linux/sched.h>
#include "chip_int.h"
#include <linux/slab.h>
#include <internal.h>	/* fs/proc/internal.h */
#endif/*MP_PLATFORM_ARCH_GENERAL*/

#include "internals.h"
#include "chip_int.h"

/*
 * Access rules:
 *
 * procfs protects read/write of /proc/irq/N/ files against a
 * concurrent free of the interrupt descriptor. remove_proc_entry()
 * immediately prevents new read/writes to happen and waits for
 * already running read/write functions to complete.
 *
 * We remove the proc entries first and then delete the interrupt
 * descriptor from the radix tree and free it. So it is guaranteed
 * that irq_to_desc(N) is valid as long as the read/writes are
 * permitted by procfs.
 *
 * The read from /proc/interrupts is a different problem because there
 * is no protection. So the lookup and the access to irqdesc
 * information must be protected by sparse_irq_lock.
 */
static struct proc_dir_entry *root_irq_dir;

#ifdef CONFIG_SMP

static int show_irq_affinity(int type, struct seq_file *m, void *v)
{
	struct irq_desc *desc = irq_to_desc((long)m->private);
	const struct cpumask *mask = desc->irq_common_data.affinity;

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (irqd_is_setaffinity_pending(&desc->irq_data))
		mask = desc->pending_mask;
#endif
	if (type)
		seq_printf(m, "%*pbl\n", cpumask_pr_args(mask));
	else
		seq_printf(m, "%*pb\n", cpumask_pr_args(mask));
	return 0;
}

static int irq_affinity_hint_proc_show(struct seq_file *m, void *v)
{
	struct irq_desc *desc = irq_to_desc((long)m->private);
	unsigned long flags;
	cpumask_var_t mask;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	raw_spin_lock_irqsave(&desc->lock, flags);
	if (desc->affinity_hint)
		cpumask_copy(mask, desc->affinity_hint);
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	seq_printf(m, "%*pb\n", cpumask_pr_args(mask));
	free_cpumask_var(mask);

	return 0;
}

#ifndef is_affinity_mask_valid
#define is_affinity_mask_valid(val) 1
#endif

int no_irq_affinity;
static int irq_affinity_proc_show(struct seq_file *m, void *v)
{
	return show_irq_affinity(0, m, v);
}

static int irq_affinity_list_proc_show(struct seq_file *m, void *v)
{
	return show_irq_affinity(1, m, v);
}


static ssize_t write_irq_affinity(int type, struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	unsigned int irq = (int)(long)PDE_DATA(file_inode(file));
	cpumask_var_t new_value;
	int err;

	if (!irq_can_set_affinity_usr(irq) || no_irq_affinity)
		return -EIO;

	if (!alloc_cpumask_var(&new_value, GFP_KERNEL))
		return -ENOMEM;

	if (type)
		err = cpumask_parselist_user(buffer, count, new_value);
	else
		err = cpumask_parse_user(buffer, count, new_value);
	if (err)
		goto free_cpumask;

	if (!is_affinity_mask_valid(new_value)) {
		err = -EINVAL;
		goto free_cpumask;
	}

	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	if (!cpumask_intersects(new_value, cpu_online_mask)) {
		/* Special case for empty set - allow the architecture
		   code to set default SMP affinity. */
		err = irq_select_affinity_usr(irq, new_value) ? -EINVAL : count;
	} else {
		irq_set_affinity(irq, new_value);
		err = count;
	}

free_cpumask:
	free_cpumask_var(new_value);
	return err;
}

static ssize_t irq_affinity_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	return write_irq_affinity(0, file, buffer, count, pos);
}

static ssize_t irq_affinity_list_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	return write_irq_affinity(1, file, buffer, count, pos);
}

static int irq_affinity_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, irq_affinity_proc_show, PDE_DATA(inode));
}

static int irq_affinity_list_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, irq_affinity_list_proc_show, PDE_DATA(inode));
}

static int irq_affinity_hint_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, irq_affinity_hint_proc_show, PDE_DATA(inode));
}

static const struct file_operations irq_affinity_proc_fops = {
	.open		= irq_affinity_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= irq_affinity_proc_write,
};

static const struct file_operations irq_affinity_hint_proc_fops = {
	.open		= irq_affinity_hint_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations irq_affinity_list_proc_fops = {
	.open		= irq_affinity_list_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= irq_affinity_list_proc_write,
};

static int default_affinity_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%*pb\n", cpumask_pr_args(irq_default_affinity));
	return 0;
}

static ssize_t default_affinity_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	cpumask_var_t new_value;
	int err;

	if (!alloc_cpumask_var(&new_value, GFP_KERNEL))
		return -ENOMEM;

	err = cpumask_parse_user(buffer, count, new_value);
	if (err)
		goto out;

	if (!is_affinity_mask_valid(new_value)) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	if (!cpumask_intersects(new_value, cpu_online_mask)) {
		err = -EINVAL;
		goto out;
	}

	cpumask_copy(irq_default_affinity, new_value);
	err = count;

out:
	free_cpumask_var(new_value);
	return err;
}

static int default_affinity_open(struct inode *inode, struct file *file)
{
	return single_open(file, default_affinity_show, PDE_DATA(inode));
}

static const struct file_operations default_affinity_proc_fops = {
	.open		= default_affinity_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= default_affinity_write,
};

static int irq_node_proc_show(struct seq_file *m, void *v)
{
	struct irq_desc *desc = irq_to_desc((long) m->private);

	seq_printf(m, "%d\n", irq_desc_get_node(desc));
	return 0;
}

static int irq_node_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, irq_node_proc_show, PDE_DATA(inode));
}

static const struct file_operations irq_node_proc_fops = {
	.open		= irq_node_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int irq_spurious_proc_show(struct seq_file *m, void *v)
{
	struct irq_desc *desc = irq_to_desc((long) m->private);

	seq_printf(m, "count %u\n" "unhandled %u\n" "last_unhandled %u ms\n",
		   desc->irq_count, desc->irqs_unhandled,
		   jiffies_to_msecs(desc->last_unhandled));
	return 0;
}

static int irq_spurious_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, irq_spurious_proc_show, PDE_DATA(inode));
}

static const struct file_operations irq_spurious_proc_fops = {
	.open		= irq_spurious_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

struct irq_proc {
	int irq;
	wait_queue_head_t q;
	atomic_t count;
	char devname[TASK_COMM_LEN];
	int debug_mode;
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	unsigned int mask;
	unsigned int completion;
	pid_t pid;
	struct list_head list;
	int irq_push_exit;
#endif
};

static DEFINE_MUTEX(irq_proc_mutex);

#ifdef CONFIG_MP_PLATFORM_ARCH_GENERAL

#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
#define UTOPIA_DEBUG 0

#define  PRINT_KD(fmt , args ...)  	 printk(PRINT_KD_STR fmt, ##args)
/*#define DEBUG */
# if UTOPIA_DEBUG
# define dprintk(x...) PRINT_KD(x)
# else
# define dprintk(x...)
# endif

#define E_IRQ_DISABLE           (0)
#define E_IRQ_ENABLE            (1)
#define E_IRQ_ACK               (2)
#define E_IRQ_COMPLETE          (3)
#define E_IRQ_DEBUG_STATUS_FLOW (4)
#define E_IRQ_PUSH_EXIT         (5)
#define E_IRQ_DEBUG_DISABLE     (1 << 31)

struct irq_proc_head {
	atomic_t num;
	int irq;
	unsigned int mask;
	spinlock_t mask_lock;
	struct mutex head_lock;
	atomic_t complete_num;
	struct list_head list;
};

static struct irq_proc_head irq_proc_head[NR_IRQS];

/*
 * update head->mask by ANDing all irq_proc->mask with same irq
 * and return it
 */
static unsigned int all_threads_mask(struct irq_proc_head *head)
{
	unsigned long flags;
	struct irq_proc *ip;

	spin_lock_irqsave(&head->mask_lock, flags);
	head->mask = true;
	list_for_each_entry(ip, &head->list, list)
		head->mask &= ip->mask;
	spin_unlock_irqrestore(&head->mask_lock, flags);

	return head->mask;
}

/*
 * ANDing all irq_proc->completion and return it
 */
static unsigned int all_threads_complete(struct irq_proc_head *head)
{
	unsigned long flags;
	struct irq_proc *ip;
	unsigned int completion = true;

	spin_lock_irqsave(&head->mask_lock, flags);
	list_for_each_entry(ip, &head->list, list)
		completion &= ip->completion;
	spin_unlock_irqrestore(&head->mask_lock, flags);

	return completion;
}

/*
static void show_irq_list(struct irq_proc_head *cur_top)
{
	struct irq_proc *ip;
	unsigned long flags;

	spin_lock_irqsave(&cur_top->mask_lock, flags);
	dprintk("---------------------------------------------------------------------------------------\n");
	dprintk("show list\n");
	dprintk("cur_top->mask=%d, cur_top->irq=%d, cur_top->num=%d\n", cur_top->mask,cur_top->irq, cur_top->num);

	list_for_each_entry(ip, &cur_top->list, list) {
		dprintk("ip->irq=%d, ip->devname=%s, ip->pid=%d, ip->mask=%d, ip->completion=%d, ip->count=%d\n",
			ip->irq, ip->devname, ip->pid, ip->mask, ip->completion, ip->count);
	}
	dprintk("\n\n");
	spin_unlock_irqrestore(&cur_top->mask_lock, flags);
}
*/

#endif

/*
 * increment count & wake up unmasked threads
 */
static irqreturn_t irq_proc_irq_handler(int irq, void *desc)
{
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	struct irq_proc_head *head = (struct irq_proc_head *)desc;
	struct irq_proc *ip;
	unsigned long flags;

	BUG_ON(head->irq != irq);

	dprintk("irq_proc_irq_handler:\n");
	disable_irq_nosync(irq);

	spin_lock_irqsave(&head->mask_lock, flags);
	list_for_each_entry(ip, &head->list, list) {
		if(ip->mask == false) {
			ip->mask = true;
			ip->completion = false;
			atomic_inc(&ip->count);
			atomic_inc(&head->complete_num);
			wake_up(&ip->q);
		}
	}
	spin_unlock_irqrestore(&head->mask_lock, flags);

	return IRQ_HANDLED;
#else
	struct irq_proc *idp = (struct irq_proc *)desc;

	BUG_ON(idp->irq != irq);

    if(idp->debug_mode & E_IRQ_DEBUG_STATUS_FLOW)
		printk("\ncpu = %X, status = DISABLE, irq = %d\n"
				, task_cpu(current), irq);

	disable_irq_nosync(irq);
	atomic_inc(&idp->count);
	wake_up(&idp->q);

	return IRQ_HANDLED;
#endif
}

/*
 * Signal to userspace an interrupt has occured.
 * Note: no data is ever transferred to/from user space!
 */
ssize_t irq_proc_read(struct file *fp, char *bufp, size_t len, loff_t *where)
{
	struct irq_proc *ip = (struct irq_proc *)fp->private_data;
	irq_desc_t *idp = irq_to_desc(ip->irq);
	int i;
	int err;

	DEFINE_WAIT(wait);

	if (len < sizeof(int))
		return -EINVAL;

	if ((i = atomic_read(&ip->count)) == 0) {
		if (idp->status_use_accessors & IRQD_IRQ_DISABLED)
			enable_irq(ip->irq);
		if (fp->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
	}

	while (i == 0) {
		prepare_to_wait(&ip->q, &wait, TASK_INTERRUPTIBLE);
		if ((i = atomic_read(&ip->count)) == 0)
			schedule();
		finish_wait(&ip->q, &wait);
		if (signal_pending(current))
		{
			return -ERESTARTSYS;
		}
	}

	if ((err = copy_to_user(bufp, &i, sizeof i)))
		return err;
	*where += sizeof i;

	atomic_sub(i, &ip->count);
	return sizeof i;
}

/*
 * according to write option
 * enable/disable/complete/debug irq
 */
static ssize_t irq_proc_write(struct file *fp,
		const char *bufp, size_t len, loff_t *where)
{
	struct irq_proc *ip = (struct irq_proc *)fp->private_data;
	int option;
	int err;
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	struct irq_proc_head *head = &irq_proc_head[ip->irq];
	unsigned long flags;
#endif

	if (len < sizeof(int))
		return -EINVAL;

	if ((err = copy_from_user(&option, bufp, sizeof(option))))
		return err;

	switch (option) {
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
		case E_IRQ_ENABLE:
			ip->mask = false;
			spin_lock_irqsave(&head->mask_lock, flags);
			head->mask = false;
			spin_unlock_irqrestore(&head->mask_lock, flags);

			/*
			 * if num == 1, we help users "complete" for compatibility
			 * else, they have to explicitly notify kernel of the completion
			 */
			if (atomic_read(&head->num) == 1)
				goto complete;
			atomic_dec(&head->complete_num);
			break;

		case E_IRQ_COMPLETE:
		if (atomic_read(&head->num) == 1)
				break;
complete:
			mutex_lock(&head->head_lock);
			ip->completion = true;
			atomic_dec(&head->complete_num);
			if(all_threads_complete(head) == true &&
					all_threads_mask(head) == false)
				/* do not enable irq if already enableld */
				if ((irq_to_desc(ip->irq)->irq_common_data.state_use_accessors &
							IRQD_IRQ_DISABLED))
					enable_irq(ip->irq);
			mutex_unlock(&head->head_lock);
			break;

		case E_IRQ_DISABLE:
			ip->mask = true;
			if(all_threads_mask(head) == true)
				if ((irq_to_desc(ip->irq)->irq_common_data.state_use_accessors
					& IRQD_IRQ_DISABLED)!= IRQD_IRQ_DISABLED)
				disable_irq_nosync(ip->irq);
			break;
#else
	case E_IRQ_ENABLE:
		if(ip->debug_mode & E_IRQ_DEBUG_STATUS_FLOW)
			printk("\ncpu = %d, status = ENABLE, irq = %d\n"
					, task_cpu(current), ip->irq);
		/* do not enable irq if already enableld */
		if((irq_to_desc(ip->irq)->irq_common_data.state_use_accessors &
					IRQD_IRQ_DISABLED))
			enable_irq(ip->irq);

		break;

	case E_IRQ_DISABLE:
		disable_irq_nosync(ip->irq);
		break;
#endif

	case E_IRQ_ACK:
		if(ip->debug_mode & E_IRQ_DEBUG_STATUS_FLOW)
			printk("\ncpu = %d, status = ACK, irq = %d\n"
					, task_cpu(current), ip->irq);
		break;

	case E_IRQ_DEBUG_DISABLE:
		ip->debug_mode = 0;
		break;
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	case E_IRQ_PUSH_EXIT:
		ip->irq_push_exit = 0xBEEF;
		wake_up(&ip->q);
		break;
#endif

	default:
		ip->debug_mode = option;
		break;
	}

	*where += sizeof(option);
	return sizeof(option);
}

/*
 * initialize irq_proc and, if necessary, irq_proc_head
 */
static int irq_proc_open(struct inode *inop, struct file *fp)
{
	struct irq_proc *ip;
	struct proc_dir_entry *ent = PDE(inop);
	int error;
	unsigned long  irqflags;
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	struct irq_proc_head *head;
#endif

	mutex_lock(&irq_proc_mutex);

	ip = kzalloc(sizeof *ip, GFP_KERNEL);
	if (!ip) {
		mutex_unlock(&irq_proc_mutex);
		return -ENOMEM;
	}

	strncpy(ip->devname, current->comm, TASK_COMM_LEN - 1);
	init_waitqueue_head(&ip->q);
	atomic_set(&ip->count, 0);
	ip->irq = (size_t)ent->data;

#ifdef CONFIG_MP_PLATFORM_INT_1_to_1_SPI
	irqflags = (ip->irq == E_IRQ_DISP) ? IRQF_SHARED | IRQF_ONESHOT : SA_INTERRUPT | IRQF_ONESHOT;
#else
	irqflags = (ip->irq == E_IRQ_DISP) ? IRQF_SHARED | IRQF_ONESHOT : SA_INTERRUPT ;
#endif

#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	ip->mask = true;
	ip->debug_mode = 0;
	ip->pid = current->pid;
	head = &irq_proc_head[ip->irq];

	/* if head doesn't exist yet, initialize it here */
	if (!atomic_read(&head->num)) {
		INIT_LIST_HEAD(&head->list);
		head->irq = ip->irq;
		head->mask = true;
		atomic_set(&head->num, 0);
		atomic_set(&head->complete_num, 0);
		spin_lock_init (&head->mask_lock);
		mutex_init(&head->head_lock);
		if ((error = request_irq(ip->irq, irq_proc_irq_handler,
				irqflags, ip->devname, head)) < 0) {
#else
		if ((error = request_irq(ip->irq, irq_proc_irq_handler,
				irqflags, ip->devname, ip)) < 0) {
#endif
			kfree(ip);
			mutex_unlock(&irq_proc_mutex);
			return error;
		}

	if(!(irq_to_desc(ip->irq)->irq_common_data.state_use_accessors &
				IRQD_IRQ_DISABLED))
		disable_irq_nosync(ip->irq);
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	}
	if (atomic_read(&head->complete_num) == 0)
	    ip->completion = true;
	else
	    ip->completion = false;
	list_add_tail(&ip->list, &head->list);
	atomic_inc(&head->num);
#endif
	fp->private_data = (void *)ip;

	mutex_unlock(&irq_proc_mutex);

	return 0;
}

/*
 * i don't see anyone call this function...
 */
static int irq_proc_release(struct inode *inop, struct file *fp)
{
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
	/* protect fp->private_data & ip->list */
	mutex_lock(&irq_proc_mutex);

	if (fp->private_data != NULL) {
		struct irq_proc *ip = (struct irq_proc *)fp->private_data;
		int irq = ip->irq;
		struct irq_proc_head *head = &irq_proc_head[irq];

		list_del(&ip->list);
	atomic_dec(&head->num);
		kfree(ip);
		fp->private_data = NULL;

		if (!atomic_read(&head->num))
			free_irq(irq, head); /* note: head won't be freed */
	}

	mutex_unlock(&irq_proc_mutex);

	return 0;
#else
	if(fp->private_data != NULL) {
		struct irq_proc *ip = (struct irq_proc *)fp->private_data;

		free_irq(ip->irq, ip);
		kfree(ip);
		fp->private_data = NULL;
	}

	return 0;
#endif
}

/*
 * let userspace thread sleep until irq_proc_irq_handler wakes it up
 */
unsigned int irq_proc_poll(struct file *fp, struct poll_table_struct *wait)
{
    struct irq_proc *ip = (struct irq_proc *)fp->private_data;

    poll_wait(fp, &ip->q, wait);
#ifdef CONFIG_MP_PLATFORM_UTOPIA2_INTERRUPT
    if(ip->irq_push_exit == 0xBEEF) {
        printk("IRQ %d detach speedup!!\n",ip->irq);
        ip->irq_push_exit = 0;
        return POLLPRI;
    }
#endif
    if (atomic_read(&ip->count) > 0) {
        atomic_dec(&ip->count);
        return POLLIN | POLLRDNORM;
    }
    return 0;
}

/*
 * from CHIP_DetachISR, but do the same job as irq_proc_release
 */
static long irq_proc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case 137: /* special command to free_irq immediately */
		return irq_proc_release(NULL, fp);
	default:
		return -1;
	}
}

static long compat_irq_proc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case 137: /* special command to free_irq immediately */
		return irq_proc_release(NULL, fp);
	default:
		return -1;
	}
}


struct file_operations irq_proc_file_operations = {
        .read = irq_proc_read,
        .write = irq_proc_write,
        .open = irq_proc_open,
        .release = irq_proc_release,
        .poll = irq_proc_poll,
        .unlocked_ioctl= irq_proc_ioctl,  //Procfs ioctl handlers must use unlocked_ioctl.
	#if defined(CONFIG_COMPAT)
	.compat_ioctl = compat_irq_proc_ioctl,
	#endif
};

#endif/*CONFIG_MP_PLATFORM_ARCH_GENERAL*/

#define MAX_NAMELEN 128

static int name_unique(unsigned int irq, struct irqaction *new_action)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	unsigned long flags;
	int ret = 1;

	raw_spin_lock_irqsave(&desc->lock, flags);
	for_each_action_of_desc(desc, action) {
		if ((action != new_action) && action->name &&
				!strcmp(new_action->name, action->name)) {
			ret = 0;
			break;
		}
	}
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	return ret;
}

void register_handler_proc(unsigned int irq, struct irqaction *action)
{
	char name [MAX_NAMELEN];
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc->dir || action->dir || !action->name ||
					!name_unique(irq, action))
		return;

	snprintf(name, MAX_NAMELEN, "%s", action->name);

	/* create /proc/irq/1234/handler/ */
	action->dir = proc_mkdir(name, desc->dir);
}

#undef MAX_NAMELEN

#define MAX_NAMELEN 10

void register_irq_proc(unsigned int irq, struct irq_desc *desc)
{
	static DEFINE_MUTEX(register_lock);
	char name [MAX_NAMELEN];

#if (MP_PLATFORM_ARCH_GENERAL == 1)
	struct proc_dir_entry *entry;
#endif/*MP_PLATFORM_ARCH_GENERAL*/

	if (!root_irq_dir || (desc->irq_data.chip == &no_irq_chip))
		return;

	/*
	 * irq directories are registered only when a handler is
	 * added, not when the descriptor is created, so multiple
	 * tasks might try to register at the same time.
	 */
	mutex_lock(&register_lock);

	if (desc->dir)
		goto out_unlock;

	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	desc->dir = proc_mkdir(name, root_irq_dir);
	if (!desc->dir)
		goto out_unlock;

#ifdef CONFIG_SMP
	/* create /proc/irq/<irq>/smp_affinity */
	proc_create_data("smp_affinity", 0644, desc->dir,
			 &irq_affinity_proc_fops, (void *)(long)irq);

	/* create /proc/irq/<irq>/affinity_hint */
	proc_create_data("affinity_hint", 0444, desc->dir,
			 &irq_affinity_hint_proc_fops, (void *)(long)irq);

	/* create /proc/irq/<irq>/smp_affinity_list */
	proc_create_data("smp_affinity_list", 0644, desc->dir,
			 &irq_affinity_list_proc_fops, (void *)(long)irq);

	proc_create_data("node", 0444, desc->dir,
			 &irq_node_proc_fops, (void *)(long)irq);
#endif

	proc_create_data("spurious", 0444, desc->dir,
			 &irq_spurious_proc_fops, (void *)(long)irq);

#if (MP_PLATFORM_ARCH_GENERAL == 1)
#if (MP_Android_MSTAR_CHANGE_IRQ_FILE_PERMISSION == 1)
	entry = proc_create("irq", 0666, desc->dir, &irq_proc_file_operations);
#else
	entry = proc_create("irq", 0600, desc->dir, &irq_proc_file_operations);
#endif
	if (entry)
		entry->data = (void *)(long)irq;
#endif  /* MP_PLATFORM_ARCH_GENERAL */

out_unlock:
	mutex_unlock(&register_lock);
}

void unregister_irq_proc(unsigned int irq, struct irq_desc *desc)
{
	char name [MAX_NAMELEN];

	if (!root_irq_dir || !desc->dir)
		return;
#ifdef CONFIG_SMP
	remove_proc_entry("smp_affinity", desc->dir);
	remove_proc_entry("affinity_hint", desc->dir);
	remove_proc_entry("smp_affinity_list", desc->dir);
	remove_proc_entry("node", desc->dir);
#endif
	remove_proc_entry("spurious", desc->dir);

	sprintf(name, "%u", irq);
	remove_proc_entry(name, root_irq_dir);
}

#undef MAX_NAMELEN

void unregister_handler_proc(unsigned int irq, struct irqaction *action)
{
	proc_remove(action->dir);
}

static void register_default_affinity_proc(void)
{
#ifdef CONFIG_SMP
	proc_create("irq/default_smp_affinity", 0644, NULL,
		    &default_affinity_proc_fops);
#endif
}

void init_irq_proc(void)
{
	unsigned int irq;
	struct irq_desc *desc;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", NULL);
	if (!root_irq_dir)
		return;

	register_default_affinity_proc();

	/*
	 * Create entries for all existing IRQs.
	 */
	for_each_irq_desc(irq, desc)	// this is virq
		register_irq_proc(irq, desc);
}

#ifdef CONFIG_GENERIC_IRQ_SHOW

int __weak arch_show_interrupts(struct seq_file *p, int prec)
{
	return 0;
}

#ifndef ACTUAL_NR_IRQS
# define ACTUAL_NR_IRQS nr_irqs
#endif

int show_interrupts(struct seq_file *p, void *v)
{
	static int prec;

	unsigned long flags, any_count = 0;
	int i = *(loff_t *) v, j;
	struct irqaction *action;
	struct irq_desc *desc;

	if (i > ACTUAL_NR_IRQS)
		return 0;

	if (i == ACTUAL_NR_IRQS)
		return arch_show_interrupts(p, prec);

	/* print header and calculate the width of the first column */
	if (i == 0) {
		for (prec = 3, j = 1000; prec < 10 && j <= nr_irqs; ++prec)
			j *= 10;

		seq_printf(p, "%*s", prec + 8, "");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%-8d", j);
		seq_putc(p, '\n');
	}

	irq_lock_sparse();
	desc = irq_to_desc(i);
	if (!desc)
		goto outsparse;

	raw_spin_lock_irqsave(&desc->lock, flags);
	for_each_online_cpu(j)
		any_count |= kstat_irqs_cpu(i, j);
	action = desc->action;
	if ((!action || irq_desc_is_chained(desc)) && !any_count)
		goto out;

	seq_printf(p, "%*d: ", prec, i);
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", kstat_irqs_cpu(i, j));

	if (desc->irq_data.chip) {
		if (desc->irq_data.chip->irq_print_chip)
			desc->irq_data.chip->irq_print_chip(&desc->irq_data, p);
		else if (desc->irq_data.chip->name)
			seq_printf(p, " %8s", desc->irq_data.chip->name);
		else
			seq_printf(p, " %8s", "-");
	} else {
		seq_printf(p, " %8s", "None");
	}
	if (desc->irq_data.domain)
		seq_printf(p, " %*d", prec, (int) desc->irq_data.hwirq);
#ifdef CONFIG_GENERIC_IRQ_SHOW_LEVEL
	seq_printf(p, " %-8s", irqd_is_level_type(&desc->irq_data) ? "Level" : "Edge");
#endif
	if (desc->name)
		seq_printf(p, "-%-8s", desc->name);

	if (action) {
		seq_printf(p, "  %s", action->name);
		while ((action = action->next) != NULL)
			seq_printf(p, ", %s", action->name);
	}

	seq_putc(p, '\n');
out:
	raw_spin_unlock_irqrestore(&desc->lock, flags);
outsparse:
	irq_unlock_sparse();
	return 0;
}
#endif
