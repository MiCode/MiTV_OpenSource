/*
 * kdbg-func.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by lee jung-seung(js07.lee@samsung.com)
 *
 * Created by lee jung-seung (js07.lee@samsung.com)
 *
 * 2009-11-17 : Added, Virt to physical ADDR converter.
 * 2009-11-20 : Modified, show_task_state_backtrace for
 * taking real backtrace on kernel.
 * NOTE:
 *
 */

#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <asm/pgtable.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>

#include <sec_topthread.h>

#include <kdebugd.h>
#include "kdbg_arch_wrapper.h"
#include "kdbg_util.h"
#include "sec_workq.h"

#include <linux/bootmem.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
#include "sec_perfcounters.h"
#endif

#ifdef CONFIG_ELF_MODULE
#include "elf/kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_ADVANCE_OPROFILE
#include "aop/aop_oprofile.h"
#endif /* CONFIG_ADVANCE_OPROFILE */

#ifdef CONFIG_VIRTUAL_TO_PHYSICAL
#include <linux/err.h>
#include <linux/highmem.h>
#endif

#ifdef CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#include <linux/module.h>
struct proc_dir_entry *kdebugd_cpu_dir;

#ifdef CONFIG_KDEBUGD_HUB_DTVLOGD
extern int dtvlogd_buffer_printf(void);
#endif

#ifdef CONFIG_ELF_MODULE

/* Circular Array for containing Program Counter's and time info*/
struct sec_kdbg_pc_info {
	unsigned int pc;	/* program counter value */
	struct timespec pc_time;	/* time of the program counter */
	unsigned int cpu;
};
#endif /* CONFIG_ELF_MODULE */

#ifndef OFFSET_MASK
#define OFFSET_MASK (PAGE_SIZE - 1)
#endif

#define OFFSET_ALIGN_MASK (OFFSET_MASK & ~(0x3))
#define DUMP_SIZE 0x400
#define P2K(x) ((x) << (PAGE_SHIFT - 10))

/* kdebugd Functions */

unsigned int kdebugd_nobacktrace;

int get_user_stack(struct task_struct *task,
	unsigned int **us_content, unsigned long *start, unsigned long *end)
{
	struct pt_regs *regs;
	struct vm_area_struct *vma;
	int no_of_us_value = 0;
	struct mm_struct *mm = NULL;

	regs = task_pt_regs(task);

	mm = get_task_mm(task);
	vma = find_vma(task->mm, task->user_ssp);
	if (vma) {
		unsigned long top;
		unsigned long p;
		mm_segment_t fs;
		unsigned int *tmp_user_stack;
		unsigned long bottom;
        #if defined(CONFIG_MP_PLATFORM_ARM_64bit_POARTING)
                 if (compat_user_mode(regs))
                     bottom = regs->compat_sp;
                 else
                     bottom = regs->sp;

        #else
		     bottom = regs->ARM_sp;
        #endif

		top = task->user_ssp;
		p = bottom & ~31;
		*start = bottom;
		*end = top;

		*us_content = (unsigned int *)vmalloc(
				(top - bottom) * sizeof (unsigned int));

		if (!*us_content) {
			printk ("%s %d> No memory to build user stack\n",
					__FUNCTION__, __LINE__);
			if (mm)
				mmput(mm);
			return no_of_us_value;
		}

		printk("stack area (0x%08lx ~ 0x%08lx)\n", vma->vm_start, vma->vm_end);

        #if defined(CONFIG_MP_PLATFORM_ARM_64bit_POARTING)
                 if (compat_user_mode(regs))
                 {
		      printk("User stack area (0x%08lx ~ 0x%08lx)\n",
				(unsigned long)regs->compat_sp, (unsigned long)vma->vm_end);
                 }
                 else
                 {
		      printk("User stack area (0x%08lx ~ 0x%08lx)\n",
				(unsigned long)regs->sp, vma->vm_end);

                 }
        #else
		      printk("User stack area (0x%08llx ~ 0x%08lx)\n",
				regs->ARM_sp, vma->vm_end);

        #endif
		/*
		 * We need to switch to kernel mode so that we can use __get_user
		 * to safely read from kernel space.  Note that we now dump the
		 * code first, just in case the backtrace kills us.
		 */
		fs = get_fs();
		set_fs(KERNEL_DS);

		/* copy the pointer  to tmp, to fill the app cookie value */
		tmp_user_stack = *us_content;

		for (p = bottom & ~31; p < top; p += 4) {
			if (!(p < bottom || p >= top)) {
				unsigned int val;
				__get_user(val, (unsigned long *)p);
				if (val) {
					*(tmp_user_stack+no_of_us_value++) = p & 0xffff;
					*(tmp_user_stack+no_of_us_value++) = val;
					/*printk("<%08x - %08x> ",
					 *(tmp_user_stack+no_of_us_value++), val);*/
				}
			}
		}

		set_fs(fs);

	}
	if (mm)
		mmput(mm);
	return no_of_us_value;
}


/*
* kdbg_dump_mem:
* to dump user stack
*/
void kdbg_dump_mem(const char *str, unsigned long bottom, unsigned long top)
{
	unsigned long p = bottom & ~31;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);
	PRINT_KD("%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

	/* We print for 8 addresses (8*4 = 32) in a line.
	 * If address is outside the limit, blank is printed
	 */
	for (p = bottom & ~31; p < top;) {
		PRINT_KD("%04lx: ", p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				PRINT_KD("         ");
			else {
				__get_user(val, (unsigned long *)p);
				PRINT_KD("%08x ", val);
			}
		}
		PRINT_KD("\n");
	}
	set_fs(fs);
}

