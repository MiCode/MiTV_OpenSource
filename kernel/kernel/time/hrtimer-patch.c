#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/tick.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/debugobjects.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/timer.h>
#include <linux/freezer.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/uaccess.h>

#include <trace/events/timer.h>
#include <linux/slab.h>

#include <linux/proc_fs.h>
static struct proc_dir_entry *ns_boundary_dir;
static long tv_nsec_bound = 4000000;
static long tv_nsec_bound_toggle = 0;

// At most 32 entries
#define PNAME_MAX_LEN 512
static char exc_pname1[PNAME_MAX_LEN] = {0};
static char exc_pname2[PNAME_MAX_LEN] = {0};
static long tv_exc_load_bound1 = 4000000;
static long tv_exc_load_bound2 = 4000000;

static long tv_low_load_bound1 = 4000000;
static long tv_low_load_bound2 = 4000000;


int mstar_hrt_mode = 0;

#define HRT_WHITE_MODE 0
#define HRT_BLACK_MODE 1

#define SEPARATE_SYMBOL ":"

struct black_task_info
{
	char *black_task_list;
	int black_task_list_len;
	struct mutex  black_task_lock;
};

struct black_task_info black_task_list1;
struct black_task_info black_task_list2 ;

bool is_enable_hrtimer_patch(void)
{
	if(tv_nsec_bound_toggle >= 1)
		return true;
	else
		return false;
}

static bool foundSpecialTask(char* searchSource, char* pattern,int len)
{
	int result = false;
	char * pch = NULL;
	char tmpbuf[PNAME_MAX_LEN] = {0};
	char* base = tmpbuf;

	if(len > PNAME_MAX_LEN)
		len = PNAME_MAX_LEN;

	strncpy(tmpbuf, searchSource, len);

	while ((pch = strsep(&base, SEPARATE_SYMBOL)) != NULL) {
		if (strcmp(pch, pattern) == 0) {
			result = true;
			break;
		}
	}
	return result;
}

static long run_hrtwhite_mode(struct timespec64 *tu)
{
	struct task_struct *task = current;

	if(exc_pname1[0] != 0) {
		if (true == foundSpecialTask(exc_pname1, task->comm,PNAME_MAX_LEN)) {
			if ( tu->tv_sec == 0 && (tu->tv_nsec <= tv_exc_load_bound1)) {
				tu->tv_nsec = tv_exc_load_bound1;
				return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
			}
		}
	}


	if(exc_pname2[0] != 0) {
		if (true == foundSpecialTask(exc_pname2, task->comm,PNAME_MAX_LEN)) {
			if ( tu->tv_sec == 0 && (tu->tv_nsec <= tv_exc_load_bound2)) {
				tu->tv_nsec = tv_exc_load_bound2;
				return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
			}
		}
	}

	if (unlikely(tv_nsec_bound_toggle)) {
		if ( tu->tv_sec == 0 && (tu->tv_nsec <= tv_nsec_bound))
		tu->tv_nsec = tv_nsec_bound;
	}

	return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
}


long run_hrtblack_mode(struct timespec64 *tu)
{
	struct task_struct *task = current;

	mutex_lock(&black_task_list1.black_task_lock);


	if(black_task_list1.black_task_list != NULL) {
		if (true == foundSpecialTask(black_task_list1.black_task_list, task->comm,black_task_list1.black_task_list_len)) {
			if ( tu->tv_sec == 0 && (tu->tv_nsec <= tv_low_load_bound1)) {
				tu->tv_nsec = tv_low_load_bound1;
				mutex_unlock(&black_task_list1.black_task_lock);
				return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
			}
		}
	}

	mutex_unlock(&black_task_list1.black_task_lock);


	mutex_lock(&black_task_list2.black_task_lock);

	if(black_task_list2.black_task_list != NULL) {
		if (true == foundSpecialTask(black_task_list2.black_task_list, task->comm,black_task_list2.black_task_list_len)) {
			if ( tu->tv_sec == 0 && (tu->tv_nsec <= tv_low_load_bound2)) {
				tu->tv_nsec = tv_low_load_bound2;
				mutex_unlock(&black_task_list2.black_task_lock);
				return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
			}
		}
	}

	mutex_unlock(&black_task_list2.black_task_lock);

	return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
}


extern long (*hrtimer_patch_function)(struct timespec64 *tu);


