#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/wait.h>
#include <linux/cgroup.h>
#include <linux/oom.h>
#include <linux/rcupdate.h>
#include <linux/cgroup-defs.h>
#include <linux/pid_namespace.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/vmstat.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/delay.h>

#define NAP_TIME 1 //HZ*NAP_TIME
#define KILL_PENDING_TIME 2 // HZ*KILL_PENDING_TIME
#define MAX_EPOLL_EVENTS 10
#define PATH_MAX 50
#define MEM_FREE_FILE_SIZE 100
#define SWAP_FILE_SIZE 200
#define OOM_SCORE_ADJ_MAX 1000
#define OOM_SCORE_ADJ_MIN -1000
struct task_struct *mtk_lmkd;
static int mtk_lmkd_func(void*);
static int mtk_mem_qos_func(void*);
static void mtk_kill_all(void);
static void mtk_kill_one(void);
static int mtk_show_zram_stat(void);
static void mtk_adjust_swappiness(int threshold);
static void mtk_privilege_list_init(void);
static void mtk_drop_caches(void);
static int mtk_query_drop_caches_level(void);
static int mtk_query_mem_qos_interval(void);
static int mtk_query_free_mem_threshold(void);
static int mtk_query_swap_threshold(void);
static int mtk_query_swappiness_low(void);
static int mtk_query_swappiness_high(void);
static int mtk_query_zram_kill_timer(void);
static bool mtk_query_zram_kill(void);
struct task_struct *mtk_find_lock_task_mm(struct task_struct *p);
unsigned long total_kill_size = 0; //KB
int oom_score_adj_threshold = 600; //kill > this value
int swap_threshold = 0; //Percentage
int swappiness_low = -1;
int swappiness_high = -1;
int zram_kill_timer = 0;
int drop_caches_level = 0;
int mem_qos_interval = 0; //Second
int free_mem_threshold = 0; //KB
bool zram_pressure_high = false;
int zram_pressure_high_counter = 0;
bool zram_kill = false;
#define TRIGER_BY_VDEC 1
#define DECIMAL 10
#define PERCENTAGE 100
#define DROP_CACHES_LEVEL_1 1
#define DROP_CACHES_LEVEL_2 2
#define DROP_CACHES_LEVEL_3 3

DEFINE_MUTEX(privilege_list_lock);
DEFINE_MUTEX(drop_caches_level_lock);
DEFINE_MUTEX(mem_qos_interval_lock);
DEFINE_MUTEX(free_mem_threshold_lock);
DEFINE_MUTEX(swap_threshold_lock);
DEFINE_MUTEX(swappiness_low_lock);
DEFINE_MUTEX(swappiness_high_lock);
DEFINE_MUTEX(zram_kill_timer_lock);
DEFINE_MUTEX(zram_kill_lock);
struct privilege_node
{
	char name[TASK_COMM_LEN];
	struct list_head list;
};

struct list_head privilege_list = LIST_HEAD_INIT(privilege_list);
#ifdef TRIGER_BY_QOS
extern void mtk_wakeup_lmkd(void);
extern void mtk_hypnotize_lmkd(void);
extern wait_queue_head_t mtk_lmkd_wait;
extern bool should_wait;
#endif

wait_queue_head_t mem_qos_wait;
EXPORT_SYMBOL(mem_qos_wait);
#if TRIGER_BY_VDEC
wait_queue_head_t mtk_lmkd_wait;
EXPORT_SYMBOL(mtk_lmkd_wait);
bool should_wait = true;
EXPORT_SYMBOL(should_wait);
void mtk_wakeup_lmkd(void)
{
	if (waitqueue_active(&mtk_lmkd_wait)) {
		should_wait = false;
		printk(KERN_ALERT"[mtklmkd] wake up \n");
		wake_up_interruptible(&mtk_lmkd_wait);
	}
}
EXPORT_SYMBOL(mtk_wakeup_lmkd);
void mtk_hypnotize_lmkd(void)
{
	printk(KERN_ALERT"[mtklmkd] go sleep \n");
	should_wait = true;
}
EXPORT_SYMBOL(mtk_hypnotize_lmkd);
#endif

static ssize_t lmkd_privilege_list_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	mutex_lock(&privilege_list_lock);
	struct list_head *now;
	struct privilege_node *now_node;
	int len;
	char sbuf[4096];
	list_for_each(now, &privilege_list) {
		now_node = list_entry(now, struct privilege_node, list);
		printk(KERN_ALERT"%s\n", now_node->name);
	}
	mutex_unlock(&privilege_list_lock);
	return 0;

}

static ssize_t lmkd_privilege_list_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	struct privilege_node *new_node = kmalloc(sizeof(struct privilege_node),GFP_KERNEL);
	memset(new_node, 0, sizeof(struct privilege_node));
	if (!count || count > TASK_COMM_LEN)
		return 0;
	if (new_node) {
		mutex_lock(&privilege_list_lock);
		copy_from_user(new_node->name, buf, count);
		list_add(&new_node->list,&privilege_list);
		mutex_unlock(&privilege_list_lock);
		return count;
	}

	else
		return -EINVAL;
}