#ifdef CONFIG_SHOW_TASK_STATE
static int show_task_state(void)
{
	kdebugd_nobacktrace = 1;
	show_state();
	task_state_help();

	return 1;
}
#endif

#ifdef CONFIG_TASK_STATE_BACKTRACE
static int show_task_state_backtrace(void)
{
	struct task_struct *tsk;
	long event;

	kdebugd_nobacktrace = 0;

	PRINT_KD("\n");
	PRINT_KD("Enter pid of task (0 for all task)\n");
	PRINT_KD("===>  ");
	event = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD("\n");

	if (event) {
		/* If task count already incremented if task exists */
		tsk = get_task_with_given_pid(event);
		if (tsk == NULL) {
			PRINT_KD("\n");
			PRINT_KD("[ALERT] NO Thread\n");
			return 1;	/* turn on the menu */
		}
	} else {
		show_state();
		return 1;	/* turn on the menu */
	}

	task_lock(tsk);

	wrap_show_task(tsk);

	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_given_pid */
	put_task_struct(tsk);

	return 1;
}
#endif

#ifdef CONFIG_TASK_FOR_COREDUMP
static int kill_task_for_coredump(void)
{
	struct task_struct *tsk;

	/* If task count already incremented if task exists */
	tsk = get_task_with_pid();
	if (tsk == NULL) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] NO Thread\n");
		return 1;	/* turn on the menu */
	}

	/* Send Signal for killing the task */
	force_sig(SIGABRT, tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);

	return 1;	/* turn on the menu */
}
#endif

/* 6. Dump task register with pid */
#ifdef CONFIG_SHOW_USER_THREAD_REGS
static int show_user_thread_regs(void)
{
	struct task_struct *tsk;
	struct pt_regs *regs;

	/* If task count already incremented if task exists */
	tsk = get_task_with_pid();
	if (tsk == NULL) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] NO Thread\n");
		return 1;	/* turn on the menu */
	}

	task_lock(tsk);
	/* Namit 10-Dec-2010 in funciton __show_reg.
	 * cpu showing is current running CPU which
	 * can be mismatched with the task CPU*/
	regs = task_pt_regs(tsk);
	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);
	show_regs_wr(regs);

	return 1;		/* turn on the menu */
}
#endif

/* 7. Dump task pid maps with pid */
#ifdef CONFIG_SHOW_USER_MAPS_WITH_PID
static void __show_user_maps_with_pid(void)
{
	struct task_struct *tsk;

	/* If task count already incremented if task exists */
	tsk = find_user_task_with_pid();
	if (tsk == NULL)
		return;

	task_lock(tsk);

	PRINT_KD("======= Maps Summary Report =======================\n");
	PRINT_KD("       Total VMA Count : %d\n", tsk->mm->map_count);
	PRINT_KD("       Total VMA Size  : %lu kB\n",
			P2K(tsk->mm->total_vm));
	PRINT_KD("==============================================\n");

	show_pid_maps_wr(tsk);
	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * find_user_task_with_pid */
	put_task_struct(tsk);
}

static int show_user_maps_with_pid(void)
{
	__show_user_maps_with_pid();

	return 1;
}
#endif

/* 8. Dump user stack with pid */
#ifdef CONFIG_SHOW_USER_STACK_WITH_PID
static void __show_user_stack_with_pid(void)
{
	struct task_struct *tsk = NULL;
	struct pt_regs *regs = NULL;
	struct mm_struct *tsk_mm;

	/* If task count already incremented if task exists */
	tsk = find_user_task_with_pid();

	if (tsk == NULL)
		return;

	task_lock(tsk);

	regs = task_pt_regs(tsk);
	task_unlock(tsk);

	tsk_mm = get_task_mm(tsk);
	if (tsk_mm) {
		use_mm(tsk_mm);
		//show_user_stack(tsk, regs);
		unuse_mm(tsk_mm);
		mmput(tsk_mm);
	} else {
		PRINT_KD("No mm\n");
	}

	/* Decrement usage count which is incremented in
	 * find_user_task_with_pid */
	put_task_struct(tsk);

}

static int show_user_stack_with_pid(void)
{
	__show_user_stack_with_pid();

	return 1;
}
#endif

/* 9. Convert Virt Addr(User) to Physical Addr */
#ifdef CONFIG_VIRTUAL_TO_PHYSICAL
static void print_get_physaddr(unsigned long pfn,
			       unsigned long p_addr, unsigned long k_addr)
{

	unsigned long dump_limit = k_addr + DUMP_SIZE;

	if ((PAGE_MASK & k_addr) != (PAGE_MASK & dump_limit))
		dump_limit = (PAGE_MASK & k_addr) + OFFSET_MASK;

	/* Inform page table walking procedure */
	PRINT_KD
	    ("\n===============================================================");
	PRINT_KD("\nPhysical Addr:0x%08lx, Kernel Addr:0x%08lx", p_addr,
		 k_addr);
	PRINT_KD
	    ("\n===============================================================");
	PRINT_KD("\n Page Table walking...! [Aligned addresses]");
	PRINT_KD("\n PFN :0x%08lx\n PHYS_ADDR: 0x%08lx ", pfn,
		 (pfn << PAGE_SHIFT));
	PRINT_KD("\n VIRT_ADDR: 0x%08lx + 0x%08lx", PAGE_MASK & k_addr,
		 OFFSET_ALIGN_MASK & p_addr);
	PRINT_KD("\n VALUE    : 0x%08lx (addr:0x%08lx)",
		 *(unsigned long *)k_addr, k_addr);
	PRINT_KD
	    ("\n==============================================================");
	kdbg_dump_mem("\nKERNEL_ADDR", k_addr, dump_limit);
	PRINT_KD
	    ("\n==============================================================");
}