long run_hrt_timer_patch(struct timespec64 *tu)
{
	if(!is_enable_hrtimer_patch())
		return hrtimer_nanosleep(tu, HRTIMER_MODE_REL, CLOCK_MONOTONIC);

	if(mstar_hrt_mode == HRT_BLACK_MODE)
		return run_hrtblack_mode(tu);
	else if(mstar_hrt_mode == HRT_WHITE_MODE)
		return run_hrtwhite_mode(tu);
	else
		return run_hrtwhite_mode(tu);
}

// sw patch by kilroy
static ssize_t ns_bound_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[64];
	long idx = 0;

	if (!count)
		return count;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';

	if (strict_strtol(buffer, 0, &idx) != 0)
		return -EINVAL;

	tv_nsec_bound = idx;
	printk("!!!! bound value = %ld\n", idx);

	return count;
}

static int ns_bound_release(struct inode *inode, struct file * file)
{
	return 0;
}

static ssize_t ns_toggle_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char buffer[64];
    char *tmp_ptr = NULL;
    char * pch = NULL;
    long idx = 0;

    if (!count || count > 63)
        return count;

    if (copy_from_user(buffer, buf, count))
        return -EFAULT;

     buffer[count] = '\0';

     tmp_ptr = buffer;

     pch = strsep(&tmp_ptr, SEPARATE_SYMBOL);

     if(pch != NULL)
     {

         if (strict_strtol(pch, 0, &idx) != 0)
             return -EINVAL;
         if (idx == 0)
              tv_nsec_bound_toggle=0;
         else
             tv_nsec_bound_toggle=1;


         printk("!!!! toggle value = %ld\n", idx);

     	}

	pch = strsep(&tmp_ptr, SEPARATE_SYMBOL);

       if(pch != NULL)
       {

           if (strict_strtol(pch, 0, &idx) != 0)
              return -EINVAL;
           if (idx == 0)
              mstar_hrt_mode= HRT_WHITE_MODE;
           else
              mstar_hrt_mode= HRT_BLACK_MODE;

     	}
	else
	{
	     mstar_hrt_mode = HRT_WHITE_MODE;
	}

	printk("!!!! mode = %d\n", mstar_hrt_mode);


      return count;
}



static int ns_toggle_release(struct inode *inode, struct file * file)
{
	return 0;
}

static int ns_toggle_open(struct inode *inode, struct file *file)
{
	printk("open tv_nsec_toggle = %ld\n", tv_nsec_bound_toggle);
	return 0;
}

static int ns_bound_open(struct inode *inode, struct file *file)
{
	printk("open tv_nsec_bound = %ld\n", tv_nsec_bound);
	return 0;
}


static ssize_t ns_toggle_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int len = 0;

	if(*ppos != 0)
		return 0;
	len = sprintf(buf+len, "tv_nsec_bound_toggle : %ld\n", tv_nsec_bound_toggle);
	*ppos += len;

	return len;
}





static ssize_t ns_bound_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	ssize_t len = 0;

	if(*ppos != 0)
		return 0;
	len = sprintf(buf+len, "tv_nsec_bound : %ld\n", tv_nsec_bound);
	*ppos += len;

	return len;
}

static int exc_pname_open(struct inode *inode, struct file *file)
{
	printk("open exc_pname1 = %s\n", exc_pname1);
	return 0;
}

static ssize_t exc_pname_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	ssize_t len = 0;

	if(*ppos != 0)
		return 0;
	len = sprintf(buf+len, "exc_pname1 : %s\n", exc_pname1);
	*ppos += len;

	return len;
}

static int exc_pname_release(struct inode *inode, struct file * file)
{
	return 0;
}


// static DEFINE_MUTEX(boost_client_mutex);

static ssize_t exc_pname_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    if (!count)
        return count;

    if(count >= PNAME_MAX_LEN)
        return count;

    memset(exc_pname1, 0, PNAME_MAX_LEN);
    if (copy_from_user(exc_pname1, buf, count))
        return -EFAULT;

    return count;
}

// -----------------------------------------------------------------------------------------------
static int ns_exc_load_bound_open(struct inode *inode, struct file *file)
{
	printk("open tv_exc_load_bound1 = %ld\n", tv_exc_load_bound1);
	return 0;
}