static int lmkd_privilege_list_open(struct inode *inode, struct file *file){
    return 0;
}

static int lmkd_privilege_list_release(struct inode *inode, struct file *file){
    return 0;
}


static struct file_operations const lmkd_privilege_list_fops = {
        .owner          = THIS_MODULE,
        .read           = lmkd_privilege_list_read,
        .write          = lmkd_privilege_list_write,
        .open           = lmkd_privilege_list_open,
        .release        = lmkd_privilege_list_release,
};

static ssize_t drop_caches_level_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] drop caches level = %d \n", mtk_query_drop_caches_level());
	return 0;
}

static ssize_t drop_caches_level_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char level[TASK_COMM_LEN+1] = {0};
	copy_from_user(level, buf, count);

	mutex_lock(&drop_caches_level_lock);
	kstrtoint(strstrip(level), DECIMAL, &drop_caches_level);
	if ((drop_caches_level != DROP_CACHES_LEVEL_1)
		&& (drop_caches_level != DROP_CACHES_LEVEL_2)
		&& (drop_caches_level != DROP_CACHES_LEVEL_3)) {
		drop_caches_level = 0;
	}
	printk(KERN_ALERT"[mtklmkd] set drop caches level = %d \n", drop_caches_level);

	if (drop_caches_level != 0) {
		if (waitqueue_active(&mem_qos_wait)) {
			printk(KERN_ALERT"[mtklmkd] wake up mem qos \n");
			wake_up_interruptible(&mem_qos_wait);
		}
	}
	mutex_unlock(&drop_caches_level_lock);

	return count;
}

static int drop_caches_level_open(struct inode *inode, struct file *file){
	return 0;
}

static int drop_caches_level_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const drop_caches_level_fops = {
        .owner          = THIS_MODULE,
        .read           = drop_caches_level_read,
        .write          = drop_caches_level_write,
        .open           = drop_caches_level_open,
        .release        = drop_caches_level_release,
};

static int mtk_query_drop_caches_level(void){
	int level = 0;

	mutex_lock(&drop_caches_level_lock);
	level = drop_caches_level;
	mutex_unlock(&drop_caches_level_lock);

	return level;
}

static ssize_t mem_qos_interval_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] mem qos interval = %d s \n", mtk_query_mem_qos_interval());
	return 0;
}

static ssize_t mem_qos_interval_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char interval[TASK_COMM_LEN+1] = {0};
	copy_from_user(interval, buf, count);

	mutex_lock(&mem_qos_interval_lock);
	kstrtoint(strstrip(interval), DECIMAL, &mem_qos_interval);
	if (mem_qos_interval < 0) {
		mem_qos_interval = 0;
	}
	printk(KERN_ALERT"[mtklmkd] set mem qos interval = %d s \n", mem_qos_interval);

	if (mem_qos_interval != 0) {
		if (waitqueue_active(&mem_qos_wait)) {
			printk(KERN_ALERT"[mtklmkd] wake up mem qos \n");
			wake_up_interruptible(&mem_qos_wait);
		}
	}
	mutex_unlock(&mem_qos_interval_lock);


	return count;
}

static int mem_qos_interval_open(struct inode *inode, struct file *file){
	return 0;
}

static int mem_qos_interval_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const mem_qos_interval_fops = {
        .owner          = THIS_MODULE,
        .read           = mem_qos_interval_read,
        .write          = mem_qos_interval_write,
        .open           = mem_qos_interval_open,
        .release        = mem_qos_interval_release,
};

static int mtk_query_mem_qos_interval(void){
	int interval = 0;

	mutex_lock(&mem_qos_interval_lock);
	interval = mem_qos_interval;
	mutex_unlock(&mem_qos_interval_lock);

	return interval;
}

static ssize_t free_mem_threshold_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] free mem threshold = %d KB \n", mtk_query_free_mem_threshold());
	return 0;
}

static ssize_t free_mem_threshold_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char threshold[TASK_COMM_LEN+1] = {0};
	copy_from_user(threshold, buf, count);

	mutex_lock(&free_mem_threshold_lock);
	kstrtoint(strstrip(threshold), DECIMAL, &free_mem_threshold);
	if (free_mem_threshold < 0) {
		free_mem_threshold = 0;
	}
	printk(KERN_ALERT"[mtklmkd] set free mem threshold = %d KB \n", free_mem_threshold);
	mutex_unlock(&free_mem_threshold_lock);

	return count;
}

static int free_mem_threshold_open(struct inode *inode, struct file *file){
	return 0;
}

static int free_mem_threshold_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const free_mem_threshold_fops = {
        .owner          = THIS_MODULE,
        .read           = free_mem_threshold_read,
        .write          = free_mem_threshold_write,
        .open           = free_mem_threshold_open,
        .release        = free_mem_threshold_release,
};

static int mtk_query_free_mem_threshold(void){
	int threshold = 0;

	mutex_lock(&free_mem_threshold_lock);
	threshold = free_mem_threshold;
	mutex_unlock(&free_mem_threshold_lock);

	return threshold;
}