unsigned long get_physaddr(struct task_struct *tsk, unsigned long u_addr,
			   int detail)
{
	struct page *page;
	struct vm_area_struct *vma = NULL;
	unsigned long k_addr, k_preaddr, p_addr, pfn, pfn_addr;
	int high_mem = 0;

	/* find vma w/ user address */
	vma = find_vma(tsk->mm, u_addr);
	if (!vma) {
		PRINT_KD("NO VMA\n");
		goto out;
	}

	/* get page struct w/ user address */
	page = follow_page(vma, u_addr, 0);

	/*Aug-17:Added check to see if the returned value is ERROR */
	if (!page || IS_ERR(page)) {
		PRINT_KD("NO PAGE\n");
		goto out;
	}

	if (PageReserved(page))
		PRINT_KD("[Zero Page]\n");

	if (PageHighMem(page)) {
		PRINT_KD("[Higmem Page]\n");
		high_mem = 1;
	}

	/* Calculate pfn, physical address,
	 * kernel mapped address w/ page struct */
	pfn = page_to_pfn(page);

	/*Aug-5-2010:modified comparison operation into macro check */
	/* In MSTAR pfn_valid is expanded as follows
	 * arch/mips/include/asm/page.h:#define pfn_valid(pfn)
	 * ((pfn) >= ARCH_PFN_OFFSET && (pfn) < max_mapnr)
	 * where the value of ARCH_PFN_OFFSET is 0 for MSTAR.
	 * This causes prevent to display warning that
	 * comparison >=0 is always true.
	 * Since this is due to system macro expansion
	 * this warning is acceptable.
	 */
	if (!pfn_valid(pfn)) {
		PRINT_KD("PFN IS NOT VALID\n");
		goto out;
	}

	/*Aug-5-2010:removed custom function to reuse system macro */
	pfn_addr = page_to_phys(page);

	if (!pfn_addr) {
		PRINT_KD("CAN'T CONVERT PFN TO PHYSICAL ADDR\n");
		goto out;
	}

	p_addr = pfn_addr + (OFFSET_ALIGN_MASK & u_addr);

	/*Aug-6-2010:Map the page into kernel if memory is in HIGHMEM */
	if (high_mem)
		k_preaddr = (unsigned long)kmap(page);
	else
		k_preaddr = (unsigned long)page_address(page);

	if (!k_preaddr) {
		PRINT_KD
		    ("KERNEL ADDRESS CONVERSION FAILED (k_preaddr:0x%08lx)\n",
		     k_preaddr);
		goto out;
	}

	k_addr = k_preaddr + (OFFSET_ALIGN_MASK & u_addr);
	/* In MSTAR virt_addr_valid is expanded as follows
	 * arch/mips/include/asm/page.h:#define virt_addr_valid(kaddr)   pfn_valid(PFN_DOWN(virt_to_phys(kaddr)))
	 * where the warning for "pfn_valid" system macro is explained above.
	 * Since this system macro depends on pfn_valid, which is another system macro the
	 * warning that comparison >=0 is always true due to macro expansion is acceptable
	 */
	if (!high_mem && (!virt_addr_valid((void *)k_addr)))
		PRINT_KD("INVALID KERNEL ADDRESS\n");

	if (detail == 1)
		print_get_physaddr(pfn, p_addr, k_addr);
	else
		PRINT_KD("Physical Addr:0x%08lx, Kernel Addr:0x%08lx\n", p_addr,
			 k_addr);
	if (high_mem)
		kunmap(page);

	return k_addr;
out:
	return 0;
}

static void __physical_memory_converter(void)
{
	struct task_struct *tsk;
	unsigned long p_addr, u_addr;

	tsk = get_task_with_pid();
	if (tsk == NULL || !tsk->mm) {
		PRINT_KD("\n[ALERT] %s Thread",
			 (tsk == NULL) ? "No" : "Kernel");
		return;
	}

	/* get address */
	PRINT_KD("\nEnter memory address....\n===>  ");
	u_addr = debugd_get_event_as_numeric(NULL, NULL);

	task_lock(tsk);
	/* Convert User Addr => Kernel mapping Addr */
	p_addr = get_physaddr(tsk, u_addr, 1);
	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);

	if (!p_addr) {
		PRINT_KD("\n [KDEBUGD] "
			 "The virtual Address(user:0x%08lx) is not mapped to real memory",
			 u_addr);
	} else {
		PRINT_KD("\n [KDEBUGD] physical address :0x%08lx", p_addr);
	}

	return;
}

static int physical_memory_converter(void)
{
	__physical_memory_converter();
	return 1;
}
#endif /* CONFIG_VIRTUAL_TO_PHYSICAL */