static ssize_t ns_exc_load_bound_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{

    ssize_t len = 0;

    printk("tv_exc_load_bound1 = %ld\n", tv_exc_load_bound1);
    if(*ppos != 0)
        return 0;

    printk("tv_exc_load_bound1 = %ld\n", tv_exc_load_bound1);
    len = sprintf(buf+len, "tv_exc_load_bound1 : %ld\n", tv_exc_load_bound1);
    *ppos += len;

    return len;
    return 0;
}

static int ns_exc_load_bound_release(struct inode *inode, struct file * file)
{
	return 0;
}

static ssize_t ns_exc_load_bound_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char buffer[64];
    long idx = 0;

    if (!count)
        return count;

    if (copy_from_user(buffer, buf, count))
        return -EFAULT;

    buffer[count] = '\0';

    if (strict_strtol(buffer, 0, &idx) != 0)
        return -EINVAL;

    tv_exc_load_bound1 = idx;
    printk("!!!! tv_exc_load_bound1 = %ld\n", tv_exc_load_bound1);

    return count;
}




static int exc_pname_open2(struct inode *inode, struct file *file)
{
    printk("open exc_pname2 = %s\n", exc_pname2);
    return 0;
}

static ssize_t exc_pname_read2(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    ssize_t len = 0;
    if(*ppos != 0)
        return 0;
    len = sprintf(buf+len, "exc_pname2 : %s\n", exc_pname2);
    *ppos += len;

    return len;
}

static int exc_pname_release2(struct inode *inode, struct file * file)
{
	return 0;
}


static ssize_t exc_pname_proc_write2(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    if (!count)
        return count;

    if(count >= PNAME_MAX_LEN)
        return count;

    memset(exc_pname2, 0, PNAME_MAX_LEN);
    if (copy_from_user(exc_pname2, buf, count))
        return -EFAULT;

    return count;
}

// -----------------------------------------------------------------------------------------------
static int ns_exc_load_bound_open2(struct inode *inode, struct file *file)
{
    printk("open tv_exc_load_bound2 = %ld\n", tv_exc_load_bound2);
    return 0;
}


static ssize_t ns_exc_load_bound_read2(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    ssize_t len = 0;
    printk("tv_exc_load_bound2 = %ld\n", tv_exc_load_bound2);


    if(*ppos != 0)
        return 0;

    printk("tv_exc_load_bound2 = %ld\n", tv_exc_load_bound2);
    len = sprintf(buf+len, "tv_exc_load_bound2 : %ld\n", tv_exc_load_bound2);
    *ppos += len;

    return len;
    return 0;
}

static int ns_exc_load_bound_release2(struct inode *inode, struct file * file)
{
	return 0;
}

static ssize_t ns_exc_load_bound_proc_write2(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char buffer[64];
    long idx = 0;

    if (!count)
        return count;

    if (copy_from_user(buffer, buf, count))
        return -EFAULT;

    buffer[count] = '\0';

    if (strict_strtol(buffer, 0, &idx) != 0)
        return -EINVAL;

    tv_exc_load_bound2 = idx;
    printk("!!!! tv_exc_load_bound2 = %ld\n", tv_exc_load_bound2);

    return count;
}


//--------------------------------------------------------------


static int blacklisted_pname_open(struct inode *inode, struct file *file)
{


    mutex_lock(&black_task_list1.black_task_lock);

    if(black_task_list1.black_task_list  != NULL)
    {
         printk("open black_pname1 = %s\n", black_task_list1.black_task_list);
    }


    mutex_unlock(&black_task_list1.black_task_lock);


    return 0;
}

static ssize_t blacklisted_pname_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    ssize_t len = 0;

    mutex_lock(&black_task_list1.black_task_lock);


    if(*ppos != 0 || black_task_list1.black_task_list == NULL)
    {
        mutex_unlock(&black_task_list1.black_task_lock);
        return 0;
    }



    len = sprintf(buf+len, "black_pname1 : %s\n", black_task_list1.black_task_list);



    *ppos += len;

    mutex_unlock(&black_task_list1.black_task_lock);

    return len;
}

static int blacklisted_pname_release(struct inode *inode, struct file * file)
{
	return 0;
}