static ssize_t swap_threshold_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] swap threshold = %d % \n", mtk_query_swap_threshold());
	return 0;
}

static ssize_t swap_threshold_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char threshold[TASK_COMM_LEN+1] = {0};
	copy_from_user(threshold, buf, count);

	mutex_lock(&swap_threshold_lock);
	kstrtoint(strstrip(threshold), DECIMAL, &swap_threshold);
	if (swap_threshold < 0) {
		swap_threshold = 0;
	}
	if (swap_threshold > PERCENTAGE) {
		swap_threshold = PERCENTAGE;
	}
	printk(KERN_ALERT"[mtklmkd] set swap threshold = %d percentage \n", swap_threshold);

	if (swap_threshold != 0) {
		if (waitqueue_active(&mem_qos_wait)) {
			printk(KERN_ALERT"[mtklmkd] wake up mem qos \n");
			wake_up_interruptible(&mem_qos_wait);
		}
	}
	mutex_unlock(&swap_threshold_lock);

	return count;
}

static int swap_threshold_open(struct inode *inode, struct file *file){
	return 0;
}

static int swap_threshold_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const swap_threshold_fops = {
        .owner          = THIS_MODULE,
        .read           = swap_threshold_read,
        .write          = swap_threshold_write,
        .open           = swap_threshold_open,
        .release        = swap_threshold_release,
};

static int mtk_query_swap_threshold(void){
	int threshold = 0;

	mutex_lock(&swap_threshold_lock);
	threshold = swap_threshold;
	mutex_unlock(&swap_threshold_lock);

	return threshold;
}

static ssize_t swappiness_low_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] swappiness low = %d \n", mtk_query_swappiness_low());
	return 0;
}

static ssize_t swappiness_low_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char swappiness[TASK_COMM_LEN+1] = {0};
	copy_from_user(swappiness, buf, count);

	mutex_lock(&swappiness_low_lock);
	kstrtoint(strstrip(swappiness), DECIMAL, &swappiness_low);
	if ((swappiness_low < 0) || (swappiness_low > PERCENTAGE)) {
		swappiness_low = -1;
	}
	printk(KERN_ALERT"[mtklmkd] set swappiness low = %d \n", swappiness_low);

	if (swappiness_low != -1) {
		if (waitqueue_active(&mem_qos_wait)) {
			printk(KERN_ALERT"[mtklmkd] wake up mem qos \n");
			wake_up_interruptible(&mem_qos_wait);
		}
	}
	mutex_unlock(&swappiness_low_lock);

	return count;
}

static int swappiness_low_open(struct inode *inode, struct file *file){
	return 0;
}

static int swappiness_low_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const swappiness_low_fops = {
        .owner          = THIS_MODULE,
        .read           = swappiness_low_read,
        .write          = swappiness_low_write,
        .open           = swappiness_low_open,
        .release        = swappiness_low_release,
};

static int mtk_query_swappiness_low(void){
	int swappiness = 0;

	mutex_lock(&swappiness_low_lock);
	swappiness = swappiness_low;
	mutex_unlock(&swappiness_low_lock);

	return swappiness;
}

static ssize_t swappiness_high_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] swappiness high = %d \n", mtk_query_swappiness_high());
	return 0;
}

static ssize_t swappiness_high_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char swappiness[TASK_COMM_LEN+1] = {0};
	copy_from_user(swappiness, buf, count);

	mutex_lock(&swappiness_high_lock);
	kstrtoint(strstrip(swappiness), DECIMAL, &swappiness_high);
	if ((swappiness_high < 0) || (swappiness_high > PERCENTAGE)) {
		swappiness_high = -1;
	}
	printk(KERN_ALERT"[mtklmkd] set swappiness high = %d \n", swappiness_high);

	if (swappiness_high != -1) {
		if (waitqueue_active(&mem_qos_wait)) {
			printk(KERN_ALERT"[mtklmkd] wake up mem qos \n");
			wake_up_interruptible(&mem_qos_wait);
		}
	}
	mutex_unlock(&swappiness_high_lock);

	return count;
}

static int swappiness_high_open(struct inode *inode, struct file *file){
	return 0;
}

static int swappiness_high_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const swappiness_high_fops = {
        .owner          = THIS_MODULE,
        .read           = swappiness_high_read,
        .write          = swappiness_high_write,
        .open           = swappiness_high_open,
        .release        = swappiness_high_release,
};

static int mtk_query_swappiness_high(void){
	int swappiness = 0;

	mutex_lock(&swappiness_high_lock);
	swappiness = swappiness_high;
	mutex_unlock(&swappiness_high_lock);

	return swappiness;
}

static ssize_t zram_kill_timer_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"[mtklmkd] zram kill timer = %d \n", mtk_query_zram_kill_timer());
	return 0;
}

static ssize_t zram_kill_timer_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	if (count == 0 || count > TASK_COMM_LEN)
		return 0;

	char timer[TASK_COMM_LEN+1] = {0};
	copy_from_user(timer, buf, count);

	mutex_lock(&zram_kill_timer_lock);
	kstrtoint(strstrip(timer), DECIMAL, &zram_kill_timer);
	if (zram_kill_timer < 0) {
		zram_kill_timer = 0;
	}
	printk(KERN_ALERT"[mtklmkd] set zram kill timer = %d \n", zram_kill_timer);
	mutex_unlock(&zram_kill_timer_lock);

	return count;
}