#ifndef CONFIG_SMP
#ifdef CONFIG_MEMORY_VALIDATOR

/*Struct defined in linux/kdebugd.h*/
struct kdbg_mem_watch kdbg_mem_watcher = { 0,};

static void __memory_validator(void)
{
	struct task_struct *tsk;
	struct mm_struct *next_mm;
	mm_segment_t fs;
	unsigned long k_addr;

	/* If task count already incremented if task exists */
	tsk = get_task_with_pid();
	if (tsk == NULL) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] No Thread\n");
		return;
	}

	if (!tsk->mm) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] Kernel Thread\n");
		goto error_exit;
	}

	kdbg_mem_watcher.watching = 0;
	next_mm = tsk->active_mm;

	/* get pid */
	kdbg_mem_watcher.pid = tsk->pid;
	kdbg_mem_watcher.tgid = tsk->tgid;

	/* get address */
	PRINT_KD("\n");
	PRINT_KD("Enter memory address....\n");
	PRINT_KD("===>  ");
	kdbg_mem_watcher.u_addr = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n");

	/* check current active mm */
	if (current->active_mm != next_mm) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] This is Experimental Function..\n");

		k_addr = get_physaddr(tsk, kdbg_mem_watcher.u_addr, 0);
		if (k_addr == 0) {
			PRINT_KD
				("[ALERT] Page(addr:%08x) is not loaded in memory\n",
				 kdbg_mem_watcher.u_addr);
			goto error_exit;
		}

		kdbg_mem_watcher.buff = *(unsigned long *)k_addr;
	} else {

		/* Get User memory value */
		fs = get_fs();
		set_fs(KERNEL_DS);
		if (!access_ok
				(VERIFY_READ, (unsigned long *)kdbg_mem_watcher.u_addr,
				 sizeof(unsigned long *))) {
			PRINT_KD("[ALERT] Invalid User Address\n");
			set_fs(fs);
			goto error_exit;
		}
		__get_user(kdbg_mem_watcher.buff,
				(unsigned long *)kdbg_mem_watcher.u_addr);
		set_fs(fs);
	}
	/* Print Information */
	PRINT_KD("=====================================================\n");
	PRINT_KD(" Memory Value Watcher....!\n");
	PRINT_KD(" [Trace]  PID:%d  value:0x%08x (addr:0x%08x)\n",
			kdbg_mem_watcher.pid, kdbg_mem_watcher.buff,
			kdbg_mem_watcher.u_addr);
	PRINT_KD("=====================================================\n");
	kdbg_mem_watcher.watching = 1;

error_exit:
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);
	/* Check whenever invoke schedule().!! */
}

static int memory_validator(void)
{
	__memory_validator();

	return 1;
}
#endif /*CONFIG_MEMORY_VALIDATOR */
#endif /*CONFIG_SMP */

/* 11. Trace thread execution(look at PC) */

struct timespec timespec_interval = {.tv_sec = 0, .tv_nsec = 100000};

#ifdef CONFIG_TRACE_THREAD_PC

static unsigned int prev_pc;
#define SEC_PC_TRACE_MS           ((5 * 1000)/HZ)	/* 5 MS Timer tickes  */
#define SEC_PC_TRACE_MAX_ITEM   100	/* Max no of PC's in circular array */

#ifdef CONFIG_ELF_MODULE
static char *trace_func_name;
static int sec_pc_trace_idx;	/* Index for Currnet Program Counter */
#endif /* CONFIG_ELF_MODULE */

static int pctracing;
struct sec_kdbg_pc_info *sec_pc_trace_info;
struct task_struct *sec_pc_trace_tsk;
pid_t trace_tsk_pid;
struct hrtimer trace_thread_timer;

static enum hrtimer_restart show_pc(struct hrtimer *timer)
{
	unsigned int cur_pc, cpu = 0;
	struct task_struct *trace_tsk = 0;
	ktime_t now;
	struct timespec curr_timespec;
	/* Check whether timer is still tracing already dead thread or not */