static ssize_t blacklisted_pname_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{


    if (!count)
        return count;

    if(count >= PNAME_MAX_LEN)
        return count;

    mutex_lock(&black_task_list1.black_task_lock);


    if(black_task_list1.black_task_list != NULL)
    {
        kfree(black_task_list1.black_task_list);
	 black_task_list1.black_task_list_len = 0;
    }

     black_task_list1.black_task_list = kmalloc(count,GFP_KERNEL);
     black_task_list1.black_task_list_len = count;


    if (copy_from_user(black_task_list1.black_task_list, buf, count))
    {

        mutex_unlock(&black_task_list1.black_task_lock);
        return -EFAULT;
    }

    mutex_unlock(&black_task_list1.black_task_lock);


    return count;
}





static int blacklisted_pname_open2(struct inode *inode, struct file *file)
{

    mutex_lock(&black_task_list2.black_task_lock);

    if(black_task_list2.black_task_list  != NULL)
    {
         printk("open black_pname2 = %s\n", black_task_list2.black_task_list);
    }

    mutex_unlock(&black_task_list2.black_task_lock);


    return 0;
}



static ssize_t blacklisted_pname_read2(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    ssize_t len = 0;

   mutex_lock(&black_task_list2.black_task_lock);


    if(*ppos != 0 || black_task_list2.black_task_list == NULL)
    {
        mutex_unlock(&black_task_list2.black_task_lock);
        return 0;
    }



    len = sprintf(buf+len, "black_pname2 : %s\n", black_task_list2.black_task_list);

    mutex_unlock(&black_task_list2.black_task_lock);

    *ppos += len;

    return len;
}


static int blacklisted_pname_release2(struct inode *inode, struct file * file)
{
	return 0;
}


static ssize_t blacklisted_pname_proc_write2(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{


    if (!count)
        return count;

    if(count >= PNAME_MAX_LEN)
        return count;

    mutex_lock(&black_task_list2.black_task_lock);


    if(black_task_list2.black_task_list != NULL)
    {
        kfree(black_task_list2.black_task_list);
	 black_task_list2.black_task_list_len = 0;

    }

    black_task_list2.black_task_list = kmalloc(count,GFP_KERNEL);
    black_task_list2.black_task_list_len = count;


    if (copy_from_user(black_task_list2.black_task_list, buf, count))
    {

        mutex_unlock(&black_task_list2.black_task_lock);
        return -EFAULT;
    }

    mutex_unlock(&black_task_list2.black_task_lock);

    return count;
}



static int ns_low_load_bound_open(struct inode *inode, struct file *file)
{
    printk("open tv_low_load_bound1 = %ld\n", tv_low_load_bound1);
    return 0;
}




static ssize_t ns_low_load_bound_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{

    ssize_t len = 0;

    printk("tv_low_load_bound1 = %ld\n", tv_low_load_bound1);
    if(*ppos != 0)
        return 0;

    printk("tv_low_load_bound1 = %ld\n", tv_low_load_bound1);
    len = sprintf(buf+len, "tv_low_load_bound1 : %ld\n", tv_low_load_bound1);
    *ppos += len;

    return len;
    return 0;
}

static int ns_low_load_bound_release(struct inode *inode, struct file * file)
{
	return 0;
}

static ssize_t ns_low_load_bound_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char buffer[64];
    long idx = 0;

    if (!count)
        return count;

    if (copy_from_user(buffer, buf, count))
        return -EFAULT;

    buffer[count] = '\0';

    if (strict_strtol(buffer, 0, &idx) != 0)
        return -EINVAL;

    tv_low_load_bound1 = idx;
    printk("!!!! tv_low_load_bound1 = %ld\n", tv_low_load_bound1);

    return count;
}




static int ns_low_load_bound2_open(struct inode *inode, struct file *file)
{
    printk("open tv_exc_load_bound2 = %ld\n", tv_low_load_bound2);
    return 0;
}




static ssize_t ns_low_load_bound2_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{

    ssize_t len = 0;

    printk("tv_low_load_bound2 = %ld\n", tv_low_load_bound2);
    if(*ppos != 0)
        return 0;

    printk("tv_low_load_bound2 = %ld\n", tv_low_load_bound2);
    len = sprintf(buf+len, "tv_low_load_bound2 : %ld\n", tv_low_load_bound2);
    *ppos += len;

    return len;
    return 0;
}

static int ns_low_load_bound2_release(struct inode *inode, struct file * file)
{
	return 0;
}