static int zram_kill_timer_open(struct inode *inode, struct file *file){
	return 0;
}

static int zram_kill_timer_release(struct inode *inode, struct file *file){
	return 0;
}

static struct file_operations const zram_kill_timer_fops = {
        .owner          = THIS_MODULE,
        .read           = zram_kill_timer_read,
        .write          = zram_kill_timer_write,
        .open           = zram_kill_timer_open,
        .release        = zram_kill_timer_release,
};

static int mtk_query_zram_kill_timer(void){
	int timer = 0;

	mutex_lock(&zram_kill_timer_lock);
	timer = zram_kill_timer;
	mutex_unlock(&zram_kill_timer_lock);

	return timer;
}

static bool mtk_query_zram_kill(void){
	bool status = false;

	mutex_lock(&zram_kill_lock);
	status = zram_kill;
	mutex_unlock(&zram_kill_lock);

	return status;
}

static void mtk_set_zram_kill(bool status){
	mutex_lock(&zram_kill_lock);
	zram_kill = status;
	mutex_unlock(&zram_kill_lock);
}

static ssize_t lmkd_killed_size_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	return 0;
}
static ssize_t lmkd_killed_size_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"total killed szie %d Kb \n", total_kill_size);
    return 0;
}
static int lmkd_killed_size_open(struct inode *inode, struct file *file){
    return 0;
}
static int lmkd_killed_size_release(struct inode *inode, struct file *file){
    return 0;
}
static struct file_operations const lmkd_killed_size_fops = {
        .owner          = THIS_MODULE,
        .read           = lmkd_killed_size_read,
        .write          = lmkd_killed_size_write,
        .open           = lmkd_killed_size_open,
        .release        = lmkd_killed_size_release,
};
static ssize_t oom_score_adj_threshold_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	char buffer[8];
	char *temp;
	long oom_score_adj;
    if (copy_from_user(buffer, buf, count)) {
		return -EINVAL;
    }
    oom_score_adj = simple_strtol(buffer, &temp, 10);
	if (oom_score_adj > OOM_SCORE_ADJ_MAX || oom_score_adj < OOM_SCORE_ADJ_MIN)
		return -EINVAL;
	oom_score_adj_threshold = oom_score_adj;
	return count;
}
static ssize_t oom_score_adj_threshold_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT"oom score adj threshold %d \n", oom_score_adj_threshold);
    return 0;
}
static int oom_score_adj_threshold_open(struct inode *inode, struct file *file){
    return 0;
}
static int oom_score_adj_threshold_release(struct inode *inode, struct file *file){
    return 0;
}
static unsigned int oom_score_adj_threshold_poll(struct file *file, struct poll_table_struct *pts)
{
	printk(KERN_ALERT"start poll \n");
	printk(KERN_ALERT"after poll \n");
	int	poll_flags = POLLIN | POLLRDNORM;
	return poll_flags;
}
static struct file_operations const oom_score_adj_threshold_fops = {
        .owner          = THIS_MODULE,
        .read           = oom_score_adj_threshold_read,
        .write          = oom_score_adj_threshold_write,
        .open           = oom_score_adj_threshold_open,
        .release        = oom_score_adj_threshold_release,
        .poll        	= oom_score_adj_threshold_poll,
};
static ssize_t mtk_wakeup_lmkd_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	return 0;
}
static ssize_t mtk_wakeup_lmkd_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT" should wait %d \n", should_wait);
    return 0;
}
static int mtk_wakeup_lmkd_open(struct inode *inode, struct file *file){
	mtk_wakeup_lmkd();
    return 0;
}
static int mtk_wakeup_lmkd_read_release(struct inode *inode, struct file *file){
    return 0;
}
static struct file_operations const mtk_wakeup_lmkd_fops = {
        .owner          = THIS_MODULE,
        .read           = mtk_wakeup_lmkd_read,
        .write          = mtk_wakeup_lmkd_write,
        .open           = mtk_wakeup_lmkd_open,
        .release        = mtk_wakeup_lmkd_read_release,
};
static ssize_t mtk_hypnotize_lmkd_write(struct file *file, const char __user *buf, size_t count,
                             loff_t *ppos)
{
	return 0;
}
static ssize_t mtk_hypnotize_lmkd_read(struct file *file, char __user *buf, size_t count,
                             loff_t *ppos)
{
	printk(KERN_ALERT" should wait %d \n", should_wait);
    return 0;
}
static int mtk_hypnotize_lmkd_open(struct inode *inode, struct file *file){
	mtk_hypnotize_lmkd();
    return 0;
}
static int mtk_hypnotize_lmkd_release(struct inode *inode, struct file *file){
    return 0;
}
static struct file_operations const mtk_hypnotize_lmkd_fops = {
        .owner          = THIS_MODULE,
        .read           = mtk_hypnotize_lmkd_read,
        .write          = mtk_hypnotize_lmkd_write,
        .open           = mtk_hypnotize_lmkd_open,
        .release        = mtk_hypnotize_lmkd_release,
};

