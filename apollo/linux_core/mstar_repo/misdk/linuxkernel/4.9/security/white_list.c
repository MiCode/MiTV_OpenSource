#include <linux/cred.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/white_list.h>
#include <linux/sched.h>
#include <linux/ptrace.h>

#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
#include <linux/bitmap.h>
#include <linux/vmalloc.h>
#include <asm-generic/fcntl.h>
#include <asm/thread_info.h>
#include <asm/syscall.h>
#include <trace/syscall.h>
#include "cap_num_toString.h"
#endif

#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
#define FCNTL_BIT_TO_STR(x)	#x
struct gid_sc_map_t {
	struct list_head node;
	long int	gid;
	u32		sc_map[16];
};
static spinlock_t gid_map_glblock;
static DEFINE_MUTEX(user_dump_mutex);
struct list_head gidmap_list;
#endif
struct bypass_entry * bypass_table= NULL;
static kernel_cap_t allow_cap = {
	CAP_TO_MASK(CAP_CHOWN) | CAP_TO_MASK(CAP_SETGID) | CAP_TO_MASK(CAP_SETUID) | CAP_TO_MASK(CAP_NET_RAW) |
	CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_IPC_LOCK) | CAP_TO_MASK(CAP_IPC_OWNER) | CAP_TO_MASK(CAP_SYS_RESOURCE) |
	CAP_TO_MASK(CAP_SYS_PTRACE) | CAP_TO_MASK(CAP_CHOWN) |CAP_TO_MASK(CAP_SYS_MODULE),
	0x0
};

#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
#define DF_DEAULT_VAL	-1
static uid_t uid_filter = DF_DEAULT_VAL;
static int syscall_filter = DF_DEAULT_VAL;
static int cap_filter = DF_DEAULT_VAL;
extern void show_pid_maps(struct task_struct *task);
extern void show_user_backtrace_task(struct task_struct *tsk, int load_elf);
extern struct syscall_metadata *syscall_nr_to_meta(int nr);

/* boot argumets parse */
static int uid_filter_setup(char *str)
{
	uid_filter = (uid_t)simple_strtoul(str, NULL, 0);
	printk("(CAP_DEBUG) setup uid filter, uid = %lu \n",(unsigned long)uid_filter);
	return 1;
}
static int syscall_filter_setup(char *str)
{
	syscall_filter = simple_strtoul(str, NULL, 0);
	printk("(CAP_DEBUG) setup syscall filter, scno = %lu \n",(int)syscall_filter);
	return 1;
}
static int cap_filter_setup(char *str)
{
	cap_filter = simple_strtoul(str, NULL, 0);
	printk("(CAP_DEBUG) setup cap filter, cap = %lu, %s \n",(int)cap_filter, cap_string[cap_filter]);
	return 1;
}

__setup("uid=", uid_filter_setup);
__setup("syscall_no=", syscall_filter_setup);
__setup("cap=", cap_filter_setup);

/* Return 0 if it's the case we want to analyze */
static int dump_filter(kuid_t uid, int scno, int cap)
{
	/* if syscall_no equals 1234, every permission check fail we dump user stack */
	if ( uid_filter == uid.val || syscall_filter == scno || cap_filter == cap ||
			syscall_filter == 1234 )
		return 0;

	return 1;
}

#define max_name_len 128
extern const void *compat_sys_call_table[];
static int get_syscall_name(int nr, char *name)
{
	unsigned long addr;
	int i;
#if defined(CONFIG_ARM64)
	int is_compat = test_thread_flag(TIF_32BIT);
#endif
	if (nr < 0) {
		printk("fail %s, %d \n",__func__,__LINE__);
		return -1;
	}
#if defined(CONFIG_ARM64)
	if(is_compat) {
		if (nr >= __NR_compat_syscalls) {
			printk("fail %s, %d \n",__func__,__LINE__);
			return -1;
		}
		addr = (unsigned long)compat_sys_call_table[nr];
		snprintf(name, max_name_len, "compat:%pF", (void*)addr);
	} else {
#endif
		if (nr >= NR_syscalls) {
			printk("fail %s, %d \n",__func__,__LINE__);
			return -1;
		}
		addr = (unsigned long)sys_call_table[nr];
		snprintf(name, max_name_len, "%pF", (void*)addr);
#if defined(CONFIG_ARM64)
	}
#endif
	if (name) {
		i = 0;
		while(name[i] != '\0' && i < 128) {
			if (name[i] == '+') {
				name[i] = '\0';
				break;
			}
			i++;
		}
	}

	return 0;
}
#endif