static ssize_t ns_low_load_bound2_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char buffer[64];
    long idx = 0;

    if (!count)
        return count;

    if (copy_from_user(buffer, buf, count))
        return -EFAULT;

    buffer[count] = '\0';

    if (strict_strtol(buffer, 0, &idx) != 0)
        return -EINVAL;

    tv_low_load_bound2 = idx;
    printk("!!!! tv_low_load_bound2 = %ld\n", tv_low_load_bound2);

    return count;
}



//---------------------------------------------------------------
static const struct file_operations ns_bound_proc_fops = {
    .open           = ns_bound_open,
    .read           = ns_bound_read,
    .release        = ns_bound_release,
    .write          = ns_bound_proc_write,
};

static const struct file_operations ns_bound_toggle_proc_fops = {
    .open           = ns_toggle_open,
    .read           = ns_toggle_read,
    .release        = ns_toggle_release,
    .write          = ns_toggle_proc_write,
};

static const struct file_operations exc_pname_proc_fops = {
    .open           = exc_pname_open,
    .read           = exc_pname_read,
    .release        = exc_pname_release,
    .write          = exc_pname_proc_write,
};

static const struct file_operations ns_exc_load_bound_proc_fops = {
    .open           = ns_exc_load_bound_open,
    .read           = ns_exc_load_bound_read,
    .release        = ns_exc_load_bound_release,
    .write          = ns_exc_load_bound_proc_write,
};



static const struct file_operations exc_pname_proc_fops2 = {
    .open           = exc_pname_open2,
    .read           = exc_pname_read2,
    .release        = exc_pname_release2,
    .write          = exc_pname_proc_write2,
};

static const struct file_operations ns_exc_load_bound_proc_fops2 = {
    .open           = ns_exc_load_bound_open2,
    .read           = ns_exc_load_bound_read2,
    .release        = ns_exc_load_bound_release2,
    .write          = ns_exc_load_bound_proc_write2,
};


static const struct file_operations blacklisted_pname_proc_fops = {
    .open           = blacklisted_pname_open,
    .read           = blacklisted_pname_read,
    .release        = blacklisted_pname_release,
    .write          = blacklisted_pname_proc_write,
};


static const struct file_operations blacklisted_pname_proc_fops2 = {
    .open           = blacklisted_pname_open2,
    .read           = blacklisted_pname_read2,
    .release        = blacklisted_pname_release2,
    .write          = blacklisted_pname_proc_write2,
};



static const struct file_operations ns_low_load_bound_proc_fops = {
    .open           = ns_low_load_bound_open,
    .read           = ns_low_load_bound_read,
    .release        = ns_low_load_bound_release,
    .write          = ns_low_load_bound_proc_write,
};


static const struct file_operations ns_low_load_bound2_proc_fops = {
    .open           = ns_low_load_bound2_open,
    .read           = ns_low_load_bound2_read,
    .release        = ns_low_load_bound2_release,
    .write          = ns_low_load_bound2_proc_write,
};



void init_hrt_proc(void)
{

    memset(exc_pname1, 0, sizeof(exc_pname1));
    memset(exc_pname2, 0, sizeof(exc_pname2));


     mutex_init(&black_task_list1.black_task_lock);
     mutex_init(&black_task_list2.black_task_lock);


    ns_boundary_dir = proc_mkdir("nano_sleep_boundary", NULL);
    proc_create("low_bound", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &ns_bound_proc_fops);


    proc_create("toggle", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &ns_bound_toggle_proc_fops);
proc_create("exc_pname", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &exc_pname_proc_fops);
    proc_create("exc_load_bound", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &ns_exc_load_bound_proc_fops);


    proc_create("exc_pname2", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &exc_pname_proc_fops2);
    proc_create("exc_load_bound2", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &ns_exc_load_bound_proc_fops2);


   proc_create("blacklisted_pname", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &blacklisted_pname_proc_fops);

   proc_create("blacklisted_pname2", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &blacklisted_pname_proc_fops2);


   proc_create("low_load_bound", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &ns_low_load_bound_proc_fops);


   proc_create("low_load_bound2", S_IRUSR | S_IWUSR, ns_boundary_dir,
            &ns_low_load_bound2_proc_fops);



}

static int __init hrtimer_patch_init(void)
{

         init_hrt_proc();

         hrtimer_patch_function = run_hrt_timer_patch;

	 return 0;
}

module_init(hrtimer_patch_init);