	now = ktime_get();
	/*Take RCU read lock register can be changed */
	rcu_read_lock();
	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */
	trace_tsk = find_task_by_pid_ns(trace_tsk_pid, &init_pid_ns);
	if (trace_tsk) {
		/*Increment usage count */
		get_task_struct(trace_tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (!trace_tsk) {
		PRINT_KD("\n");
		PRINT_KD("[kdebugd] traced task is killed...\n");
		prev_pc = 0;
		return HRTIMER_NORESTART;
	}

	/* Cant take task lock here may be chances for hang */
	cur_pc = KSTK_EIP(trace_tsk);

	cpu = task_cpu(trace_tsk);

	/* Decrement usage count */
	put_task_struct(trace_tsk);

	if (prev_pc != cur_pc) {

		ktime_get_ts(&curr_timespec);
#ifdef CONFIG_ELF_MODULE
		/* Save the PC and time in a array of structure of sec_pc_trace_info
		 * and the symbol can be extracted out from sec_pc_trace_pc_symbol
		 * thread. */

		/* buffer not allocated dont process further */
		if (sec_pc_trace_info) {

			sec_pc_trace_info[sec_pc_trace_idx].pc = cur_pc;
			sec_pc_trace_info[sec_pc_trace_idx].pc_time.tv_sec =
				curr_timespec.tv_sec;
			sec_pc_trace_info[sec_pc_trace_idx].pc_time.tv_nsec =
				curr_timespec.tv_nsec;
			sec_pc_trace_info[sec_pc_trace_idx].cpu = cpu;
			sec_pc_trace_idx =
				(sec_pc_trace_idx + 1) % SEC_PC_TRACE_MAX_ITEM;
		}
#else
		/* 		PRINT_KD ("\n"); */
		PRINT_KD
			("[kdebugd] [CPU %d] Pid:%d  PC:0x%08x\t\t(TIME:%ld.%ld)\n",
			 cpu, trace_tsk_pid, cur_pc, curr_timespec.tv_sec, curr_timespec.tv_nsec);
#endif /* CONFIG_ELF_MODULE */
	}

	prev_pc = cur_pc;

	hrtimer_forward(&trace_thread_timer, now,
			timespec_to_ktime(timespec_interval));
	return HRTIMER_RESTART;
}

static void start_trace_timer(void)
{
	hrtimer_init(&trace_thread_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	trace_thread_timer._softexpires = timespec_to_ktime(timespec_interval);
	trace_thread_timer.function = show_pc;
	hrtimer_start(&trace_thread_timer, trace_thread_timer._softexpires,
		      HRTIMER_MODE_REL);
}

static inline void end_trace_timer(void)
{
	hrtimer_cancel(&trace_thread_timer);
}

static void turnoff_trace_thread_pc(void)
{
	if (pctracing != 0) {
		PRINT_KD("\n");
		PRINT_KD("trace thread pc OFF!!\n");
		end_trace_timer();
		prev_pc = 0;
		pctracing = 0;

#ifdef CONFIG_ELF_MODULE
		if (sec_pc_trace_tsk)
			kthread_stop(sec_pc_trace_tsk);	/* Stop the symbol resolution thread */

		PRINT_KD(" Kernel Thread for symbol resolution done ...\n");
		sec_pc_trace_tsk = NULL;

		if (trace_func_name) {
			KDBG_MEM_DBG_KFREE(trace_func_name);
			trace_func_name = NULL;
		}
		if (sec_pc_trace_info) {
			KDBG_MEM_DBG_KFREE(sec_pc_trace_info);
			sec_pc_trace_info = NULL;
		}
#endif /* CONFIG_ELF_MODULE */

	}
}

#ifdef CONFIG_ELF_MODULE

/* function takes the program counter from the array which is
 * populated in show_pc function
 * and extract out corresponding symbol from symbol database
 * of application */

static int sec_pc_trace_pc_symbol(void *arg)
{
	static int rd_idx;
	int wr_idx = 0;
	int ii = 0;
	int buffer_count = 0;

	/*  Start from write index  */
	rd_idx = sec_pc_trace_idx;

	/* break the task function, when kthead_stop is called */
	while (!kthread_should_stop()) {
		/* buffer not allocated no need to process further */
		if (!sec_pc_trace_info)
			break;

		wr_idx = sec_pc_trace_idx;	/* take the idx lacally to prevent run time change */

		/* case of empty array or read and write index are in same position
		   can be handle automaticaly (buffer_count = 0) */
		if (rd_idx < wr_idx) {	/*  write ptr is leading  */
			buffer_count = wr_idx - rd_idx;
		} else if (rd_idx > wr_idx) {	/* read ptr rolled back first go to end
										   and then roll the write ptr */
			buffer_count = SEC_PC_TRACE_MAX_ITEM - rd_idx + wr_idx;
		} else {	/* case of empty array or read and write index are in same position
					   (buffer_count = 0) */
			buffer_count = 0;
		}

		for (ii = 0; ii < buffer_count; ++ii, ++rd_idx) {
			rd_idx = rd_idx % SEC_PC_TRACE_MAX_ITEM;

			WARN_ON(!sec_pc_trace_info[rd_idx].pc);

			if (trace_func_name) {
				/* get the symbol for program counter */
				if (!kdbg_elf_get_symbol_by_pid(trace_tsk_pid,
							sec_pc_trace_info
							[rd_idx].pc,
							trace_func_name)) {
					strncpy(trace_func_name, "???",
							KDBG_ELF_SYM_NAME_LENGTH_MAX -
							1);
					trace_func_name
						[KDBG_ELF_SYM_NAME_LENGTH_MAX - 1] =
						0;
				}
			}

			PRINT_KD
				("[kdebugd] [CPU %d] Pid:%d  PC:0x%08x (TIME:%06ld.%-9ld)  %s\n",
				 sec_pc_trace_info[rd_idx].cpu, trace_tsk_pid,
				 sec_pc_trace_info[rd_idx].pc,
				 sec_pc_trace_info[rd_idx].pc_time.tv_sec,
				 sec_pc_trace_info[rd_idx].pc_time.tv_nsec,
				 (trace_func_name ? trace_func_name : "???"));
		}

		rd_idx = rd_idx % SEC_PC_TRACE_MAX_ITEM;
		msleep(SEC_PC_TRACE_MS);	/* N (default 5) timer ticks sleep */
	}

	return 0;
}

#endif /* CONFIG_ELF_MODULE */

static void __trace_thread_pc(void)
{
	struct task_struct *trace_tsk = NULL;

	if (pctracing == 0) {
		PRINT_KD("trace thread pc ON!!\n");
		trace_tsk = get_task_with_pid();

		if (trace_tsk == NULL || !trace_tsk->mm) {
			PRINT_KD("[ALERT] %s Thread\n",
					trace_tsk == NULL ? "No" : "Kernel");
			return;
		}
		trace_tsk_pid = trace_tsk->pid;

		/* Decrement usage count which is incremented in
		 * get_task_with_pid */
		put_task_struct(trace_tsk);

#ifdef CONFIG_ELF_MODULE
		BUG_ON(sec_pc_trace_info);
		sec_pc_trace_info =
			(struct sec_kdbg_pc_info *)
			KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
					SEC_PC_TRACE_MAX_ITEM *
					sizeof(struct sec_kdbg_pc_info),
					GFP_KERNEL);

		BUG_ON(!sec_pc_trace_info);
		if (!sec_pc_trace_info)
			return;
		memset(sec_pc_trace_info, 0,
				(SEC_PC_TRACE_MAX_ITEM *
				 sizeof(struct sec_kdbg_pc_info)));

		BUG_ON(trace_func_name);
		trace_func_name = (char *)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_KDEBUGD_MODULE, KDBG_ELF_SYM_NAME_LENGTH_MAX,
			 GFP_KERNEL);
		BUG_ON(!trace_func_name);
		if (!trace_func_name)
			return;

		kdbg_elf_load_elf_db_by_pids(&trace_tsk_pid, 1);

		BUG_ON(sec_pc_trace_tsk);
		/* create thread for symbol resolution */
		sec_pc_trace_tsk = kthread_create(sec_pc_trace_pc_symbol, NULL,
				"sec_pc_trace_tsk");
		if (IS_ERR(sec_pc_trace_tsk)) {
			PRINT_KD
				("%s:Symbol Resolve thread Creation Failed --------\n",
				 __FUNCTION__);
			sec_pc_trace_tsk = NULL;
		}

		sec_pc_trace_idx = 0;
		if (sec_pc_trace_tsk) {
			sec_pc_trace_tsk->flags |= PF_NOFREEZE;
			wake_up_process(sec_pc_trace_tsk);
		}
#endif /* CONFIG_ELF_MODULE */

		start_trace_timer();
		pctracing = 1;
	} else {
		PRINT_KD("trace thread pc OFF!!\n");
		end_trace_timer();
		prev_pc = 0;
		pctracing = 0;

#ifdef CONFIG_ELF_MODULE
		if (sec_pc_trace_tsk)
			kthread_stop(sec_pc_trace_tsk);	/* Stop the symbol resolution thread */

		PRINT_KD(" Kernel Thread for symbol resolution done ...\n");
		sec_pc_trace_tsk = NULL;

		if (trace_func_name) {
			KDBG_MEM_DBG_KFREE(trace_func_name);
			trace_func_name = NULL;
		}

		if (sec_pc_trace_info) {
			KDBG_MEM_DBG_KFREE(sec_pc_trace_info);
			sec_pc_trace_info = NULL;
		}
#endif /* CONFIG_ELF_MODULE */

	}
}

static int trace_thread_pc(void)
{
	__trace_thread_pc();

	return 0;
}
#endif /* CONFIG_TRACE_THREAD_PC */

#if defined(CONFIG_KDEBUGD_MISC) && defined(CONFIG_SCHED_HISTORY)

/* For kdebugd - schedule history logger */
#define QUEUE_LIMIT 500

struct sched_queue {
	int pid;
	int tgid;
	char comm[16];
	unsigned long long sched_clock;
	int cpu;
#ifdef CONFIG_ELF_MODULE
	unsigned int pc;
#endif				/* CONFIG_ELF_MODULE */
};

struct sched_queue *g_pvd_queue_;
static atomic_t  sched_history_init_flag = ATOMIC_INIT(E_NONE);
unsigned long vd_queue_idx;
int vd_queue_is_full;

static DEFINE_SPINLOCK(sched_history_lock);

bool init_sched_history(void)
{

	WARN_ON(g_pvd_queue_);
	g_pvd_queue_ = (struct sched_queue *)KDBG_MEM_DBG_KMALLOC
	    (KDBG_MEM_KDEBUGD_MODULE, QUEUE_LIMIT * sizeof(struct sched_queue),
	     GFP_KERNEL);
	if (!g_pvd_queue_) {
		PRINT_KD
		    ("Cannot initialize schedule history: Insufficient memory\n");
		atomic_set(&sched_history_init_flag, 0);
		return false;
	}
	atomic_set(&sched_history_init_flag, 1);
	return true;

}

void destroy_sched_history(void)
{
	struct sched_queue *aptr = NULL;

	spin_lock(&sched_history_lock);
	if (g_pvd_queue_) {
		/* This spin lock is going to use from scheduler
		 * keep as light as possible */
		/* Assign the pointer and free later from outside spin lock*/
		aptr = g_pvd_queue_;
		g_pvd_queue_ = NULL;
		atomic_set(&sched_history_init_flag, 0);
	}
	spin_unlock(&sched_history_lock);

	if (aptr) {
		KDBG_MEM_DBG_KFREE(aptr);
		PRINT_KD("Sched History Destroyed Successfuly\n");
	} else {
		PRINT_KD("Not Initialized\n");
	}
}

int status_sched_history(void)
{
	if (atomic_read(&sched_history_init_flag))
		PRINT_KD("Initialized        Running\n");
	else
		PRINT_KD("Not Initialized    Not Running\n");

	return 1;
}

void sched_history_OnOff(void)
{
	if (atomic_read(&sched_history_init_flag))
		destroy_sched_history();
	else
		init_sched_history();
}

int show_sched_history(void)
{
	int idx, j;
	int buffer_count = 0;

	if (!atomic_read(&sched_history_init_flag))
		init_sched_history();

	if (atomic_read(&sched_history_init_flag)) {

#ifdef CONFIG_ELF_MODULE
		char *func_name = NULL;
		func_name = (char *)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_KDEBUGD_MODULE, KDBG_ELF_SYM_NAME_LENGTH_MAX,
			 GFP_KERNEL);

		/* Loading ELF Database.... */
		kdbg_elf_load_elf_db_for_all_process();
#endif /* CONFIG_ELF_MODULE */

		/* BUG:flag is on but no memory allocated !! */
		BUG_ON(!g_pvd_queue_);

		if (vd_queue_is_full) {
			buffer_count = QUEUE_LIMIT;
			idx = vd_queue_idx % QUEUE_LIMIT;
		} else {
			buffer_count = vd_queue_idx % QUEUE_LIMIT;
			idx = 0;
		}

		/*preempt_disable(); resolve the crash at the time of IO read */
		PRINT_KD
			("===========================================================\n");
		PRINT_KD("CONTEXT SWITCH HISTORY LOGGER\n");
		PRINT_KD
			("===========================================================\n");
		PRINT_KD("Current time:%llu Context Cnt:%lu\n", sched_clock(),
				vd_queue_idx);
		PRINT_KD
			("===========================================================\n");

		if (buffer_count) {
			/* print */
			for (j = 0; j < buffer_count; ++idx, idx %= QUEUE_LIMIT) {
				PRINT_KD
					(" INFO:%3d:[CPU %d]%-17s(tid:%-4d,tgid:%-4d)t:%llu\n",
					 j++, g_pvd_queue_[idx].cpu,
					 g_pvd_queue_[idx].comm,
					 g_pvd_queue_[idx].pid,
					 g_pvd_queue_[idx].tgid,
					 g_pvd_queue_[idx].sched_clock);
#ifdef CONFIG_ELF_MODULE
				if (func_name) {
					if (!kdbg_elf_get_symbol_by_pid
							(g_pvd_queue_[idx].pid,
							 g_pvd_queue_[idx].pc, func_name)) {
						strncpy(func_name, "???",
								sizeof("???"));
						func_name[sizeof("???") - 1] =
							'\0';
					}
				}
				PRINT_KD(" PC: (0x%x)  %s\n",
						g_pvd_queue_[idx].pc,
						(func_name ? func_name : "???"));
#endif /* CONFIG_ELF_MODULE */
			}
		} else {
			PRINT_KD
				("No Statistics found [may be system is Idle]\n");
		}

#ifdef CONFIG_ELF_MODULE
		if (func_name)
			KDBG_MEM_DBG_KFREE(func_name);
#endif /* CONFIG_ELF_MODULE */

		PRINT_KD("\n");
		vd_queue_idx = 0;
		/* preempt_enable(); Fix the crash at the time of IO read i.e.,from USB*/
	}