static int __init mtk_lmkd_init(void)
{
	mtk_privilege_list_init();

	init_waitqueue_head(&mem_qos_wait);
#ifdef TRIGER_BY_VDEC
	init_waitqueue_head(&mtk_lmkd_wait);
#endif
    static struct proc_dir_entry *mtk_lmkd_dir;
    mtk_lmkd_dir = proc_mkdir("mtk_lmkd", NULL);
    if (!proc_create("total_killed_size", 0644, mtk_lmkd_dir, &lmkd_killed_size_fops)) {
    	printk(KERN_ALERT"mtklmkd fail !!\n");
      	return -1;
    }
    if (!proc_create("oom_score_adj_threshold", 0644, mtk_lmkd_dir, &oom_score_adj_threshold_fops)) {
    	printk(KERN_ALERT"mtklmkd fail !!\n");
      	return -1;
    }
    if (!proc_create("mtk_hypnotize_lmkd", 0644, mtk_lmkd_dir, &mtk_hypnotize_lmkd_fops)) {
    	printk(KERN_ALERT"mtklmkd fail !!\n");
      	return -1;
    }
    if (!proc_create("mtk_wakeup_lmkd", 0644, mtk_lmkd_dir, &mtk_wakeup_lmkd_fops)) {
    	printk(KERN_ALERT"mtklmkd fail !!\n");
      	return -1;
    }
    if (!proc_create("privilege_list", 0644, mtk_lmkd_dir, &lmkd_privilege_list_fops)) {
    	printk(KERN_ALERT"mtklmkd fail !!\n");
      	return -1;
    }
	if (!proc_create("drop_caches_level", 0644, mtk_lmkd_dir, &drop_caches_level_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}
	if (!proc_create("mem_qos_interval", 0644, mtk_lmkd_dir, &mem_qos_interval_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}
	if (!proc_create("free_mem_threshold", 0644, mtk_lmkd_dir, &free_mem_threshold_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}
	if (!proc_create("swap_threshold", 0644, mtk_lmkd_dir, &swap_threshold_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}
	if (!proc_create("swappiness_low", 0644, mtk_lmkd_dir, &swappiness_low_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}
	if (!proc_create("swappiness_high", 0644, mtk_lmkd_dir, &swappiness_high_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}
	if (!proc_create("zram_kill_timer", 0644, mtk_lmkd_dir, &zram_kill_timer_fops)) {
		printk(KERN_ALERT"mtklmkd fail !!\n");
		return -1;
	}

    mtk_lmkd = kthread_run(mtk_lmkd_func, NULL, "mtk_lmkd");
    mtk_lmkd = kthread_run(mtk_mem_qos_func, NULL, "mtk_mem_qos");
	if (IS_ERR(mtk_lmkd)) {
		return -1;
	}
	return 0;
}


#ifdef CONFIG_CPUSETS
struct fmeter {
	int cnt;		/* unprocessed events count */
	int val;		/* most recent output value */
	time64_t time;		/* clock (secs) when val computed */
	spinlock_t lock;	/* guards read or write of above */
};
struct cpuset {
	struct cgroup_subsys_state css;
	unsigned long flags;		/* "unsigned long" so bitops work */
	cpumask_var_t cpus_allowed;
	cpumask_var_t cpus_requested;
	nodemask_t mems_allowed;
	cpumask_var_t effective_cpus;
	nodemask_t effective_mems;
	nodemask_t old_mems_allowed;
	struct fmeter fmeter;		/* memory_pressure filter */
	int attach_in_progress;
	int pn;
	int relax_domain_level;
#ifdef CONFIG_MP_ASYM_UMA_ALLOCATION
	/* for memory region idx */
	int memalloc_idx;
#endif
};

static struct cgroup_subsys_state *mtk_task_cs(struct task_struct *tsk)
{
	struct cgroup_subsys_state *ptr = task_css(tsk, cpuset_cgrp_id);
	if (ptr)
		return ptr;
	else
		return NULL;
}

#endif

static int mtk_show_zram_stat(void) //return usage
{
	unsigned char buf[SWAP_FILE_SIZE] = {0};
	unsigned char *temp = NULL;
	char path[PATH_MAX+1] = {0};
	snprintf(path, PATH_MAX, "/proc/swaps");
	struct file *f = filp_open(path, O_RDONLY, 0);
	if(IS_ERR_OR_NULL(f)) {
		printk(KERN_ALERT"[mtklmkd] open %s failed \n", path);
		return -1;
	}
	mm_segment_t fs;
	fs = get_fs();
	set_fs(get_ds());
	if (f->f_op->read)
		f->f_op->read(f, buf ,SWAP_FILE_SIZE ,&f->f_pos);
	set_fs(fs);
	filp_close(f, NULL);
	int total_size = 0;
	int used = 0;
	unsigned char *pos = strstr(buf, "on");
	if (pos) {
		int i = 0, k = 0;
		int i_start = 0, i_end = 0;
		int k_start = 0, k_end = 0;
		for (i = 0; i < (SWAP_FILE_SIZE - (pos - buf)); i++) {
			if (isdigit(pos[i]) && !i_start)
				i_start = i;
			if (i_start && !isdigit(pos[i])) {
				i_end = i - 1;
				break;
			}
		}
		if (!i_start)
			return -1;
		temp = kvmalloc(((i_end - i_start + 1) + 1), GFP_KERNEL);
		if (temp == NULL) {
			return -1;
		}
		strncpy(temp, pos + i_start, (i_end - i_start + 1));
		temp[i_end - i_start + 1] = '\0';
		kstrtoint((temp), DECIMAL, &total_size);
		kvfree(temp);
		for (k = 0; k < SWAP_FILE_SIZE - (pos + i - buf); k ++) {
			if (isdigit(pos[i + k]) && !k_start)
				k_start = k;
			if (k_start && !isdigit(pos[i + k])) {
				k_end = k - 1;
				break;
			}
		}
		if (!k_start)
			return -1;
		temp = kvmalloc(((k_end - k_start + 1) + 1), GFP_KERNEL);
		if (temp == NULL) {
			return -1;
		}
		strncpy(temp, pos + i + k_start, (k_end - k_start + 1));
		temp[k_end - k_start + 1] = '\0';
		kstrtoint((temp), DECIMAL, &used);
		kvfree(temp);
		return ((used * PERCENTAGE) / total_size);
	} else
		return -1;
}

static int mtk_show_free_mem(void)
{
	unsigned char buf[MEM_FREE_FILE_SIZE+1] = {0};
	unsigned char *temp = NULL;
	char path[PATH_MAX+1] = {0};
	snprintf(path, PATH_MAX, "/proc/meminfo");
	mm_segment_t fs;

	struct file *f = filp_open(path, O_RDONLY, 0);
	if(IS_ERR_OR_NULL(f)) {
		printk(KERN_ALERT"[mtklmkd] open %s failed \n", path);
		return -1;
	}

	fs = get_fs();
	set_fs(get_ds());
	if (f->f_op->read) {
		f->f_op->read(f, buf, MEM_FREE_FILE_SIZE, &f->f_pos);
	}
	set_fs(fs);
	filp_close(f,NULL);

	int free_mem = 0;
	unsigned char *pos = strstr(buf, "MemFree");
	if (pos != NULL) {
		int i = 0, i_start = 0, i_end = 0;
		for (i = 0; i < (MEM_FREE_FILE_SIZE - (pos - buf)); i++) {
			if ((isdigit(pos[i]) == true) && (i_start == 0)) {
				i_start = i;
			}
			if ((i_start != 0) && (isdigit(pos[i]) == false)) {
				i_end = i - 1;
				break;
			}
		}
		if (i_start == 0) {
			return -1;
		}
		temp = kvmalloc(((i_end - i_start + 1) + 1), GFP_KERNEL);
		if (temp == NULL) {
			return -1;
		}
		strncpy(temp, pos + i_start, (i_end - i_start + 1));
		temp[i_end - i_start + 1] = '\0';
		kstrtoint((temp), DECIMAL, &free_mem);
		kvfree(temp);
		return free_mem;
	} else {
		return -1;
	}
}

static bool netflix_running(void)
{
	struct task_struct *tsk;
	bool found = false;
	rcu_read_lock();
  for_each_process(tsk) {
    struct task_struct *p;
    p = mtk_find_lock_task_mm(tsk);
    if (!p)
    	continue;
    if ((strstr(p->comm, "netflix.ninja") || strstr(p->comm, "youtube.tv")) && (p->signal->oom_score_adj <= 0)) {
    	found = true;
     	task_unlock(p);
      break;
    }
		task_unlock(p);
	}
	rcu_read_unlock();
	return found;
}

static int mtk_lmkd_func(void *data)
{
	long remaining = 0;
	DEFINE_WAIT(wait);
	while (1)
	{
	   if (kthread_should_stop())
		   break;
		if (should_wait) {
			prepare_to_wait(&mtk_lmkd_wait, &wait, TASK_INTERRUPTIBLE);
			remaining = schedule_timeout(HZ*NAP_TIME);
			finish_wait(&mtk_lmkd_wait, &wait);
		} else
			remaining = 1;
		if (remaining) {
				if (netflix_running()) {
					printk(KERN_ALERT"[mtk_lmkd] netflix running \n");
					goto start_kill;
				}
				else {
					printk(KERN_ALERT"[mtk_lmkd] netflix not running \n");
					goto sleep;
				}
		}

		if (mtk_query_zram_kill() == true && (netflix_running())) {
			goto start_kill;
		} else {
			goto sleep;
		}
start_kill:
		mtk_kill_all();
		prepare_to_wait(&mtk_lmkd_wait, &wait, TASK_INTERRUPTIBLE);
		schedule_timeout(HZ * KILL_PENDING_TIME);
		finish_wait(&mtk_lmkd_wait, &wait);
sleep:
		should_wait = true;
		remaining = 0;
	}
	return 0;
}

static int mtk_mem_qos_func(void *data)
{
	DEFINE_WAIT(wait);
	int level = 0, interval = 0, threshold_mem = 0, free_mem = 0;
	int threshold_swap = 0, swap_low = -1, swap_high = -1;
	bool drop_caches_start = false, swappiness_start = false;

	while (1)
	{
		if (kthread_should_stop())
			break;

		level = mtk_query_drop_caches_level();
		interval = mtk_query_mem_qos_interval();
		threshold_mem = mtk_query_free_mem_threshold();
		threshold_swap = mtk_query_swap_threshold();
		swap_low = mtk_query_swappiness_low();
		swap_high = mtk_query_swappiness_high();

		if (level != 0)
			drop_caches_start = true;

		if ((threshold_swap != 0) && (swap_low != -1) && (swap_high != -1))
			swappiness_start = true;

		if ((interval == 0)
			|| ((drop_caches_start == false) && (swappiness_start == false))) {
			prepare_to_wait(&mem_qos_wait, &wait, TASK_INTERRUPTIBLE);
			schedule();
			finish_wait(&mem_qos_wait, &wait);
		}

		if (drop_caches_start == true) {
			if (threshold_mem != 0) {
				free_mem = mtk_show_free_mem();
			}

			if ((threshold_mem == 0)
				|| ((free_mem != -1) && (free_mem < threshold_mem))) {
				mtk_drop_caches();
			}
		}

		if (swappiness_start == true) {
			mtk_adjust_swappiness(threshold_swap);
		}

		msleep(interval*1000);
	}

	return 0;
}

struct task_struct *mtk_find_lock_task_mm(struct task_struct *p)
{
	struct task_struct *t;
	rcu_read_lock();
	for_each_thread(p, t) {
		task_lock(t);
		if (likely(t->mm))
			goto found;
		task_unlock(t);
	}
	t = NULL;
found:
	rcu_read_unlock();
	return t;
}

static bool mtk_should_kill(struct task_struct *p, int last_kill_pid)
{
#ifdef CONFIG_CPUSETS
	struct cgroup_subsys_state *cpuset_ptr;
	cpuset_ptr = mtk_task_cs(p);
	if (strstr(cpuset_ptr->cgroup->kn->name, "app")) {
		return false;
	}
#endif
	struct list_head *now;
	struct privilege_node *now_node;
	mutex_lock(&privilege_list_lock);
	list_for_each(now, &privilege_list) {
		now_node = list_entry(now, struct privilege_node, list);
		if (strstr(now_node->name, p->comm)) {
			mutex_unlock(&privilege_list_lock);
			return false;
        }
	}
	mutex_unlock(&privilege_list_lock);
	int nr_free = global_zone_page_state(NR_FREE_PAGES);
	if (p->signal->oom_score_adj <= oom_score_adj_threshold) {
		return false;
	}
	if (p->pid == last_kill_pid) {
		return false;
	}
	return true;
}
static void mtk_kill_one(void)
{
 	struct task_struct *tsk;
	static int last_kill_pid = -1;
	static int last_kill_size = 0;
	int tasksize = 0;
	static int retry_count = 0;
	int find = 0;
	struct task_struct *victim;
	if (last_kill_pid > 0 && retry_count < 10) {
		rcu_read_lock();
		victim = pid_task(find_pid_ns(last_kill_pid, task_active_pid_ns(current)), PIDTYPE_PID);
		if (!victim || (victim->exit_state && thread_group_empty(victim))) {
			printk(KERN_ALERT"[mtk_lmkd] pid %d already been killed! \n", last_kill_pid);
			total_kill_size += (last_kill_size * (long)(PAGE_SIZE / 1024));
			last_kill_pid = -1;
			last_kill_size = 0;
		} else {
			retry_count ++;
			goto end;
		}
		rcu_read_unlock();
	}
	rcu_read_lock();
    for_each_process(tsk) {
    	 if (should_wait && !mtk_query_zram_kill())
    	 	 break;
    	 struct task_struct *p;
    	 p = mtk_find_lock_task_mm(tsk);
         if (!p)
              continue;
         if (!mtk_should_kill(p, last_kill_pid)) {
			task_unlock(p);
			continue;
		 }
		 tasksize = get_mm_rss(p->mm);
		 if (tasksize > last_kill_size) {
		 	 last_kill_size = tasksize;
		 	 last_kill_pid = p->pid;
		 	 find = 1;
		 }
		 task_unlock(p);
	}
	if (find) {
		victim = pid_task(find_pid_ns(last_kill_pid, task_active_pid_ns(current)), PIDTYPE_PID);
		if (victim) {
			task_lock(victim);
			retry_count = 0;
			send_sig(SIGKILL, victim, 0);
			printk(KERN_ALERT"[mtk_lmkd] start to kill pid %d ! %s  size %d kB \n",
										last_kill_pid, victim->comm, (last_kill_size * (long)(PAGE_SIZE / 1024)));
			task_unlock(victim);
		}
	}
	if (mtk_query_zram_kill())
		mtk_set_zram_kill(false);
end:
	rcu_read_unlock();
}

static void mtk_kill_all(void)
{
 	struct task_struct *tsk;
	static int last_kill_pid = -1;
	int tasksize = 0;

	rcu_read_lock();
    for_each_process(tsk) {
		if (should_wait && !mtk_query_zram_kill())
		break;
		struct task_struct *p;
		p = mtk_find_lock_task_mm(tsk);
		if (!p)
			continue;
		if (!mtk_should_kill(p, last_kill_pid)) {
			task_unlock(p);
			continue;
		}
		tasksize = get_mm_rss(p->mm);
		last_kill_pid = p->pid;
		send_sig(SIGKILL, p, 0);
		printk(KERN_ALERT"[mtk_lmkd] start to kill pid %d ! %s  size %d kB \n",
										last_kill_pid, p->comm, (tasksize * (long)(PAGE_SIZE / 1024)));
		task_unlock(p);
		total_kill_size += (tasksize * (long)(PAGE_SIZE / 1024));
	}
	if (mtk_query_zram_kill())
		mtk_set_zram_kill(false);

end:
	rcu_read_unlock();
}


static void mtk_adjust_swappiness(int threshold)
{
	unsigned char input[TASK_COMM_LEN+1] = {0};
	unsigned char path[PATH_MAX+1] = {0};
	struct file *f = NULL;
	mm_segment_t fs;
	loff_t pos;
	snprintf(path, PATH_MAX, "/proc/sys/vm/swappiness");
	f = filp_open(path, O_RDWR, 0);
	if (IS_ERR(f)) {
		printk(KERN_ALERT"[mtklmkd] open %s failed \n", path);
		return;
	}
	fs = get_fs();
	set_fs(get_ds());
	if (mtk_show_zram_stat() > threshold) {/*if condition here */
		sprintf(input, "%d", mtk_query_swappiness_low());
		if ((zram_pressure_high == false)
			|| ((mtk_query_zram_kill_timer() != 0) && (zram_pressure_high_counter > mtk_query_zram_kill_timer()))) {
			mtk_set_zram_kill(true);
			zram_pressure_high_counter = 0;
		}
		zram_pressure_high = true;
		if (mtk_query_zram_kill_timer() != 0)
			zram_pressure_high_counter++;
	} else {
		sprintf(input, "%d", mtk_query_swappiness_high());
		zram_pressure_high = false;
	}
	pos = 0;
	vfs_write(f, input, strlen(input), &pos);
	set_fs(fs);
	filp_close(f,NULL);
}
static void mtk_privilege_list_init(void)
{
	struct privilege_node *new_node;

	new_node = kmalloc(sizeof(struct privilege_node),GFP_KERNEL);
	memset(new_node, 0, sizeof(struct privilege_node));
	strcpy(new_node->name, ".katniss:search");
	list_add(&new_node->list,&privilege_list);

	new_node = kmalloc(sizeof(struct privilege_node),GFP_KERNEL);
	memset(new_node, 0, sizeof(struct privilege_node));
	strcpy(new_node->name, "niss:interactor");
	list_add(&new_node->list,&privilege_list);

	new_node = kmalloc(sizeof(struct privilege_node),GFP_KERNEL);
	memset(new_node, 0, sizeof(struct privilege_node));
	strcpy(new_node->name, ".remote.service");
	list_add(&new_node->list,&privilege_list);

	new_node = kmalloc(sizeof(struct privilege_node),GFP_KERNEL);
	memset(new_node, 0, sizeof(struct privilege_node));
	strcpy(new_node->name, "apps.mediashell");
	list_add(&new_node->list,&privilege_list);

}

static void mtk_drop_caches(void)
{
	unsigned char input[TASK_COMM_LEN+1] = {0};
	unsigned char path[PATH_MAX+1] = {0};
	struct file *f = NULL;
	mm_segment_t fs;
	loff_t pos = 0;

	int level = mtk_query_drop_caches_level();
	if (level == DROP_CACHES_LEVEL_1) {
		input[0] = 'DROP_CACHES_LEVEL_1';
	} else if (level == DROP_CACHES_LEVEL_2) {
		input[0] = 'DROP_CACHES_LEVEL_2';
	} else if (level == DROP_CACHES_LEVEL_3) {
		input[0] = 'DROP_CACHES_LEVEL_3';
	} else {
		return;
	}

	snprintf(path, PATH_MAX, "/proc/sys/vm/drop_caches");
	f = filp_open(path, O_WRONLY, 0);

	if (IS_ERR(f)) {
		printk(KERN_ALERT"[mtklmkd] open %s failed \n", path);
		return;
	}

	fs = get_fs();
	set_fs(get_ds());
	vfs_write(f, input, strlen(input), &pos);
	set_fs(fs);
	filp_close(f,NULL);
}

static void __exit mtk_lmkd_exit(void)
{
	send_sig(SIGKILL, mtk_lmkd, 0);
}
module_init(mtk_lmkd_init);
module_exit(mtk_lmkd_exit);
MODULE_LICENSE("GPL");