int is_bypass(int cap, kgid_t c_gid, kuid_t c_uid)
{
#if defined(CONFIG_ARM64)
	int scno = current_pt_regs()->syscallno;
#elif defined(CONFIG_ARM)
	int scno = current_thread_info()->syscall;
#endif
	int i;
	int ret = -1;
#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
	struct gid_sc_map_t *mnod;
	u32 *map = NULL;
	unsigned long flag, fcntl_bits;
	char scname[128];

	get_syscall_name(scno, scname);
#endif

	if (!bypass_table) {
#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
		if(BYPASS_NUM)
			printk("\033[1;45m%s, %d, bypass table is not init!\033[m\n",__func__,__LINE__);
#endif
	} else {
		for (i=0; i<BYPASS_NUM; i++) {
			if (c_gid.val == bypass_table[i].gid && c_uid.val == bypass_table[i].uid && bypass_table[i].__entry_status == BP_STATUS_ENABLE) {
				return 0;
			}
		}
	}


#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
	spin_lock(&gid_map_glblock);

	list_for_each_entry(mnod ,&gidmap_list, node)
	{
		if (mnod->gid == c_gid.val)
		{
			map = mnod->sc_map;
			break;
		}
	}
	if (!map)
	{
		mnod = vmalloc(sizeof(struct gid_sc_map_t));
		memset(mnod, 0, sizeof(struct gid_sc_map_t));
		mnod->gid = c_gid.val;
		map = mnod->sc_map;
		list_add(&mnod->node, &gidmap_list);
	}
	if (!map)
	{
		printk("!!! error, create map fail !!!\n");
		spin_unlock(&gid_map_glblock);
		return ret;
	}

	/* debug msg */
	if (!test_and_set_bit(scno%512, map)) {
		printk("\033[1;42m(CAP_DEBUG)[%3u:%s] ret: %d, GID: %u, UID: %u, cap:[%2u][%s] , tgid : %u , pid : %u , %x , %s\033[m\n", scno , scname ,
			ret , c_gid.val, c_uid.val, cap , cap_string[cap] , current_thread_info()->task->tgid, current_thread_info()->task->pid , allow_cap.cap[0] ,current_thread_info()->task->comm);
	}


	spin_unlock(&gid_map_glblock);
#if defined(CONFIG_ARM64)
	/* open, compat_open, open_at, compat_openat */
	if (scno == 0 || scno == 5 || scno == 56 || scno == 322) {
#else
	if (scno == 5) {
#endif
		flag = current_thread_info()->sc_mode_flag;
		fcntl_bits = flag & 0x3;
		printk("\033[1;32m(CAP_DEBUG)[%3u:%s] filanme= %s, flag= %lx, fcntl_bit= %lx, fcntl_mode= %s, ret: %d, GID: %u, UID: %u, cap:[%2u][%20s] , tgid : %u , pid : %u\033[m\n", scno , scname ,
			current_thread_info()->lfn, flag, fcntl_bits,
			fcntl_bits == O_ACCMODE	?	FCNTL_BIT_TO_STR(O_ACCMODE) :
			fcntl_bits == O_RDONLY	?	FCNTL_BIT_TO_STR(O_RDONLY) :
			fcntl_bits == O_WRONLY	?	FCNTL_BIT_TO_STR(O_WRONLY):
			fcntl_bits == O_RDWR	?	FCNTL_BIT_TO_STR(O_RDWR):
			"other",
			ret, c_gid.val, c_uid.val, cap , cap_string[cap] , current_thread_info()->task->tgid, current_thread_info()->task->pid );
	} else if (scno == 40||scno == 39||scno == 38||scno == 11||scno == 10||scno == 33)
		printk("\033[1;32m(CAP_DEBUG)[%u] filename :%s ,mode/flag: %x , cap: %d , tgid: %u , pid: %u , egid : %u , sgid: %u\033[m\n",  scno , current_thread_info()->lfn, current_thread_info()->sc_mode_flag, cap, current_thread_info()->task->tgid, current_thread_info()->task->pid , current_egid() , current_sgid());

	if (!dump_filter(current_uid(), scno, cap)) {
		/* current not support */
		//show_usr_info(current, signal_pt_regs(), 0);
	}
#if CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG_BYPASS_ALL
	/* bypass all permission check */
	ret = 0;
#endif
#endif
	return ret;
}

#define check_size 0

/* return 0 if input size is ok */
static int fsize_chk(loff_t size, loff_t exp_size)
{
#if check_size
	loff_t s = exp_size, l = exp_size;
	if (!exp_size)
		return -1;

	s -= exp_size/10;
	l += exp_size/10;

	return size >= s && size <= l ? 0 : -1;
#else
	return 0;
#endif
}

int bypass_chk(kgid_t c_gid, kuid_t c_uid, char *filename, loff_t size)
{

	int i;

	if (!bypass_table) {
#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
		if(BYPASS_NUM)
			printk("\033[1;44m %s, %d, bypass_table not init!!! \033[m\n",__func__,__LINE__);
#endif
		return -1;
	}

	for (i=0; i<BYPASS_NUM; i++) {
		if (c_gid.val == bypass_table[i].gid && c_uid.val == bypass_table[i].uid) {
			if (bypass_table[i].__entry_status == BP_STATUS_DISABLE)
			{
				if( strlen(filename)==strlen(bypass_table[i].exec_name)
					&& !strncmp( filename , bypass_table[i].exec_name , strlen(filename) ) ) {
					if ( !fsize_chk(size, bypass_table[i].fsize) ) {
#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
						if (strlen(filename)>256)
							printk("\033[1;44m%s : size exceed 256\033[m\n" , filename );
#endif
						bypass_table[i].__entry_status = BP_STATUS_ENABLE;
						break;
					}
#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
					else /* debug msg: sizes are not as expected */
						printk("\033[1;44m dbg msg: %s, %d, name= %s, size= %llu, saved_size= %llu \033[m\n" ,__func__,__LINE__, bypass_table[i].exec_name,(unsigned long long)size, (unsigned long long)bypass_table[i].fsize);
#endif
				} else {
					bypass_table[i].__entry_status = BP_STATUS_NEVER;
				}
			}
			return 0;
		}
	}

	return 0;
}

static void __init init_bp_entry(char *exec_name, gid_t gid, uid_t uid, loff_t size)
{
	unsigned int len = strlen(exec_name) > (BP_FN_LEN-1)? strlen(exec_name):(BP_FN_LEN-1);
	static int i=0;
	struct bypass_entry *new;

	if (i==BYPASS_NUM) {
		printk("\033[1;41mfull table\033[m");
		return;
	}
	new = &bypass_table[i];
	new->gid = gid;
	new->uid = uid;
	new->fsize = size;
	strncpy(new->exec_name, exec_name, len);
	i++;
	return;
}

int __init white_list_init(void)
{
	// init table here
	bypass_table = kmalloc(sizeof(struct bypass_entry)*BYPASS_NUM , GFP_KERNEL);
	memset(bypass_table, 0, sizeof(struct bypass_entry)*BYPASS_NUM );

	/* application name size is limit by 256 */
	init_bp_entry("/a_fake_bypass_entry" , 921, 921 , 0);
#ifdef CONFIG_MP_AMAZON_NON_ROOT_SECURE_DEBUG
	INIT_LIST_HEAD(&gidmap_list);
	spin_lock_init(&gid_map_glblock);
#endif

	return 0;
}