	return 1;
}

void schedule_history(struct task_struct *next, int cpu)
{
	static int prev_idx;
	int idx = vd_queue_idx % QUEUE_LIMIT;

	spin_lock(&sched_history_lock);

	/* If not initialize return */
	if (!atomic_read(&sched_history_init_flag)) {
		spin_unlock(&sched_history_lock);
		return;
	}

	if (next->pid == g_pvd_queue_[prev_idx].pid) {
		spin_unlock(&sched_history_lock);
		return;
	}

	/* pid */
	g_pvd_queue_[idx].pid = (int)next->pid;
	g_pvd_queue_[idx].tgid = (int)next->tgid;

	/* comm */
	g_pvd_queue_[idx].comm[0] = '\0';
	get_task_comm(g_pvd_queue_[idx].comm, next);
	g_pvd_queue_[idx].comm[TASK_COMM_LEN - 1] = '\0';

#ifdef CONFIG_ELF_MODULE
	g_pvd_queue_[idx].pc = KSTK_EIP(next);
#endif /* CONFIG_ELF_MODULE */

	/* sched_clock */
	g_pvd_queue_[idx].sched_clock = sched_clock();
	g_pvd_queue_[idx].cpu = cpu;

	spin_unlock(&sched_history_lock);

	prev_idx = idx;
	vd_queue_idx++;

	if (vd_queue_idx == QUEUE_LIMIT) {
		vd_queue_is_full = 1;
	}
}
#endif /* defined(CONFIG_KDEBUGD_MISC) && defined(CONFIG_SCHED_HISTORY) */

/* By Default user print will be OFF */
static atomic_t  kdbg_printf_off = ATOMIC_INIT(1);

/* Function show the status and also toggle the status of printf */
static int kdbg_printf_status(void)
{
	int operation = 0;

	while (1) {
		PRINT_KD("-----------------------------------\n");
		PRINT_KD("Current status is user print %s\n",
				atomic_read(&kdbg_printf_off) ? "DISABLE" : "ENABLE");
		PRINT_KD("-------------------------------------\n");
		PRINT_KD("1.  For toggle status \n");
		PRINT_KD("99. For exit\n");
		PRINT_KD("--------------------------------------\n");
		PRINT_KD("Select Option==>");
		operation = debugd_get_event_as_numeric(NULL, NULL);

		if (operation == 1) {
			atomic_set(&kdbg_printf_off, !atomic_read(&kdbg_printf_off));
			break;
		} else if (operation == 99) {
			break;
		} else {
			PRINT_KD("Invalid Option..\n");
		}
	}

	return 1;

}

int kdbg_print_off()
{
	return 	atomic_read(&kdbg_printf_off);
}

static int __init kdbg_misc_init(void)
{
	int retval = 0;

#ifdef CONFIG_SHOW_TASK_STATE
	kdbg_register("DEBUG: A list of tasks and their relation information",
			show_task_state, NULL, KDBG_MENU_SHOW_TASK_STATE);
#endif

#ifdef CONFIG_SHOW_TASK_PRIORITY
	kdbg_register("DEBUG: A list of tasks and their priority information",
			show_state_prio, NULL, KDBG_MENU_SHOW_TASK_PRIORITY);
#endif

#ifdef CONFIG_TASK_STATE_BACKTRACE
	kdbg_register
		("DEBUG: A list of tasks and their information + backtrace(kernel)",
		 show_task_state_backtrace, NULL, KDBG_MENU_TASK_STATE_BACKTRACE);
#endif

#ifdef CONFIG_TASK_FOR_COREDUMP
	kdbg_register("DEBUG: Kill the task to create coredump",
			kill_task_for_coredump, NULL,
			KDBG_MENU_TASK_FOR_COREDUMP);
#endif

#ifdef CONFIG_VIRTUAL_TO_PHYSICAL
	kdbg_register("DEBUG: Virt(User) to physical ADDR Converter ",
			physical_memory_converter, NULL,
			KDBG_MENU_VIRTUAL_TO_PHYSICAL);
#endif

#ifdef CONFIG_SHOW_USER_THREAD_REGS
	kdbg_register("DEBUG: Dump task register with pid",
			show_user_thread_regs, NULL,
			KDBG_MENU_SHOW_USER_THREAD_REGS);
#endif

#ifdef CONFIG_SHOW_USER_MAPS_WITH_PID
	kdbg_register("DEBUG: Dump task maps with pid", show_user_maps_with_pid,
			NULL, KDBG_MENU_SHOW_USER_MAPS_WITH_PID);
#endif

#ifdef CONFIG_SHOW_USER_STACK_WITH_PID
	kdbg_register("DEBUG: Dump user stack with pid",
			show_user_stack_with_pid, NULL,
			KDBG_MENU_SHOW_USER_STACK_WITH_PID);
#endif

#ifdef CONFIG_KDEBUGD_TRACE
	kdbg_trace_init();
#endif

#ifdef	CONFIG_ELF_MODULE
	kdbg_register("DEBUG: Dump symbol of user stack with pid",
			kdbg_elf_show_symbol_of_user_stack_with_pid, NULL,
			KDBG_MENU_DUMP_SYMBOL_USER_STACK);
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_KDEBUGD_FUTEX
	kdbg_futex_init();
#endif

#ifdef CONFIG_KDEBUGD_FTRACE
	kdbg_ftrace_init();
#endif

#ifndef CONFIG_SMP
#ifdef CONFIG_MEMORY_VALIDATOR
	kdbg_register("TRACE: Memory Value Watcher", memory_validator, NULL,
			KDBG_MENU_MEMORY_VALIDATOR);
#endif
#endif

#ifdef CONFIG_TRACE_THREAD_PC
	kdbg_register("TRACE: Trace thread execution(look at PC)",
			trace_thread_pc, turnoff_trace_thread_pc,
			KDBG_MENU_TRACE_THREAD_PC);
#endif

#ifdef CONFIG_SCHED_HISTORY

#ifdef CONFIG_SCHED_HISTORY_AUTO_START
	init_sched_history();
#endif

	kdbg_register("TRACE: Schedule history logger", show_sched_history,
			NULL, KDBG_MENU_SCHED_HISTORY);
#endif

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
	/* Core should be initialize first .. */
	kdbg_work_queue_init();
	kdbg_register("COUNTER MONITOR: Counter monitor status", kdebugd_status,
			NULL, KDBG_MENU_COUNTER_MONITOR);
	kdbg_cpuusage_init();
	kdbg_topthread_init();
	kdbg_diskusage_init();
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS
	kdbg_perfcounters_init();
#endif
	kdbg_memusage_init();
	kdbg_netusage_init();

#endif /*CONFIG_KDEBUGD_COUNTER_MONITOR */

#ifdef CONFIG_ELF_MODULE
	kdbg_elf_init();
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_ADVANCE_OPROFILE
	aop_kdebug_start();
#endif /* CONFIG_ADVANCE_OPROFILE */

#ifdef KDBG_MEM_DBG
	kdbg_mem_init();
#endif /* KDBG_MEM_DBG */

#ifdef CONFIG_KDEBUGD_LIFE_TEST
	kdbg_key_test_player_init();
#endif

#ifdef CONFIG_KDEBUGD_HUB_DTVLOGD
	kdbg_register("HUB: DTVLOGD_LOG PRINTING", dtvlogd_buffer_printf, NULL, KDBG_MENU_DTVLOGD_LOG);
#endif
	kdbg_register("Dynamically Support Printf", kdbg_printf_status,
			NULL, KDBG_MENU_PRINT_OFF);

	return retval;
}

module_init(kdbg_misc_init);
