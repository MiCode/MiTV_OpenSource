/*
 * linux/fs/binfmt_elf.c
 *
 * These are the functions used to load ELF format executables as used
 * on SVr4 machines.  Information on the format may be found in the book
 * "UNIX SYSTEM V RELEASE 4 Programmers Guide: Ansi C and Programming Support
 * Tools".
 *
 * Copyright 1993, 1994: Eric Youngdale (ericy@cais.com).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/personality.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/elf-randomize.h>
#include <linux/utsname.h>
#include <linux/coredump.h>
#include <linux/sched.h>
#include <linux/dax.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/page.h>
#include <mstar/mpatch_macro.h>

#if (MP_DEBUG_TOOL_COREDUMP == 1)
#include <linux/zlib.h>
#include <linux/zutil.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/crc32.h>
extern unsigned long __pure gzip_crc32_le(unsigned char const *p, unsigned long len);
extern int compress_single;
/* Plan: call deflate() with avail_in == *sourcelen,
   avail_out = *dstlen - 12 and flush == Z_FINISH.
   If it doesn't manage to finish, call it again with
   avail_in == 0 and avail_out set to the remaining 12
   bytes for it to clean up.
   Q: Is 12 bytes sufficient?
*/
#ifndef STREAM_END_SPACE
#define STREAM_END_SPACE 12
#endif

//#define ULTIMATE_COMP_BUF_SIZE  10485760 /* 10MB */
#define ULTIMATE_COMP_BUF_SIZE  3145728 /* 3MB */

#ifndef ELF_CORE_MAX_PAGE_NUM
#define ELF_CORE_MAX_PAGE_NUM   32          /* kmalloc max allocation size 128KB */
#endif

#define DEBUG_BINFMT_ELF_COMP
#ifdef DEBUG_BINFMT_ELF_COMP
/* Ultimage Coredump debug mode */
#define coredump_debug(fmt,arg...) \
	printk(KERN_ALERT fmt,##arg)
#else
#define coredump_debug(fmt,arg...) \
	do { } while (0)
#endif

static DEFINE_MUTEX(deflate_mutex);
static z_stream def_strm;

typedef struct gzip_header {
	unsigned char id[2];
	unsigned char cm;
	unsigned char flag;
	unsigned char mTime[4];
	unsigned char xfl;
	unsigned char os;
} gzip_header_t;


static int write_coredump(struct coredump_params *cprm, unsigned char *src, unsigned char *comp_buf,
        loff_t src_len, unsigned long *crc, u32 *is_z_finish);
static int set_gzip_header(struct coredump_params *cprm);
static int set_gzip_tailer(struct coredump_params *cprm, unsigned long *crc, unsigned long *uncomp_size);
static int compress_coredump(struct coredump_params *cprm, unsigned char *src, unsigned char *comp_buf,
			     loff_t src_len, unsigned long *crc, u32 *is_z_finish);
static int coredump_alloc_workspaces(void);
static void coredump_free_workspaces(void);

#endif /*MP_DEBUG_TOOL_COREDUMP*/

#ifndef user_long_t
#define user_long_t long
#endif
#ifndef user_siginfo_t
#define user_siginfo_t siginfo_t
#endif

static int load_elf_binary(struct linux_binprm *bprm);
static unsigned long elf_map(struct file *, unsigned long, struct elf_phdr *,
				int, int, unsigned long);

#ifdef CONFIG_USELIB
static int load_elf_library(struct file *);
#else
#define load_elf_library NULL
#endif

/*
 * If we don't support core dumping, then supply a NULL so we
 * don't even try.
 */
#ifdef CONFIG_ELF_CORE
static int elf_core_dump(struct coredump_params *cprm);
#else
#define elf_core_dump	NULL
#endif

#if ELF_EXEC_PAGESIZE > PAGE_SIZE
#define ELF_MIN_ALIGN	ELF_EXEC_PAGESIZE
#else
#define ELF_MIN_ALIGN	PAGE_SIZE
#endif

#ifndef ELF_CORE_EFLAGS
#define ELF_CORE_EFLAGS	0
#endif

#define ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(ELF_MIN_ALIGN-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_MIN_ALIGN-1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_MIN_ALIGN - 1) & ~(ELF_MIN_ALIGN - 1))

static struct linux_binfmt elf_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_elf_binary,
	.load_shlib	= load_elf_library,
	.core_dump	= elf_core_dump,
	.min_coredump	= ELF_EXEC_PAGESIZE,
};

#define BAD_ADDR(x) ((unsigned long)(x) >= TASK_SIZE)

static int set_brk(unsigned long start, unsigned long end)
{
	start = ELF_PAGEALIGN(start);
	end = ELF_PAGEALIGN(end);
	if (end > start) {
		int error = vm_brk(start, end - start);
		if (error)
			return error;
	}
	current->mm->start_brk = current->mm->brk = end;
	return 0;
}

/* We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  This would
   contain the junk from the file that should not
   be in memory
 */
static int padzero(unsigned long elf_bss)
{
	unsigned long nbyte;

	nbyte = ELF_PAGEOFFSET(elf_bss);
	if (nbyte) {
		nbyte = ELF_MIN_ALIGN - nbyte;
		if (clear_user((void __user *) elf_bss, nbyte))
			return -EFAULT;
	}
	return 0;
}

/* Let's use some macros to make this stack manipulation a little clearer */
#ifdef CONFIG_STACK_GROWSUP
#define STACK_ADD(sp, items) ((elf_addr_t __user *)(sp) + (items))
#define STACK_ROUND(sp, items) \
	((15 + (unsigned long) ((sp) + (items))) &~ 15UL)
#define STACK_ALLOC(sp, len) ({ \
	elf_addr_t __user *old_sp = (elf_addr_t __user *)sp; sp += len; \
	old_sp; })
#else
#define STACK_ADD(sp, items) ((elf_addr_t __user *)(sp) - (items))
#define STACK_ROUND(sp, items) \
	(((unsigned long) (sp - items)) &~ 15UL)
#define STACK_ALLOC(sp, len) ({ sp -= len ; sp; })
#endif

#ifndef ELF_BASE_PLATFORM
/*
 * AT_BASE_PLATFORM indicates the "real" hardware/microarchitecture.
 * If the arch defines ELF_BASE_PLATFORM (in asm/elf.h), the value
 * will be copied to the user stack in the same manner as AT_PLATFORM.
 */
#define ELF_BASE_PLATFORM NULL
#endif

static int
create_elf_tables(struct linux_binprm *bprm, struct elfhdr *exec,
		unsigned long load_addr, unsigned long interp_load_addr)
{
	unsigned long p = bprm->p;
	int argc = bprm->argc;
	int envc = bprm->envc;
	elf_addr_t __user *argv;
	elf_addr_t __user *envp;
	elf_addr_t __user *sp;
	elf_addr_t __user *u_platform;
	elf_addr_t __user *u_base_platform;
	elf_addr_t __user *u_rand_bytes;
	const char *k_platform = ELF_PLATFORM;
	const char *k_base_platform = ELF_BASE_PLATFORM;
	unsigned char k_rand_bytes[16];
	int items;
	elf_addr_t *elf_info;
	int ei_index = 0;
	const struct cred *cred = current_cred();
	struct vm_area_struct *vma;

	/*
	 * In some cases (e.g. Hyper-Threading), we want to avoid L1
	 * evictions by the processes running on the same package. One
	 * thing we can do is to shuffle the initial stack for them.
	 */

	p = arch_align_stack(p);

	/*
	 * If this architecture has a platform capability string, copy it
	 * to userspace.  In some cases (Sparc), this info is impossible
	 * for userspace to get any other way, in others (i386) it is
	 * merely difficult.
	 */
	u_platform = NULL;
	if (k_platform) {
		size_t len = strlen(k_platform) + 1;

		u_platform = (elf_addr_t __user *)STACK_ALLOC(p, len);
		if (__copy_to_user(u_platform, k_platform, len))
			return -EFAULT;
	}

	/*
	 * If this architecture has a "base" platform capability
	 * string, copy it to userspace.
	 */
	u_base_platform = NULL;
	if (k_base_platform) {
		size_t len = strlen(k_base_platform) + 1;

		u_base_platform = (elf_addr_t __user *)STACK_ALLOC(p, len);
		if (__copy_to_user(u_base_platform, k_base_platform, len))
			return -EFAULT;
	}

	/*
	 * Generate 16 random bytes for userspace PRNG seeding.
	 */
	get_random_bytes(k_rand_bytes, sizeof(k_rand_bytes));
	u_rand_bytes = (elf_addr_t __user *)
		       STACK_ALLOC(p, sizeof(k_rand_bytes));
	if (__copy_to_user(u_rand_bytes, k_rand_bytes, sizeof(k_rand_bytes)))
		return -EFAULT;

	/* Create the ELF interpreter info */
	elf_info = (elf_addr_t *)current->mm->saved_auxv;
	/* update AT_VECTOR_SIZE_BASE if the number of NEW_AUX_ENT() changes */
#define NEW_AUX_ENT(id, val) \
	do { \
		elf_info[ei_index++] = id; \
		elf_info[ei_index++] = val; \
	} while (0)

#ifdef ARCH_DLINFO
	/* 
	 * ARCH_DLINFO must come first so PPC can do its special alignment of
	 * AUXV.
	 * update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT() in
	 * ARCH_DLINFO changes
	 */
	ARCH_DLINFO;
#endif
	NEW_AUX_ENT(AT_HWCAP, ELF_HWCAP);
	NEW_AUX_ENT(AT_PAGESZ, ELF_EXEC_PAGESIZE);
	NEW_AUX_ENT(AT_CLKTCK, CLOCKS_PER_SEC);
	NEW_AUX_ENT(AT_PHDR, load_addr + exec->e_phoff);
	NEW_AUX_ENT(AT_PHENT, sizeof(struct elf_phdr));
	NEW_AUX_ENT(AT_PHNUM, exec->e_phnum);
	NEW_AUX_ENT(AT_BASE, interp_load_addr);
	NEW_AUX_ENT(AT_FLAGS, 0);
	NEW_AUX_ENT(AT_ENTRY, exec->e_entry);
	NEW_AUX_ENT(AT_UID, from_kuid_munged(cred->user_ns, cred->uid));
	NEW_AUX_ENT(AT_EUID, from_kuid_munged(cred->user_ns, cred->euid));
	NEW_AUX_ENT(AT_GID, from_kgid_munged(cred->user_ns, cred->gid));
	NEW_AUX_ENT(AT_EGID, from_kgid_munged(cred->user_ns, cred->egid));
 	NEW_AUX_ENT(AT_SECURE, security_bprm_secureexec(bprm));
	NEW_AUX_ENT(AT_RANDOM, (elf_addr_t)(unsigned long)u_rand_bytes);
#ifdef ELF_HWCAP2
	NEW_AUX_ENT(AT_HWCAP2, ELF_HWCAP2);
#endif
	NEW_AUX_ENT(AT_EXECFN, bprm->exec);
	if (k_platform) {
		NEW_AUX_ENT(AT_PLATFORM,
			    (elf_addr_t)(unsigned long)u_platform);
	}
	if (k_base_platform) {
		NEW_AUX_ENT(AT_BASE_PLATFORM,
			    (elf_addr_t)(unsigned long)u_base_platform);
	}
	if (bprm->interp_flags & BINPRM_FLAGS_EXECFD) {
		NEW_AUX_ENT(AT_EXECFD, bprm->interp_data);
	}
#undef NEW_AUX_ENT
	/* AT_NULL is zero; clear the rest too */
	memset(&elf_info[ei_index], 0,
	       sizeof current->mm->saved_auxv - ei_index * sizeof elf_info[0]);

	/* And advance past the AT_NULL entry.  */
	ei_index += 2;

	sp = STACK_ADD(p, ei_index);

	items = (argc + 1) + (envc + 1) + 1;
	bprm->p = STACK_ROUND(sp, items);

	/* Point sp at the lowest address on the stack */
#ifdef CONFIG_STACK_GROWSUP
	sp = (elf_addr_t __user *)bprm->p - items - ei_index;
	bprm->exec = (unsigned long)sp; /* XXX: PARISC HACK */
#else
	sp = (elf_addr_t __user *)bprm->p;
#endif


	/*
	 * Grow the stack manually; some architectures have a limit on how
	 * far ahead a user-space access may be in order to grow the stack.
	 */
	vma = find_extend_vma(current->mm, bprm->p);
	if (!vma)
		return -EFAULT;

	/* Now, let's put argc (and argv, envp if appropriate) on the stack */
	if (__put_user(argc, sp++))
		return -EFAULT;
	argv = sp;
	envp = argv + argc + 1;

	/* Populate argv and envp */
	p = current->mm->arg_end = current->mm->arg_start;
	while (argc-- > 0) {
		size_t len;
		if (__put_user((elf_addr_t)p, argv++))
			return -EFAULT;
		len = strnlen_user((void __user *)p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (__put_user(0, argv))
		return -EFAULT;
	current->mm->arg_end = current->mm->env_start = p;
	while (envc-- > 0) {
		size_t len;
		if (__put_user((elf_addr_t)p, envp++))
			return -EFAULT;
		len = strnlen_user((void __user *)p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (__put_user(0, envp))
		return -EFAULT;
	current->mm->env_end = p;

	/* Put the elf_info on the stack in the right place.  */
	sp = (elf_addr_t __user *)envp + 1;
	if (copy_to_user(sp, elf_info, ei_index * sizeof(elf_addr_t)))
		return -EFAULT;
	return 0;
}

#ifndef elf_map

static unsigned long elf_map(struct file *filep, unsigned long addr,
		struct elf_phdr *eppnt, int prot, int type,
		unsigned long total_size)
{
	unsigned long map_addr;
	unsigned long size = eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr);
	unsigned long off = eppnt->p_offset - ELF_PAGEOFFSET(eppnt->p_vaddr);
	addr = ELF_PAGESTART(addr);
	size = ELF_PAGEALIGN(size);

	/* mmap() will return -EINVAL if given a zero size, but a
	 * segment with zero filesize is perfectly valid */
	if (!size)
		return addr;

	/*
	* total_size is the size of the ELF (interpreter) image.
	* The _first_ mmap needs to know the full size, otherwise
	* randomization might put this image into an overlapping
	* position with the ELF binary image. (since size < total_size)
	* So we first map the 'big' image - and unmap the remainder at
	* the end. (which unmap is needed for ELF images with holes.)
	*/
	if (total_size) {
		total_size = ELF_PAGEALIGN(total_size);
		map_addr = vm_mmap(filep, addr, total_size, prot, type, off);
		if (!BAD_ADDR(map_addr))
			vm_munmap(map_addr+size, total_size-size);
	} else
		map_addr = vm_mmap(filep, addr, size, prot, type, off);

	return(map_addr);
}

#endif /* !elf_map */

static unsigned long total_mapping_size(struct elf_phdr *cmds, int nr)
{
	int i, first_idx = -1, last_idx = -1;

	for (i = 0; i < nr; i++) {
		if (cmds[i].p_type == PT_LOAD) {
			last_idx = i;
			if (first_idx == -1)
				first_idx = i;
		}
	}
	if (first_idx == -1)
		return 0;

	return cmds[last_idx].p_vaddr + cmds[last_idx].p_memsz -
				ELF_PAGESTART(cmds[first_idx].p_vaddr);
}

/**
 * load_elf_phdrs() - load ELF program headers
 * @elf_ex:   ELF header of the binary whose program headers should be loaded
 * @elf_file: the opened ELF binary file
 *
 * Loads ELF program headers from the binary file elf_file, which has the ELF
 * header pointed to by elf_ex, into a newly allocated array. The caller is
 * responsible for freeing the allocated data. Returns an ERR_PTR upon failure.
 */
static struct elf_phdr *load_elf_phdrs(struct elfhdr *elf_ex,
				       struct file *elf_file)
{
	struct elf_phdr *elf_phdata = NULL;
	int retval, size, err = -1;

	/*
	 * If the size of this structure has changed, then punt, since
	 * we will be doing the wrong thing.
	 */
	if (elf_ex->e_phentsize != sizeof(struct elf_phdr))
		goto out;

	/* Sanity check the number of program headers... */
	if (elf_ex->e_phnum < 1 ||
		elf_ex->e_phnum > 65536U / sizeof(struct elf_phdr))
		goto out;

	/* ...and their total size. */
	size = sizeof(struct elf_phdr) * elf_ex->e_phnum;
	if (size > ELF_MIN_ALIGN)
		goto out;

	elf_phdata = kmalloc(size, GFP_KERNEL);
	if (!elf_phdata)
		goto out;

	/* Read in the program headers */
	retval = kernel_read(elf_file, elf_ex->e_phoff,
			     (char *)elf_phdata, size);
	if (retval != size) {
		err = (retval < 0) ? retval : -EIO;
		goto out;
	}

	/* Success! */
	err = 0;
out:
	if (err) {
		kfree(elf_phdata);
		elf_phdata = NULL;
	}
	return elf_phdata;
}

#ifndef CONFIG_ARCH_BINFMT_ELF_STATE

/**
 * struct arch_elf_state - arch-specific ELF loading state
 *
 * This structure is used to preserve architecture specific data during
 * the loading of an ELF file, throughout the checking of architecture
 * specific ELF headers & through to the point where the ELF load is
 * known to be proceeding (ie. SET_PERSONALITY).
 *
 * This implementation is a dummy for architectures which require no
 * specific state.
 */
struct arch_elf_state {
};

#define INIT_ARCH_ELF_STATE {}

/**
 * arch_elf_pt_proc() - check a PT_LOPROC..PT_HIPROC ELF program header
 * @ehdr:	The main ELF header
 * @phdr:	The program header to check
 * @elf:	The open ELF file
 * @is_interp:	True if the phdr is from the interpreter of the ELF being
 *		loaded, else false.
 * @state:	Architecture-specific state preserved throughout the process
 *		of loading the ELF.
 *
 * Inspects the program header phdr to validate its correctness and/or
 * suitability for the system. Called once per ELF program header in the
 * range PT_LOPROC to PT_HIPROC, for both the ELF being loaded and its
 * interpreter.
 *
 * Return: Zero to proceed with the ELF load, non-zero to fail the ELF load
 *         with that return code.
 */
static inline int arch_elf_pt_proc(struct elfhdr *ehdr,
				   struct elf_phdr *phdr,
				   struct file *elf, bool is_interp,
				   struct arch_elf_state *state)
{
	/* Dummy implementation, always proceed */
	return 0;
}

/**
 * arch_check_elf() - check an ELF executable
 * @ehdr:	The main ELF header
 * @has_interp:	True if the ELF has an interpreter, else false.
 * @interp_ehdr: The interpreter's ELF header
 * @state:	Architecture-specific state preserved throughout the process
 *		of loading the ELF.
 *
 * Provides a final opportunity for architecture code to reject the loading
 * of the ELF & cause an exec syscall to return an error. This is called after
 * all program headers to be checked by arch_elf_pt_proc have been.
 *
 * Return: Zero to proceed with the ELF load, non-zero to fail the ELF load
 *         with that return code.
 */
static inline int arch_check_elf(struct elfhdr *ehdr, bool has_interp,
				 struct elfhdr *interp_ehdr,
				 struct arch_elf_state *state)
{
	/* Dummy implementation, always proceed */
	return 0;
}

#endif /* !CONFIG_ARCH_BINFMT_ELF_STATE */

/* This is much more generalized than the library routine read function,
   so we keep this separate.  Technically the library read function
   is only provided so that we can read a.out libraries that have
   an ELF header */

static unsigned long load_elf_interp(struct elfhdr *interp_elf_ex,
		struct file *interpreter, unsigned long *interp_map_addr,
		unsigned long no_base, struct elf_phdr *interp_elf_phdata)
{
	struct elf_phdr *eppnt;
	unsigned long load_addr = 0;
	int load_addr_set = 0;
	unsigned long last_bss = 0, elf_bss = 0;
	unsigned long error = ~0UL;
	unsigned long total_size;
	int i;

	/* First of all, some simple consistency checks */
	if (interp_elf_ex->e_type != ET_EXEC &&
	    interp_elf_ex->e_type != ET_DYN)
		goto out;
	if (!elf_check_arch(interp_elf_ex))
		goto out;
	if (!interpreter->f_op->mmap)
		goto out;

	total_size = total_mapping_size(interp_elf_phdata,
					interp_elf_ex->e_phnum);
	if (!total_size) {
		error = -EINVAL;
		goto out;
	}

	eppnt = interp_elf_phdata;
	for (i = 0; i < interp_elf_ex->e_phnum; i++, eppnt++) {
		if (eppnt->p_type == PT_LOAD) {
			int elf_type = MAP_PRIVATE | MAP_DENYWRITE;
			int elf_prot = 0;
			unsigned long vaddr = 0;
			unsigned long k, map_addr;

			if (eppnt->p_flags & PF_R)
		    		elf_prot = PROT_READ;
			if (eppnt->p_flags & PF_W)
				elf_prot |= PROT_WRITE;
			if (eppnt->p_flags & PF_X)
				elf_prot |= PROT_EXEC;
			vaddr = eppnt->p_vaddr;
			if (interp_elf_ex->e_type == ET_EXEC || load_addr_set)
				elf_type |= MAP_FIXED;
			else if (no_base && interp_elf_ex->e_type == ET_DYN)
				load_addr = -vaddr;

			map_addr = elf_map(interpreter, load_addr + vaddr,
					eppnt, elf_prot, elf_type, total_size);
			total_size = 0;
			if (!*interp_map_addr)
				*interp_map_addr = map_addr;
			error = map_addr;
			if (BAD_ADDR(map_addr))
				goto out;

			if (!load_addr_set &&
			    interp_elf_ex->e_type == ET_DYN) {
				load_addr = map_addr - ELF_PAGESTART(vaddr);
				load_addr_set = 1;
			}

			/*
			 * Check to see if the section's size will overflow the
			 * allowed task size. Note that p_filesz must always be
			 * <= p_memsize so it's only necessary to check p_memsz.
			 */
			k = load_addr + eppnt->p_vaddr;
			if (BAD_ADDR(k) ||
			    eppnt->p_filesz > eppnt->p_memsz ||
			    eppnt->p_memsz > TASK_SIZE ||
			    TASK_SIZE - eppnt->p_memsz < k) {
				error = -ENOMEM;
				goto out;
			}

			/*
			 * Find the end of the file mapping for this phdr, and
			 * keep track of the largest address we see for this.
			 */
			k = load_addr + eppnt->p_vaddr + eppnt->p_filesz;
			if (k > elf_bss)
				elf_bss = k;

			/*
			 * Do the same thing for the memory mapping - between
			 * elf_bss and last_bss is the bss section.
			 */
			k = load_addr + eppnt->p_vaddr + eppnt->p_memsz;
			if (k > last_bss)
				last_bss = k;
		}
	}

	/*
	 * Now fill out the bss section: first pad the last page from
	 * the file up to the page boundary, and zero it from elf_bss
	 * up to the end of the page.
	 */
	if (padzero(elf_bss)) {
		error = -EFAULT;
		goto out;
	}
	/*
	 * Next, align both the file and mem bss up to the page size,
	 * since this is where elf_bss was just zeroed up to, and where
	 * last_bss will end after the vm_brk() below.
	 */
	elf_bss = ELF_PAGEALIGN(elf_bss);
	last_bss = ELF_PAGEALIGN(last_bss);
	/* Finally, if there is still more bss to allocate, do it. */
	if (last_bss > elf_bss) {
		error = vm_brk(elf_bss, last_bss - elf_bss);
		if (error)
			goto out;
	}

	error = load_addr;
out:
	return error;
}

/*
 * These are the functions used to load ELF style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

#ifndef STACK_RND_MASK
#define STACK_RND_MASK (0x7ff >> (PAGE_SHIFT - 12))	/* 8MB of VA */
#endif

static unsigned long randomize_stack_top(unsigned long stack_top)
{
	unsigned long random_variable = 0;

	if ((current->flags & PF_RANDOMIZE) &&
		!(current->personality & ADDR_NO_RANDOMIZE)) {
		random_variable = get_random_long();
		random_variable &= STACK_RND_MASK;
		random_variable <<= PAGE_SHIFT;
	}
#ifdef CONFIG_STACK_GROWSUP
	return PAGE_ALIGN(stack_top) + random_variable;
#else
	return PAGE_ALIGN(stack_top) - random_variable;
#endif
}

static int load_elf_binary(struct linux_binprm *bprm)
{
	struct file *interpreter = NULL; /* to shut gcc up */
 	unsigned long load_addr = 0, load_bias = 0;
	int load_addr_set = 0;
	char * elf_interpreter = NULL;
	unsigned long error;
	struct elf_phdr *elf_ppnt, *elf_phdata, *interp_elf_phdata = NULL;
	unsigned long elf_bss, elf_brk;
	int retval, i;
	unsigned long elf_entry;
	unsigned long interp_load_addr = 0;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long reloc_func_desc __maybe_unused = 0;
	int executable_stack = EXSTACK_DEFAULT;
	struct pt_regs *regs = current_pt_regs();
	struct {
		struct elfhdr elf_ex;
		struct elfhdr interp_elf_ex;
	} *loc;
	struct arch_elf_state arch_state = INIT_ARCH_ELF_STATE;

	loc = kmalloc(sizeof(*loc), GFP_KERNEL);
	if (!loc) {
		retval = -ENOMEM;
		goto out_ret;
	}

	/* Get the exec-header */
	loc->elf_ex = *((struct elfhdr *)bprm->buf);

	retval = -ENOEXEC;
	/* First of all, some simple consistency checks */
	if (memcmp(loc->elf_ex.e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	if (loc->elf_ex.e_type != ET_EXEC && loc->elf_ex.e_type != ET_DYN)
		goto out;
	if (!elf_check_arch(&loc->elf_ex))
		goto out;
	if (!bprm->file->f_op->mmap)
		goto out;

	elf_phdata = load_elf_phdrs(&loc->elf_ex, bprm->file);
	if (!elf_phdata)
		goto out;

	elf_ppnt = elf_phdata;
	elf_bss = 0;
	elf_brk = 0;

	start_code = ~0UL;
	end_code = 0;
	start_data = 0;
	end_data = 0;

	for (i = 0; i < loc->elf_ex.e_phnum; i++) {
		if (elf_ppnt->p_type == PT_INTERP) {
			/* This is the program interpreter used for
			 * shared libraries - for now assume that this
			 * is an a.out format binary
			 */
			retval = -ENOEXEC;
			if (elf_ppnt->p_filesz > PATH_MAX || 
			    elf_ppnt->p_filesz < 2)
				goto out_free_ph;

			retval = -ENOMEM;
			elf_interpreter = kmalloc(elf_ppnt->p_filesz,
						  GFP_KERNEL);
			if (!elf_interpreter)
				goto out_free_ph;

			retval = kernel_read(bprm->file, elf_ppnt->p_offset,
					     elf_interpreter,
					     elf_ppnt->p_filesz);
			if (retval != elf_ppnt->p_filesz) {
				if (retval >= 0)
					retval = -EIO;
				goto out_free_interp;
			}
			/* make sure path is NULL terminated */
			retval = -ENOEXEC;
			if (elf_interpreter[elf_ppnt->p_filesz - 1] != '\0')
				goto out_free_interp;

			interpreter = open_exec(elf_interpreter);
			retval = PTR_ERR(interpreter);
			if (IS_ERR(interpreter))
				goto out_free_interp;

			/*
			 * If the binary is not readable then enforce
			 * mm->dumpable = 0 regardless of the interpreter's
			 * permissions.
			 */
			would_dump(bprm, interpreter);

			/* Get the exec headers */
			retval = kernel_read(interpreter, 0,
					     (void *)&loc->interp_elf_ex,
					     sizeof(loc->interp_elf_ex));
			if (retval != sizeof(loc->interp_elf_ex)) {
				if (retval >= 0)
					retval = -EIO;
				goto out_free_dentry;
			}

			break;
		}
		elf_ppnt++;
	}

	elf_ppnt = elf_phdata;
	for (i = 0; i < loc->elf_ex.e_phnum; i++, elf_ppnt++)
		switch (elf_ppnt->p_type) {
		case PT_GNU_STACK:
			if (elf_ppnt->p_flags & PF_X)
				executable_stack = EXSTACK_ENABLE_X;
			else
				executable_stack = EXSTACK_DISABLE_X;
			break;

		case PT_LOPROC ... PT_HIPROC:
			retval = arch_elf_pt_proc(&loc->elf_ex, elf_ppnt,
						  bprm->file, false,
						  &arch_state);
			if (retval)
				goto out_free_dentry;
			break;
		}

	/* Some simple consistency checks for the interpreter */
	if (elf_interpreter) {
		retval = -ELIBBAD;
		/* Not an ELF interpreter */
		if (memcmp(loc->interp_elf_ex.e_ident, ELFMAG, SELFMAG) != 0)
			goto out_free_dentry;
		/* Verify the interpreter has a valid arch */
		if (!elf_check_arch(&loc->interp_elf_ex))
			goto out_free_dentry;

		/* Load the interpreter program headers */
		interp_elf_phdata = load_elf_phdrs(&loc->interp_elf_ex,
						   interpreter);
		if (!interp_elf_phdata)
			goto out_free_dentry;

		/* Pass PT_LOPROC..PT_HIPROC headers to arch code */
		elf_ppnt = interp_elf_phdata;
		for (i = 0; i < loc->interp_elf_ex.e_phnum; i++, elf_ppnt++)
			switch (elf_ppnt->p_type) {
			case PT_LOPROC ... PT_HIPROC:
				retval = arch_elf_pt_proc(&loc->interp_elf_ex,
							  elf_ppnt, interpreter,
							  true, &arch_state);
				if (retval)
					goto out_free_dentry;
				break;
			}
	}

	/*
	 * Allow arch code to reject the ELF at this point, whilst it's
	 * still possible to return an error to the code that invoked
	 * the exec syscall.
	 */
	retval = arch_check_elf(&loc->elf_ex,
				!!interpreter, &loc->interp_elf_ex,
				&arch_state);
	if (retval)
		goto out_free_dentry;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		goto out_free_dentry;

	/* Do this immediately, since STACK_TOP as used in setup_arg_pages
	   may depend on the personality.  */
	SET_PERSONALITY2(loc->elf_ex, &arch_state);
	if (elf_read_implies_exec(loc->elf_ex, executable_stack))
		current->personality |= READ_IMPLIES_EXEC;

	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		current->flags |= PF_RANDOMIZE;

	setup_new_exec(bprm);
	install_exec_creds(bprm);

	/* Do this so that we can load the interpreter, if need be.  We will
	   change some of these later */
	retval = setup_arg_pages(bprm, randomize_stack_top(STACK_TOP),
				 executable_stack);
	if (retval < 0)
		goto out_free_dentry;

	current->mm->start_stack = bprm->p;

	/* Now we do a little grungy work by mmapping the ELF image into
	   the correct location in memory. */
	for(i = 0, elf_ppnt = elf_phdata;
	    i < loc->elf_ex.e_phnum; i++, elf_ppnt++) {
		int elf_prot = 0, elf_flags;
		unsigned long k, vaddr;
		unsigned long total_size = 0;

		if (elf_ppnt->p_type != PT_LOAD)
			continue;

		if (unlikely (elf_brk > elf_bss)) {
			unsigned long nbyte;
	            
			/* There was a PT_LOAD segment with p_memsz > p_filesz
			   before this one. Map anonymous pages, if needed,
			   and clear the area.  */
			retval = set_brk(elf_bss + load_bias,
					 elf_brk + load_bias);
			if (retval)
				goto out_free_dentry;
			nbyte = ELF_PAGEOFFSET(elf_bss);
			if (nbyte) {
				nbyte = ELF_MIN_ALIGN - nbyte;
				if (nbyte > elf_brk - elf_bss)
					nbyte = elf_brk - elf_bss;
				if (clear_user((void __user *)elf_bss +
							load_bias, nbyte)) {
					/*
					 * This bss-zeroing can fail if the ELF
					 * file specifies odd protections. So
					 * we don't check the return value
					 */
				}
			}
		}

		if (elf_ppnt->p_flags & PF_R)
			elf_prot |= PROT_READ;
		if (elf_ppnt->p_flags & PF_W)
			elf_prot |= PROT_WRITE;
		if (elf_ppnt->p_flags & PF_X)
			elf_prot |= PROT_EXEC;

		elf_flags = MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE;

		vaddr = elf_ppnt->p_vaddr;
		/*
		 * If we are loading ET_EXEC or we have already performed
		 * the ET_DYN load_addr calculations, proceed normally.
		 */
		if (loc->elf_ex.e_type == ET_EXEC || load_addr_set) {
			elf_flags |= MAP_FIXED;
		} else if (loc->elf_ex.e_type == ET_DYN) {
			/*
			 * This logic is run once for the first LOAD Program
			 * Header for ET_DYN binaries to calculate the
			 * randomization (load_bias) for all the LOAD
			 * Program Headers, and to calculate the entire
			 * size of the ELF mapping (total_size). (Note that
			 * load_addr_set is set to true later once the
			 * initial mapping is performed.)
			 *
			 * There are effectively two types of ET_DYN
			 * binaries: programs (i.e. PIE: ET_DYN with INTERP)
			 * and loaders (ET_DYN without INTERP, since they
			 * _are_ the ELF interpreter). The loaders must
			 * be loaded away from programs since the program
			 * may otherwise collide with the loader (especially
			 * for ET_EXEC which does not have a randomized
			 * position). For example to handle invocations of
			 * "./ld.so someprog" to test out a new version of
			 * the loader, the subsequent program that the
			 * loader loads must avoid the loader itself, so
			 * they cannot share the same load range. Sufficient
			 * room for the brk must be allocated with the
			 * loader as well, since brk must be available with
			 * the loader.
			 *
			 * Therefore, programs are loaded offset from
			 * ELF_ET_DYN_BASE and loaders are loaded into the
			 * independently randomized mmap region (0 load_bias
			 * without MAP_FIXED).
			 */
			if (elf_interpreter) {
				load_bias = ELF_ET_DYN_BASE;
				if (current->flags & PF_RANDOMIZE)
					load_bias += arch_mmap_rnd();
				elf_flags |= MAP_FIXED;
			} else
				load_bias = 0;

			/*
			 * Since load_bias is used for all subsequent loading
			 * calculations, we must lower it by the first vaddr
			 * so that the remaining calculations based on the
			 * ELF vaddrs will be correctly offset. The result
			 * is then page aligned.
			 */
			load_bias = ELF_PAGESTART(load_bias - vaddr);

			total_size = total_mapping_size(elf_phdata,
							loc->elf_ex.e_phnum);
			if (!total_size) {
				retval = -EINVAL;
				goto out_free_dentry;
			}
		}

		error = elf_map(bprm->file, load_bias + vaddr, elf_ppnt,
				elf_prot, elf_flags, total_size);
		if (BAD_ADDR(error)) {
			retval = IS_ERR((void *)error) ?
				PTR_ERR((void*)error) : -EINVAL;
			goto out_free_dentry;
		}

		if (!load_addr_set) {
			load_addr_set = 1;
			load_addr = (elf_ppnt->p_vaddr - elf_ppnt->p_offset);
			if (loc->elf_ex.e_type == ET_DYN) {
				load_bias += error -
				             ELF_PAGESTART(load_bias + vaddr);
				load_addr += load_bias;
				reloc_func_desc = load_bias;
			}
		}
		k = elf_ppnt->p_vaddr;
		if (k < start_code)
			start_code = k;
		if (start_data < k)
			start_data = k;

		/*
		 * Check to see if the section's size will overflow the
		 * allowed task size. Note that p_filesz must always be
		 * <= p_memsz so it is only necessary to check p_memsz.
		 */
		if (BAD_ADDR(k) || elf_ppnt->p_filesz > elf_ppnt->p_memsz ||
		    elf_ppnt->p_memsz > TASK_SIZE ||
		    TASK_SIZE - elf_ppnt->p_memsz < k) {
			/* set_brk can never work. Avoid overflows. */
			retval = -EINVAL;
			goto out_free_dentry;
		}

		k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;

		if (k > elf_bss)
			elf_bss = k;
		if ((elf_ppnt->p_flags & PF_X) && end_code < k)
			end_code = k;
		if (end_data < k)
			end_data = k;
		k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
		if (k > elf_brk)
			elf_brk = k;
	}

	loc->elf_ex.e_entry += load_bias;
	elf_bss += load_bias;
	elf_brk += load_bias;
	start_code += load_bias;
	end_code += load_bias;
	start_data += load_bias;
	end_data += load_bias;

	/* Calling set_brk effectively mmaps the pages that we need
	 * for the bss and break sections.  We must do this before
	 * mapping in the interpreter, to make sure it doesn't wind
	 * up getting placed where the bss needs to go.
	 */
	retval = set_brk(elf_bss, elf_brk);
	if (retval)
		goto out_free_dentry;
	if (likely(elf_bss != elf_brk) && unlikely(padzero(elf_bss))) {
		retval = -EFAULT; /* Nobody gets to see this, but.. */
		goto out_free_dentry;
	}

	if (elf_interpreter) {
		unsigned long interp_map_addr = 0;

		elf_entry = load_elf_interp(&loc->interp_elf_ex,
					    interpreter,
					    &interp_map_addr,
					    load_bias, interp_elf_phdata);
		if (!IS_ERR((void *)elf_entry)) {
			/*
			 * load_elf_interp() returns relocation
			 * adjustment
			 */
			interp_load_addr = elf_entry;
			elf_entry += loc->interp_elf_ex.e_entry;
		}
		if (BAD_ADDR(elf_entry)) {
			retval = IS_ERR((void *)elf_entry) ?
					(int)elf_entry : -EINVAL;
			goto out_free_dentry;
		}
		reloc_func_desc = interp_load_addr;

		allow_write_access(interpreter);
		fput(interpreter);
		kfree(elf_interpreter);
	} else {
		elf_entry = loc->elf_ex.e_entry;
		if (BAD_ADDR(elf_entry)) {
			retval = -EINVAL;
			goto out_free_dentry;
		}
	}

	kfree(interp_elf_phdata);
	kfree(elf_phdata);

	set_binfmt(&elf_format);

#ifdef ARCH_HAS_SETUP_ADDITIONAL_PAGES
	retval = arch_setup_additional_pages(bprm, !!elf_interpreter);
	if (retval < 0)
		goto out;
#endif /* ARCH_HAS_SETUP_ADDITIONAL_PAGES */

	retval = create_elf_tables(bprm, &loc->elf_ex,
			  load_addr, interp_load_addr);
	if (retval < 0)
		goto out;
	/* N.B. passed_fileno might not be initialized? */
	current->mm->end_code = end_code;
	current->mm->start_code = start_code;
	current->mm->start_data = start_data;
	current->mm->end_data = end_data;
	current->mm->start_stack = bprm->p;

	if ((current->flags & PF_RANDOMIZE) && (randomize_va_space > 1)) {
		current->mm->brk = current->mm->start_brk =
			arch_randomize_brk(current->mm);
#ifdef compat_brk_randomized
		current->brk_randomized = 1;
#endif
	}

	if (current->personality & MMAP_PAGE_ZERO) {
		/* Why this, you ask???  Well SVr4 maps page 0 as read-only,
		   and some applications "depend" upon this behavior.
		   Since we do not have the power to recompile these, we
		   emulate the SVr4 behavior. Sigh. */
		error = vm_mmap(NULL, 0, PAGE_SIZE, PROT_READ | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE, 0);
	}

#ifdef ELF_PLAT_INIT
	/*
	 * The ABI may specify that certain registers be set up in special
	 * ways (on i386 %edx is the address of a DT_FINI function, for
	 * example.  In addition, it may also specify (eg, PowerPC64 ELF)
	 * that the e_entry field is the address of the function descriptor
	 * for the startup routine, rather than the address of the startup
	 * routine itself.  This macro performs whatever initialization to
	 * the regs structure is required as well as any relocations to the
	 * function descriptor entries when executing dynamically links apps.
	 */
	ELF_PLAT_INIT(regs, reloc_func_desc);
#endif

	start_thread(regs, elf_entry, bprm->p);
	retval = 0;
out:
	kfree(loc);
out_ret:
	return retval;

	/* error cleanup */
out_free_dentry:
	kfree(interp_elf_phdata);
	allow_write_access(interpreter);
	if (interpreter)
		fput(interpreter);
out_free_interp:
	kfree(elf_interpreter);
out_free_ph:
	kfree(elf_phdata);
	goto out;
}

#ifdef CONFIG_USELIB
/* This is really simpleminded and specialized - we are loading an
   a.out library that is given an ELF header. */
static int load_elf_library(struct file *file)
{
	struct elf_phdr *elf_phdata;
	struct elf_phdr *eppnt;
	unsigned long elf_bss, bss, len;
	int retval, error, i, j;
	struct elfhdr elf_ex;

	error = -ENOEXEC;
	retval = kernel_read(file, 0, (char *)&elf_ex, sizeof(elf_ex));
	if (retval != sizeof(elf_ex))
		goto out;

	if (memcmp(elf_ex.e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	/* First of all, some simple consistency checks */
	if (elf_ex.e_type != ET_EXEC || elf_ex.e_phnum > 2 ||
	    !elf_check_arch(&elf_ex) || !file->f_op->mmap)
		goto out;

	/* Now read in all of the header information */

	j = sizeof(struct elf_phdr) * elf_ex.e_phnum;
	/* j < ELF_MIN_ALIGN because elf_ex.e_phnum <= 2 */

	error = -ENOMEM;
	elf_phdata = kmalloc(j, GFP_KERNEL);
	if (!elf_phdata)
		goto out;

	eppnt = elf_phdata;
	error = -ENOEXEC;
	retval = kernel_read(file, elf_ex.e_phoff, (char *)eppnt, j);
	if (retval != j)
		goto out_free_ph;

	for (j = 0, i = 0; i<elf_ex.e_phnum; i++)
		if ((eppnt + i)->p_type == PT_LOAD)
			j++;
	if (j != 1)
		goto out_free_ph;

	while (eppnt->p_type != PT_LOAD)
		eppnt++;

	/* Now use mmap to map the library into memory. */
	error = vm_mmap(file,
			ELF_PAGESTART(eppnt->p_vaddr),
			(eppnt->p_filesz +
			 ELF_PAGEOFFSET(eppnt->p_vaddr)),
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			(eppnt->p_offset -
			 ELF_PAGEOFFSET(eppnt->p_vaddr)));
	if (error != ELF_PAGESTART(eppnt->p_vaddr))
		goto out_free_ph;

	elf_bss = eppnt->p_vaddr + eppnt->p_filesz;
	if (padzero(elf_bss)) {
		error = -EFAULT;
		goto out_free_ph;
	}

	len = ELF_PAGEALIGN(eppnt->p_filesz + eppnt->p_vaddr);
	bss = ELF_PAGEALIGN(eppnt->p_memsz + eppnt->p_vaddr);
	if (bss > len) {
		error = vm_brk(len, bss - len);
		if (error)
			goto out_free_ph;
	}
	error = 0;

out_free_ph:
	kfree(elf_phdata);
out:
	return error;
}
#endif /* #ifdef CONFIG_USELIB */

#ifdef CONFIG_ELF_CORE
/*
 * ELF core dumper
 *
 * Modelled on fs/exec.c:aout_core_dump()
 * Jeremy Fitzhardinge <jeremy@sw.oz.au>
 */

/*
 * The purpose of always_dump_vma() is to make sure that special kernel mappings
 * that are useful for post-mortem analysis are included in every core dump.
 * In that way we ensure that the core dump is fully interpretable later
 * without matching up the same kernel and hardware config to see what PC values
 * meant. These special mappings include - vDSO, vsyscall, and other
 * architecture specific mappings
 */
static bool always_dump_vma(struct vm_area_struct *vma)
{
	/* Any vsyscall mappings? */
	if (vma == get_gate_vma(vma->vm_mm))
		return true;

	/*
	 * Assume that all vmas with a .name op should always be dumped.
	 * If this changes, a new vm_ops field can easily be added.
	 */
	if (vma->vm_ops && vma->vm_ops->name && vma->vm_ops->name(vma))
		return true;

	/*
	 * arch_vma_name() returns non-NULL for special architecture mappings,
	 * such as vDSO sections.
	 */
	if (arch_vma_name(vma))
		return true;

	return false;
}

/*
 * Decide what to dump of a segment, part, all or none.
 */
static unsigned long vma_dump_size(struct vm_area_struct *vma,
				   unsigned long mm_flags)
{
#define FILTER(type)	(mm_flags & (1UL << MMF_DUMP_##type))

	/* always dump the vdso and vsyscall sections */
	if (always_dump_vma(vma))
		goto whole;

	if (vma->vm_flags & VM_DONTDUMP)
		return 0;

	/* support for DAX */
	if (vma_is_dax(vma)) {
		if ((vma->vm_flags & VM_SHARED) && FILTER(DAX_SHARED))
			goto whole;
		if (!(vma->vm_flags & VM_SHARED) && FILTER(DAX_PRIVATE))
			goto whole;
		return 0;
	}

	/* Hugetlb memory check */
	if (vma->vm_flags & VM_HUGETLB) {
		if ((vma->vm_flags & VM_SHARED) && FILTER(HUGETLB_SHARED))
			goto whole;
		if (!(vma->vm_flags & VM_SHARED) && FILTER(HUGETLB_PRIVATE))
			goto whole;
		return 0;
	}

	/* Do not dump I/O mapped devices or special mappings */
	if (vma->vm_flags & VM_IO)
		return 0;

	/* By default, dump shared memory if mapped from an anonymous file. */
	if (vma->vm_flags & VM_SHARED) {
		if (file_inode(vma->vm_file)->i_nlink == 0 ?
		    FILTER(ANON_SHARED) : FILTER(MAPPED_SHARED))
			goto whole;
		return 0;
	}

	/* Dump segments that have been written to.  */
	if (vma->anon_vma && FILTER(ANON_PRIVATE))
		goto whole;
	if (vma->vm_file == NULL)
		return 0;

	if (FILTER(MAPPED_PRIVATE))
		goto whole;

	/*
	 * If this looks like the beginning of a DSO or executable mapping,
	 * check for an ELF header.  If we find one, dump the first page to
	 * aid in determining what was mapped here.
	 */
	if (FILTER(ELF_HEADERS) &&
	    vma->vm_pgoff == 0 && (vma->vm_flags & VM_READ)) {
		u32 __user *header = (u32 __user *) vma->vm_start;
		u32 word;
		mm_segment_t fs = get_fs();
		/*
		 * Doing it this way gets the constant folded by GCC.
		 */
		union {
			u32 cmp;
			char elfmag[SELFMAG];
		} magic;
		BUILD_BUG_ON(SELFMAG != sizeof word);
		magic.elfmag[EI_MAG0] = ELFMAG0;
		magic.elfmag[EI_MAG1] = ELFMAG1;
		magic.elfmag[EI_MAG2] = ELFMAG2;
		magic.elfmag[EI_MAG3] = ELFMAG3;
		/*
		 * Switch to the user "segment" for get_user(),
		 * then put back what elf_core_dump() had in place.
		 */
		set_fs(USER_DS);
		if (unlikely(get_user(word, header)))
			word = 0;
		set_fs(fs);
		if (word == magic.cmp)
			return PAGE_SIZE;
	}

#undef	FILTER

	return 0;

whole:
	return vma->vm_end - vma->vm_start;
}

/* An ELF note in memory */
struct memelfnote
{
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += roundup(strlen(en->name) + 1, 4);
	sz += roundup(en->datasz, 4);

	return sz;
}
#if (MP_DEBUG_TOOL_COREDUMP == 1)
static unsigned long memcpy_note(struct memelfnote *men, unsigned char *elf_file_offset)
{
	unsigned long note_offset_sz = 0;
	unsigned char *src_offset = elf_file_offset;

	struct elf_note en;

	if(men->name) {
		en.n_namesz = strlen(men->name) + 1;
	} else {
		/* NULL string */
		men->name = "";
		en.n_namesz = 1;
	}

	en.n_descsz = men->datasz;
	en.n_type = men->type;

	memcpy(elf_file_offset, &en, sizeof(en));
	elf_file_offset += sizeof(en);

	memcpy(elf_file_offset, men->name, roundup(en.n_namesz, 4));    /* align name sz */
	elf_file_offset += roundup(en.n_namesz, 4);

	memcpy(elf_file_offset, men->data, roundup(men->datasz, 4));    /* align name sz */
	elf_file_offset += roundup(men->datasz, 4);

	note_offset_sz = elf_file_offset - src_offset;

	return note_offset_sz;
}
#endif /*MP_DEBUG_TOOL_COREDUMP*/

static int writenote(struct memelfnote *men, struct coredump_params *cprm)
{
	struct elf_note en;
	en.n_namesz = strlen(men->name) + 1;
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	return dump_emit(cprm, &en, sizeof(en)) &&
	    dump_emit(cprm, men->name, en.n_namesz) && dump_align(cprm, 4) &&
	    dump_emit(cprm, men->data, men->datasz) && dump_align(cprm, 4);
}

static void fill_elf_header(struct elfhdr *elf, int segs,
			    u16 machine, u32 flags)
{
	memset(elf, 0, sizeof(*elf));

	memcpy(elf->e_ident, ELFMAG, SELFMAG);
	elf->e_ident[EI_CLASS] = ELF_CLASS;
	elf->e_ident[EI_DATA] = ELF_DATA;
	elf->e_ident[EI_VERSION] = EV_CURRENT;
	elf->e_ident[EI_OSABI] = ELF_OSABI;

	elf->e_type = ET_CORE;
	elf->e_machine = machine;
	elf->e_version = EV_CURRENT;
	elf->e_phoff = sizeof(struct elfhdr);
	elf->e_flags = flags;
	elf->e_ehsize = sizeof(struct elfhdr);
	elf->e_phentsize = sizeof(struct elf_phdr);
	elf->e_phnum = segs;

	return;
}

static void fill_elf_note_phdr(struct elf_phdr *phdr, int sz, loff_t offset)
{
	phdr->p_type = PT_NOTE;
	phdr->p_offset = offset;
	phdr->p_vaddr = 0;
	phdr->p_paddr = 0;
	phdr->p_filesz = sz;
	phdr->p_memsz = 0;
	phdr->p_flags = 0;
	phdr->p_align = 0;
	return;
}

static void fill_note(struct memelfnote *note, const char *name, int type, 
		unsigned int sz, void *data)
{
	note->name = name;
	note->type = type;
	note->datasz = sz;
	note->data = data;
	return;
}

/*
 * fill up all the fields in prstatus from the given task struct, except
 * registers which need to be filled up separately.
 */
static void fill_prstatus(struct elf_prstatus *prstatus,
		struct task_struct *p, long signr)
{
	prstatus->pr_info.si_signo = prstatus->pr_cursig = signr;
	prstatus->pr_sigpend = p->pending.signal.sig[0];
	prstatus->pr_sighold = p->blocked.sig[0];
	rcu_read_lock();
	prstatus->pr_ppid = task_pid_vnr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	prstatus->pr_pid = task_pid_vnr(p);
	prstatus->pr_pgrp = task_pgrp_vnr(p);
	prstatus->pr_sid = task_session_vnr(p);
	if (thread_group_leader(p)) {
		struct task_cputime cputime;

		/*
		 * This is the record for the group leader.  It shows the
		 * group-wide total, not its individual thread total.
		 */
		thread_group_cputime(p, &cputime);
		cputime_to_timeval(cputime.utime, &prstatus->pr_utime);
		cputime_to_timeval(cputime.stime, &prstatus->pr_stime);
	} else {
		cputime_t utime, stime;

		task_cputime(p, &utime, &stime);
		cputime_to_timeval(utime, &prstatus->pr_utime);
		cputime_to_timeval(stime, &prstatus->pr_stime);
	}
	cputime_to_timeval(p->signal->cutime, &prstatus->pr_cutime);
	cputime_to_timeval(p->signal->cstime, &prstatus->pr_cstime);
}

static int fill_psinfo(struct elf_prpsinfo *psinfo, struct task_struct *p,
		       struct mm_struct *mm)
{
	const struct cred *cred;
	unsigned int i, len;
	
	/* first copy the parameters from user space */
	memset(psinfo, 0, sizeof(struct elf_prpsinfo));

	len = mm->arg_end - mm->arg_start;
	if (len >= ELF_PRARGSZ)
		len = ELF_PRARGSZ-1;
	if (copy_from_user(&psinfo->pr_psargs,
		           (const char __user *)mm->arg_start, len))
		return -EFAULT;
	for(i = 0; i < len; i++)
		if (psinfo->pr_psargs[i] == 0)
			psinfo->pr_psargs[i] = ' ';
	psinfo->pr_psargs[len] = 0;

	rcu_read_lock();
	psinfo->pr_ppid = task_pid_vnr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	psinfo->pr_pid = task_pid_vnr(p);
	psinfo->pr_pgrp = task_pgrp_vnr(p);
	psinfo->pr_sid = task_session_vnr(p);

	i = p->state ? ffz(~p->state) + 1 : 0;
	psinfo->pr_state = i;
	psinfo->pr_sname = (i > 5) ? '.' : "RSDTZW"[i];
	psinfo->pr_zomb = psinfo->pr_sname == 'Z';
	psinfo->pr_nice = task_nice(p);
	psinfo->pr_flag = p->flags;
	rcu_read_lock();
	cred = __task_cred(p);
	SET_UID(psinfo->pr_uid, from_kuid_munged(cred->user_ns, cred->uid));
	SET_GID(psinfo->pr_gid, from_kgid_munged(cred->user_ns, cred->gid));
	rcu_read_unlock();
	strncpy(psinfo->pr_fname, p->comm, sizeof(psinfo->pr_fname));
	
	return 0;
}

static void fill_auxv_note(struct memelfnote *note, struct mm_struct *mm)
{
	elf_addr_t *auxv = (elf_addr_t *) mm->saved_auxv;
	int i = 0;
	do
		i += 2;
	while (auxv[i - 2] != AT_NULL);
	fill_note(note, "CORE", NT_AUXV, i * sizeof(elf_addr_t), auxv);
}

static void fill_siginfo_note(struct memelfnote *note, user_siginfo_t *csigdata,
		const siginfo_t *siginfo)
{
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	copy_siginfo_to_user((user_siginfo_t __user *) csigdata, siginfo);
	set_fs(old_fs);
	fill_note(note, "CORE", NT_SIGINFO, sizeof(*csigdata), csigdata);
}

#define MAX_FILE_NOTE_SIZE (4*1024*1024)
/*
 * Format of NT_FILE note:
 *
 * long count     -- how many files are mapped
 * long page_size -- units for file_ofs
 * array of [COUNT] elements of
 *   long start
 *   long end
 *   long file_ofs
 * followed by COUNT filenames in ASCII: "FILE1" NUL "FILE2" NUL...
 */
static int fill_files_note(struct memelfnote *note)
{
	struct vm_area_struct *vma;
	unsigned count, size, names_ofs, remaining, n;
	user_long_t *data;
	user_long_t *start_end_ofs;
	char *name_base, *name_curpos;

	/* *Estimated* file count and total data size needed */
	count = current->mm->map_count;
	size = count * 64;

	names_ofs = (2 + 3 * count) * sizeof(data[0]);
 alloc:
	if (size >= MAX_FILE_NOTE_SIZE) /* paranoia check */
		return -EINVAL;
	size = round_up(size, PAGE_SIZE);
	data = vmalloc(size);
	if (!data)
		return -ENOMEM;

	start_end_ofs = data + 2;
	name_base = name_curpos = ((char *)data) + names_ofs;
	remaining = size - names_ofs;
	count = 0;
	for (vma = current->mm->mmap; vma != NULL; vma = vma->vm_next) {
		struct file *file;
		const char *filename;

		file = vma->vm_file;
		if (!file)
			continue;
		filename = file_path(file, name_curpos, remaining);
		if (IS_ERR(filename)) {
			if (PTR_ERR(filename) == -ENAMETOOLONG) {
				vfree(data);
				size = size * 5 / 4;
				goto alloc;
			}
			continue;
		}

		/* file_path() fills at the end, move name down */
		/* n = strlen(filename) + 1: */
		n = (name_curpos + remaining) - filename;
		remaining = filename - name_curpos;
		memmove(name_curpos, filename, n);
		name_curpos += n;

		*start_end_ofs++ = vma->vm_start;
		*start_end_ofs++ = vma->vm_end;
		*start_end_ofs++ = vma->vm_pgoff;
		count++;
	}

	/* Now we know exact count of files, can store it */
	data[0] = count;
	data[1] = PAGE_SIZE;
	/*
	 * Count usually is less than current->mm->map_count,
	 * we need to move filenames down.
	 */
	n = current->mm->map_count - count;
	if (n != 0) {
		unsigned shift_bytes = n * 3 * sizeof(data[0]);
		memmove(name_base - shift_bytes, name_base,
			name_curpos - name_base);
		name_curpos -= shift_bytes;
	}

	size = name_curpos - (char *)data;
	fill_note(note, "CORE", NT_FILE, size, data);
	return 0;
}

#ifdef CORE_DUMP_USE_REGSET
#include <linux/regset.h>

struct elf_thread_core_info {
	struct elf_thread_core_info *next;
	struct task_struct *task;
	struct elf_prstatus prstatus;
	struct memelfnote notes[0];
};

struct elf_note_info {
	struct elf_thread_core_info *thread;
	struct memelfnote psinfo;
	struct memelfnote signote;
	struct memelfnote auxv;
	struct memelfnote files;
	user_siginfo_t csigdata;
	size_t size;
	int thread_notes;
};

/*
 * When a regset has a writeback hook, we call it on each thread before
 * dumping user memory.  On register window machines, this makes sure the
 * user memory backing the register data is up to date before we read it.
 */
static void do_thread_regset_writeback(struct task_struct *task,
				       const struct user_regset *regset)
{
	if (regset->writeback)
		regset->writeback(task, regset, 1);
}

#ifndef PRSTATUS_SIZE
#define PRSTATUS_SIZE(S, R) sizeof(S)
#endif

#ifndef SET_PR_FPVALID
#define SET_PR_FPVALID(S, V, R) ((S)->pr_fpvalid = (V))
#endif

static int fill_thread_core_info(struct elf_thread_core_info *t,
				 const struct user_regset_view *view,
				 long signr, size_t *total)
{
	unsigned int i;
	unsigned int regset_size = view->regsets[0].n * view->regsets[0].size;

	/*
	 * NT_PRSTATUS is the one special case, because the regset data
	 * goes into the pr_reg field inside the note contents, rather
	 * than being the whole note contents.  We fill the reset in here.
	 * We assume that regset 0 is NT_PRSTATUS.
	 */
	fill_prstatus(&t->prstatus, t->task, signr);
	(void) view->regsets[0].get(t->task, &view->regsets[0], 0, regset_size,
				    &t->prstatus.pr_reg, NULL);

	fill_note(&t->notes[0], "CORE", NT_PRSTATUS,
		  PRSTATUS_SIZE(t->prstatus, regset_size), &t->prstatus);
	*total += notesize(&t->notes[0]);

	do_thread_regset_writeback(t->task, &view->regsets[0]);

	/*
	 * Each other regset might generate a note too.  For each regset
	 * that has no core_note_type or is inactive, we leave t->notes[i]
	 * all zero and we'll know to skip writing it later.
	 */
	for (i = 1; i < view->n; ++i) {
		const struct user_regset *regset = &view->regsets[i];
		do_thread_regset_writeback(t->task, regset);
		if (regset->core_note_type && regset->get &&
		    (!regset->active || regset->active(t->task, regset))) {
			int ret;
			size_t size = regset->n * regset->size;
			void *data = kmalloc(size, GFP_KERNEL);
			if (unlikely(!data))
				return 0;
			ret = regset->get(t->task, regset,
					  0, size, data, NULL);
			if (unlikely(ret))
				kfree(data);
			else {
				if (regset->core_note_type != NT_PRFPREG)
					fill_note(&t->notes[i], "LINUX",
						  regset->core_note_type,
						  size, data);
				else {
					SET_PR_FPVALID(&t->prstatus,
							1, regset_size);
					fill_note(&t->notes[i], "CORE",
						  NT_PRFPREG, size, data);
				}
				*total += notesize(&t->notes[i]);
			}
		}
	}

	return 1;
}

static int fill_note_info(struct elfhdr *elf, int phdrs,
			  struct elf_note_info *info,
			  const siginfo_t *siginfo, struct pt_regs *regs)
{
	struct task_struct *dump_task = current;
	const struct user_regset_view *view = task_user_regset_view(dump_task);
	struct elf_thread_core_info *t;
	struct elf_prpsinfo *psinfo;
	struct core_thread *ct;
	unsigned int i;

	info->size = 0;
	info->thread = NULL;

	psinfo = kmalloc(sizeof(*psinfo), GFP_KERNEL);
	if (psinfo == NULL) {
		info->psinfo.data = NULL; /* So we don't free this wrongly */
		return 0;
	}

	fill_note(&info->psinfo, "CORE", NT_PRPSINFO, sizeof(*psinfo), psinfo);

	/*
	 * Figure out how many notes we're going to need for each thread.
	 */
	info->thread_notes = 0;
	for (i = 0; i < view->n; ++i)
		if (view->regsets[i].core_note_type != 0)
			++info->thread_notes;

	/*
	 * Sanity check.  We rely on regset 0 being in NT_PRSTATUS,
	 * since it is our one special case.
	 */
	if (unlikely(info->thread_notes == 0) ||
	    unlikely(view->regsets[0].core_note_type != NT_PRSTATUS)) {
		WARN_ON(1);
		return 0;
	}

	/*
	 * Initialize the ELF file header.
	 */
	fill_elf_header(elf, phdrs,
			view->e_machine, view->e_flags);

	/*
	 * Allocate a structure for each thread.
	 */
	for (ct = &dump_task->mm->core_state->dumper; ct; ct = ct->next) {
		t = kzalloc(offsetof(struct elf_thread_core_info,
				     notes[info->thread_notes]),
			    GFP_KERNEL);
		if (unlikely(!t))
			return 0;

		t->task = ct->task;
		if (ct->task == dump_task || !info->thread) {
			t->next = info->thread;
			info->thread = t;
		} else {
			/*
			 * Make sure to keep the original task at
			 * the head of the list.
			 */
			t->next = info->thread->next;
			info->thread->next = t;
		}
	}

	/*
	 * Now fill in each thread's information.
	 */
	for (t = info->thread; t != NULL; t = t->next)
		if (!fill_thread_core_info(t, view, siginfo->si_signo, &info->size))
			return 0;

	/*
	 * Fill in the two process-wide notes.
	 */
	fill_psinfo(psinfo, dump_task->group_leader, dump_task->mm);
	info->size += notesize(&info->psinfo);

	fill_siginfo_note(&info->signote, &info->csigdata, siginfo);
	info->size += notesize(&info->signote);

	fill_auxv_note(&info->auxv, current->mm);
	info->size += notesize(&info->auxv);

	if (fill_files_note(&info->files) == 0)
		info->size += notesize(&info->files);

	return 1;
}

static size_t get_note_info_size(struct elf_note_info *info)
{
	return info->size;
}

/*
 * Write all the notes for each thread.  When writing the first thread, the
 * process-wide notes are interleaved after the first thread-specific note.
 */
static int write_note_info(struct elf_note_info *info,
			   struct coredump_params *cprm)
{
	bool first = true;
	struct elf_thread_core_info *t = info->thread;

	do {
		int i;

		if (!writenote(&t->notes[0], cprm))
			return 0;

		if (first && !writenote(&info->psinfo, cprm))
			return 0;
		if (first && !writenote(&info->signote, cprm))
			return 0;
		if (first && !writenote(&info->auxv, cprm))
			return 0;
		if (first && info->files.data &&
				!writenote(&info->files, cprm))
			return 0;

		for (i = 1; i < info->thread_notes; ++i)
			if (t->notes[i].data &&
			    !writenote(&t->notes[i], cprm))
				return 0;

		first = false;
		t = t->next;
	} while (t);

	return 1;
}

static void free_note_info(struct elf_note_info *info)
{
	struct elf_thread_core_info *threads = info->thread;
	while (threads) {
		unsigned int i;
		struct elf_thread_core_info *t = threads;
		threads = t->next;
		WARN_ON(t->notes[0].data && t->notes[0].data != &t->prstatus);
		for (i = 1; i < info->thread_notes; ++i)
			kfree(t->notes[i].data);
		kfree(t);
	}
	kfree(info->psinfo.data);
	vfree(info->files.data);
}

#else

/* Here is the structure in which status of each thread is captured. */
struct elf_thread_status
{
	struct list_head list;
	struct elf_prstatus prstatus;	/* NT_PRSTATUS */
	elf_fpregset_t fpu;		/* NT_PRFPREG */
	struct task_struct *thread;
#ifdef ELF_CORE_COPY_XFPREGS
	elf_fpxregset_t xfpu;		/* ELF_CORE_XFPREG_TYPE */
#endif
	struct memelfnote notes[3];
	int num_notes;
};

/*
 * In order to add the specific thread information for the elf file format,
 * we need to keep a linked list of every threads pr_status and then create
 * a single section for them in the final core file.
 */
static int elf_dump_thread_status(long signr, struct elf_thread_status *t)
{
	int sz = 0;
	struct task_struct *p = t->thread;
	t->num_notes = 0;

	fill_prstatus(&t->prstatus, p, signr);
	elf_core_copy_task_regs(p, &t->prstatus.pr_reg);	
	
	fill_note(&t->notes[0], "CORE", NT_PRSTATUS, sizeof(t->prstatus),
		  &(t->prstatus));
	t->num_notes++;
	sz += notesize(&t->notes[0]);

	if ((t->prstatus.pr_fpvalid = elf_core_copy_task_fpregs(p, NULL,
								&t->fpu))) {
		fill_note(&t->notes[1], "CORE", NT_PRFPREG, sizeof(t->fpu),
			  &(t->fpu));
		t->num_notes++;
		sz += notesize(&t->notes[1]);
	}

#ifdef ELF_CORE_COPY_XFPREGS
	if (elf_core_copy_task_xfpregs(p, &t->xfpu)) {
		fill_note(&t->notes[2], "LINUX", ELF_CORE_XFPREG_TYPE,
			  sizeof(t->xfpu), &t->xfpu);
		t->num_notes++;
		sz += notesize(&t->notes[2]);
	}
#endif	
	return sz;
}

struct elf_note_info {
	struct memelfnote *notes;
	struct memelfnote *notes_files;
	struct elf_prstatus *prstatus;	/* NT_PRSTATUS */
	struct elf_prpsinfo *psinfo;	/* NT_PRPSINFO */
	struct list_head thread_list;
	elf_fpregset_t *fpu;
#ifdef ELF_CORE_COPY_XFPREGS
	elf_fpxregset_t *xfpu;
#endif
	user_siginfo_t csigdata;
	int thread_status_size;
	int numnote;
};

static int elf_note_info_init(struct elf_note_info *info)
{
	memset(info, 0, sizeof(*info));
	INIT_LIST_HEAD(&info->thread_list);

	/* Allocate space for ELF notes */
	info->notes = kmalloc(8 * sizeof(struct memelfnote), GFP_KERNEL);
	if (!info->notes)
		return 0;
	info->psinfo = kmalloc(sizeof(*info->psinfo), GFP_KERNEL);
	if (!info->psinfo)
		return 0;
	info->prstatus = kmalloc(sizeof(*info->prstatus), GFP_KERNEL);
	if (!info->prstatus)
		return 0;
	info->fpu = kmalloc(sizeof(*info->fpu), GFP_KERNEL);
	if (!info->fpu)
		return 0;
#ifdef ELF_CORE_COPY_XFPREGS
	info->xfpu = kmalloc(sizeof(*info->xfpu), GFP_KERNEL);
	if (!info->xfpu)
		return 0;
#endif
	return 1;
}

static int fill_note_info(struct elfhdr *elf, int phdrs,
			  struct elf_note_info *info,
			  const siginfo_t *siginfo, struct pt_regs *regs)
{
	struct list_head *t;
	struct core_thread *ct;
	struct elf_thread_status *ets;

	if (!elf_note_info_init(info))
		return 0;

	for (ct = current->mm->core_state->dumper.next;
					ct; ct = ct->next) {
		ets = kzalloc(sizeof(*ets), GFP_KERNEL);
		if (!ets)
			return 0;

		ets->thread = ct->task;
		list_add(&ets->list, &info->thread_list);
	}

	list_for_each(t, &info->thread_list) {
		int sz;

		ets = list_entry(t, struct elf_thread_status, list);
		sz = elf_dump_thread_status(siginfo->si_signo, ets);
		info->thread_status_size += sz;
	}
	/* now collect the dump for the current */
	memset(info->prstatus, 0, sizeof(*info->prstatus));
	fill_prstatus(info->prstatus, current, siginfo->si_signo);
	elf_core_copy_regs(&info->prstatus->pr_reg, regs);

	/* Set up header */
	fill_elf_header(elf, phdrs, ELF_ARCH, ELF_CORE_EFLAGS);

	/*
	 * Set up the notes in similar form to SVR4 core dumps made
	 * with info from their /proc.
	 */

	fill_note(info->notes + 0, "CORE", NT_PRSTATUS,
		  sizeof(*info->prstatus), info->prstatus);
	fill_psinfo(info->psinfo, current->group_leader, current->mm);
	fill_note(info->notes + 1, "CORE", NT_PRPSINFO,
		  sizeof(*info->psinfo), info->psinfo);

	fill_siginfo_note(info->notes + 2, &info->csigdata, siginfo);
	fill_auxv_note(info->notes + 3, current->mm);
	info->numnote = 4;

	if (fill_files_note(info->notes + info->numnote) == 0) {
		info->notes_files = info->notes + info->numnote;
		info->numnote++;
	}

	/* Try to dump the FPU. */
	info->prstatus->pr_fpvalid = elf_core_copy_task_fpregs(current, regs,
							       info->fpu);
	if (info->prstatus->pr_fpvalid)
		fill_note(info->notes + info->numnote++,
			  "CORE", NT_PRFPREG, sizeof(*info->fpu), info->fpu);
#ifdef ELF_CORE_COPY_XFPREGS
	if (elf_core_copy_task_xfpregs(current, info->xfpu))
		fill_note(info->notes + info->numnote++,
			  "LINUX", ELF_CORE_XFPREG_TYPE,
			  sizeof(*info->xfpu), info->xfpu);
#endif

	return 1;
}

static size_t get_note_info_size(struct elf_note_info *info)
{
	int sz = 0;
	int i;

	for (i = 0; i < info->numnote; i++)
		sz += notesize(info->notes + i);

	sz += info->thread_status_size;

	return sz;
}

static int write_note_info(struct elf_note_info *info,
			   struct coredump_params *cprm)
{
	int i;
	struct list_head *t;

	for (i = 0; i < info->numnote; i++)
		if (!writenote(info->notes + i, cprm))
			return 0;

	/* write out the thread status notes section */
	list_for_each(t, &info->thread_list) {
		struct elf_thread_status *tmp =
				list_entry(t, struct elf_thread_status, list);

		for (i = 0; i < tmp->num_notes; i++)
			if (!writenote(&tmp->notes[i], cprm))
				return 0;
	}

	return 1;
}

static void free_note_info(struct elf_note_info *info)
{
	while (!list_empty(&info->thread_list)) {
		struct list_head *tmp = info->thread_list.next;
		list_del(tmp);
		kfree(list_entry(tmp, struct elf_thread_status, list));
	}

	/* Free data possibly allocated by fill_files_note(): */
	if (info->notes_files)
		vfree(info->notes_files->data);

	kfree(info->prstatus);
	kfree(info->psinfo);
	kfree(info->notes);
	kfree(info->fpu);
#ifdef ELF_CORE_COPY_XFPREGS
	kfree(info->xfpu);
#endif
}

#endif

static struct vm_area_struct *first_vma(struct task_struct *tsk,
					struct vm_area_struct *gate_vma)
{
	struct vm_area_struct *ret = tsk->mm->mmap;

	if (ret)
		return ret;
	return gate_vma;
}
/*
 * Helper function for iterating across a vma list.  It ensures that the caller
 * will visit `gate_vma' prior to terminating the search.
 */
static struct vm_area_struct *next_vma(struct vm_area_struct *this_vma,
					struct vm_area_struct *gate_vma)
{
	struct vm_area_struct *ret;

	ret = this_vma->vm_next;
	if (ret)
		return ret;
	if (this_vma == gate_vma)
		return NULL;
	return gate_vma;
}

static void fill_extnum_info(struct elfhdr *elf, struct elf_shdr *shdr4extnum,
			     elf_addr_t e_shoff, int segs)
{
	elf->e_shoff = e_shoff;
	elf->e_shentsize = sizeof(*shdr4extnum);
	elf->e_shnum = 1;
	elf->e_shstrndx = SHN_UNDEF;

	memset(shdr4extnum, 0, sizeof(*shdr4extnum));

	shdr4extnum->sh_type = SHT_NULL;
	shdr4extnum->sh_size = elf->e_shnum;
	shdr4extnum->sh_link = elf->e_shstrndx;
	shdr4extnum->sh_info = segs;
}

static size_t elf_core_vma_data_size(struct vm_area_struct *gate_vma,
				     unsigned long mm_flags)
{
	struct vm_area_struct *vma;
	size_t size = 0;

	for (vma = first_vma(current, gate_vma); vma != NULL;
	     vma = next_vma(vma, gate_vma))
		size += vma_dump_size(vma, mm_flags);
	return size;
}

#if (MP_DEBUG_TOOL_COREDUMP == 1)
static int write_coredump(struct coredump_params *cprm, unsigned char *uncomp_src, unsigned char *comp_buf,
		loff_t uncomp_src_len, unsigned long *crc, u32 *is_z_finish)
{
	unsigned long prev_comp_write_offset = 0, comp_write_offset = 0;

	/** VDLP 4.2.1.x bugfix, return STREAM_END_SPACE error, 2009-09-21 */
	if (uncomp_src_len <= STREAM_END_SPACE) {
		printk(KERN_ALERT "##### Warning uncompressed src length <= STREAM_END_SPACE 12bytes...\n");
		printk(KERN_ALERT "##### Check uncomp_src_len size : %lld \n", uncomp_src_len);
		//return -1;
	}

    def_strm.next_in = uncomp_src;
    def_strm.total_in = 0;

    def_strm.next_out = comp_buf;
    def_strm.total_out = 0;


    def_strm.avail_out = uncomp_src_len;
    def_strm.avail_in = uncomp_src_len;

    /* crc32 check, used in GZIP tailer */
    *crc = gzip_crc32_le(def_strm.next_in, def_strm.avail_in);


    if (!dump_emit(cprm, uncomp_src,uncomp_src_len)) {
        printk(KERN_ALERT "##### coredump ELF section dump_emit() fail...\n");
        return -1;
    }

    /* End of compressed data block, deflate Z_FINISH */
    if (*is_z_finish) {
        coredump_debug("##### (vma->vm_next) == NULL ...\n");
        def_strm.next_out = comp_buf;
        def_strm.avail_out += STREAM_END_SPACE;
        def_strm.avail_in = 0;

        comp_write_offset = (def_strm.total_out - prev_comp_write_offset);

        /* Ultimate coredump file write */
        if (!dump_emit(cprm, comp_buf, STREAM_END_SPACE)) {
            printk(KERN_ALERT "##### In case Z_FINISH, coredump ELF section dump_emit() fail...\n");
            return -1;
        }
    }

    return 0;
}

static int set_gzip_header(struct coredump_params *cprm)
{
	gzip_header_t gzip_hdr;
	struct timeval ktv;
	const char *coredump_filename = "Coredump.gz";

	/* gzip ID */
	gzip_hdr.id[0] = 0x1f;
	gzip_hdr.id[1] = 0x8b;

	/* CM - Compressed Method */
	gzip_hdr.cm = 8;

	/* FLG - flag=8 write original file name */
	gzip_hdr.flag = 8;

	/* MTime - Modification Time */
	do_gettimeofday(&ktv);
	memcpy(gzip_hdr.mTime, &ktv.tv_sec, sizeof(time_t));

	/* XFL - eXtra Flags */
	gzip_hdr.xfl = 2;

	/* OS - OS Filesystem */
	gzip_hdr.os = 3;

	/* gzip_header_t write */
	if (!dump_emit(cprm, &gzip_hdr, sizeof(gzip_header_t))) {
		printk(KERN_ALERT "gzip header dump_emit() fail\n");
		return -1;
	}

	/* write original file name */
	if (!dump_emit(cprm, coredump_filename, strlen(coredump_filename)+1)) {
		printk(KERN_ALERT "coredump filenale dump_emit() fail\n");
		return -1;
	}

	return 0;
}

static int set_gzip_tailer(struct coredump_params *cprm, unsigned long *crc, unsigned long *uncomp_size)
{
	*crc = le32_to_cpu((*crc));
	*uncomp_size = le32_to_cpu((*uncomp_size));

	/* GZIP tailer, CRC32 */
	coredump_debug("##### GZIP tailer CRC32 : %lu\n", *crc);

	if (!dump_emit(cprm, crc, sizeof(u32))) {
		printk(KERN_ALERT "##### GZIP tailer CRC32, dump_emit() fail\n");
		return -1;
	}

	if (!dump_emit(cprm, uncomp_size, sizeof(unsigned long))) {
		printk(KERN_ALERT "##### GZIP tailer uncompressed file size, dump_emit() fail\n");
		return -1;
	}

	return 0;
}

static int compress_coredump(struct coredump_params *cprm, unsigned char *uncomp_src, unsigned char *comp_buf,
		loff_t uncomp_src_len, unsigned long *crc, u32 *is_z_finish)
{
	int ret = 0;
	unsigned long prev_comp_write_offset = 0, comp_write_offset = 0;

	/** VDLP 4.2.1.x bugfix, return STREAM_END_SPACE error, 2009-09-21 */
	if (uncomp_src_len <= STREAM_END_SPACE) {
		printk(KERN_ALERT "##### Warning uncompressed src length <= STREAM_END_SPACE 12bytes...\n");
		printk(KERN_ALERT "##### Check uncomp_src_len size : %lld \n", uncomp_src_len);
		//return -1;
	}

	def_strm.next_in = uncomp_src;
	def_strm.total_in = 0;

	def_strm.next_out = comp_buf;
	def_strm.total_out = 0;


	def_strm.avail_out = uncomp_src_len;
	def_strm.avail_in = uncomp_src_len;

	/* crc32 check, used in GZIP tailer */
	*crc = gzip_crc32_le(def_strm.next_in, def_strm.avail_in);

	/* if you want to compress debugging step by step
	   coredump_debug("##### calling deflate with avail_in %u, avail_out %u\n",
	   def_strm.avail_in, def_strm.avail_out);
	 */
	ret = zlib_deflate(&def_strm, Z_PARTIAL_FLUSH);

	/* if you want to compress debugging step by step
	   coredump_debug("##### deflate returned with avail_in %u, avail_out %u, total_in %ld, total_out %ld\n",
	   def_strm.avail_in, def_strm.avail_out, def_strm.total_in, def_strm.total_out);
	 */
	if (ret != Z_OK) {
		printk(KERN_ALERT "##### deflate in loop returned %d\n", ret);
		zlib_deflateEnd(&def_strm);
		return -1;
	}

	/* Ultimate coredump file write */
	if (!dump_emit(cprm, comp_buf, def_strm.total_out)) {
		printk(KERN_ALERT "##### coredump ELF section dump_emit() fail...\n");
		return -1;
	}

	/* set next page address, if you want to compress debugging step by step
	   coredump_debug("##### while() def_strm.next_in : %p\n", def_strm.next_in);
	 */

	/* if you want to compress debugging step by step
	   coredump_debug("##### while() def_strm.next_out : %p\n", def_strm.next_out);
	 */

	prev_comp_write_offset = def_strm.total_out;

	/* End of compressed data block, deflate Z_FINISH */
	if (*is_z_finish) {
		coredump_debug("##### (vma->vm_next) == NULL ...\n");
		def_strm.next_out = comp_buf;
		def_strm.avail_out += STREAM_END_SPACE;
		def_strm.avail_in = 0;

		ret = zlib_deflate(&def_strm, Z_FINISH);

		if (ret != Z_STREAM_END) {
			printk(KERN_ALERT "##### final deflate returned %d\n", ret);
			return -1;
		}

		comp_write_offset = (def_strm.total_out - prev_comp_write_offset);

		/* Ultimate coredump file write */
		if (!dump_emit(cprm, comp_buf, comp_write_offset)) {
			printk(KERN_ALERT "##### In case Z_FINISH, coredump ELF section dump_emit() fail...\n");
			return -1;
		}
	}

	return 0;
}

static int coredump_alloc_workspaces(void)
{
	def_strm.workspace = vmalloc(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL));

	if (!def_strm.workspace) {
		printk(KERN_ALERT "##### Failed to allocate %d bytes for deflate workspace\n", zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL));
		return -ENOMEM;
	}
	coredump_debug("##### Allocated %d bytes for deflate workspace\n", zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL));

	return 0;
}

static void coredump_free_workspaces(void)
{
	vfree(def_strm.workspace);
}
#endif /*MP_DEBUG_TOOL_COREDUMP*/

/*
 * Actual dumper
 *
 * This is a two-pass process; first we find the offsets of the bits,
 * and then they are actually written out.  If we run out of core limit
 * we just truncate.
 */
static int elf_core_dump(struct coredump_params *cprm)
{
	int has_dumped = 0;
	mm_segment_t fs;
	int segs, i;
	size_t vma_data_size = 0;
	struct vm_area_struct *vma, *gate_vma;
	struct elfhdr *elf = NULL;
	loff_t offset = 0, dataoff, foffset;
	struct elf_note_info info = { };
	struct elf_phdr *phdr4note = NULL;
	struct elf_shdr *shdr4extnum = NULL;
	Elf_Half e_phnum;
	elf_addr_t e_shoff;
	elf_addr_t *vma_filesz = NULL;

#if (MP_DEBUG_TOOL_COREDUMP == 1)
	/* Ultimate Coredump var */
#define CORE_BIG_VMA            0
#define CORE_SAME_VMA           1
#define CORE_SMALL_VMA          2
	int  ret_comp_val=0;
#ifdef CORE_DUMP_USE_REGSET
	struct elf_thread_core_info *t;
	const struct user_regset_view *view = task_user_regset_view(current);
#else
	struct list_head *t = NULL;
#endif
	size_t aligned_elfhdr_sect_sz=0, elfhdr_sect_sz=0;
	size_t  note_size=0, aligned_elf_pages_num=0, last_vma =0, z_finish=0;
	unsigned char *elf_file_buf=NULL, *elf_zero_file_buf=NULL;
	unsigned char *compressed_buf=NULL, *uncomp_src_buf=NULL;
	loff_t elf_foffset=0, aligned_elf_foffset=0;
	unsigned long uncomp_coredump_file_size=0;
	unsigned long crc32_val=0;
	unsigned int user_page_cnt=0, kernel_page_cnt=0, zero_page_cnt=0, vma_cnt=0, vm_page=0;
	unsigned char locked = 0;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

	/*
	 * We no longer stop all VM operations.
	 *
	 * This is because those proceses that could possibly change map_count
	 * or the mmap / vma pages are now blocked in do_exit on current
	 * finishing this core dump.
	 *
	 * Only ptrace can touch these memory addresses, but it doesn't change
	 * the map_count or the pages allocated. So no possibility of crashing
	 * exists while dumping the mm->vm_next areas to the core file.
	 */
  
	/* alloc memory for large data structures: too large to be on stack */
	elf = kmalloc(sizeof(*elf), GFP_KERNEL);
	if (!elf)
		goto out;
	/*
	 * The number of segs are recored into ELF header as 16bit value.
	 * Please check DEFAULT_MAX_MAP_COUNT definition when you modify here.
	 */
	segs = current->mm->map_count;
	segs += elf_core_extra_phdrs();

	gate_vma = get_gate_vma(current->mm);
	if (gate_vma != NULL)
		segs++;

	/* for notes section */
	segs++;

	/* If segs > PN_XNUM(0xffff), then e_phnum overflows. To avoid
	 * this, kernel supports extended numbering. Have a look at
	 * include/linux/elf.h for further information. */
	e_phnum = segs > PN_XNUM ? PN_XNUM : segs;

	/*
	 * Collect all the non-memory information about the process for the
	 * notes.  This also sets up the file header.
	 */
	if (!fill_note_info(elf, e_phnum, &info, cprm->siginfo, cprm->regs))
		goto cleanup;

	has_dumped = 1;

	fs = get_fs();
	set_fs(KERNEL_DS);

#if (MP_DEBUG_TOOL_COREDUMP == 1)
#ifdef CORE_DUMP_USE_REGSET
	/* info->size already has the total size of the note sections (including the
	 * thread sections as per fill_note_info, so no need to compute that. */
	note_size = info.size;
#else
	/*
	 * calc total size of note sections.
	 */
	for (i = 0; i < info.numnote; i++)
		note_size += notesize(info.notes + i);
#endif

	/* calc total size of ELF header, program header and note sections. */

	elfhdr_sect_sz += sizeof(struct elfhdr);                /* ELF header */
	elfhdr_sect_sz += sizeof(struct elf_phdr);              /* Program header for current process */
	elfhdr_sect_sz += (segs * sizeof(struct elf_phdr));     /* Profram header for all threads of current process */
	elfhdr_sect_sz += note_size;                            /* Note section for current process */
#ifndef CORE_DUMP_USE_REGSET
	elfhdr_sect_sz += info.thread_status_size;                   /* Note section for all threads of current process */
#endif
	aligned_elfhdr_sect_sz = roundup(elfhdr_sect_sz, ELF_EXEC_PAGESIZE);    /* aligned 4KB */

	aligned_elf_pages_num = (aligned_elfhdr_sect_sz/ELF_EXEC_PAGESIZE);
	/** VDLP 4.2.1.x bugfix, Coredump.gz size is 0, 2009-09-12
	 Because of memory alloc func changed from kmalloc() to vmalloc(), skip elf max page num
	if (aligned_elf_pages_num > ELF_CORE_MAX_PAGE_NUM) {
		printk(KERN_ALERT "##### elf hdr, section size > 128KB, pages num : %u\n", aligned_elf_pages_num);
		goto end_coredump;
	} else
        */
	coredump_debug(KERN_ALERT "##### elf aligned pages num : %u + (3 Coredump guard buffers)\n", aligned_elf_pages_num);

	elf_zero_file_buf = (unsigned char *)vmalloc((aligned_elf_pages_num + 4) * ELF_EXEC_PAGESIZE);

	if (!elf_zero_file_buf) {
		printk(KERN_ALERT "##### pages num : %u + (3 Coredump guard buffers), vmalloc fail...\n", aligned_elf_pages_num);
		goto end_coredump;
	}

	memset(elf_zero_file_buf, 0, (aligned_elf_pages_num + 4) * ELF_EXEC_PAGESIZE);

	/*
	 * Allocaed memory map
	 +------------------+
	 |   for Zero page  |
	 +------------------+
	 | upper guard buf  |
	 +------------------+    <= elf_file_buf, start address of file writing
	 |       . . .      |
	 |   ELF Coredump   |
	 |       . . .      |
	 +------------------+
	 | lower guard buf  |    <= will be used ELF Coredump after copying ELF section to memory
	 +------------------+
	 | lower guard buf  |
	 +------------------+
	 */

	/* set upper guard buf */
	elf_file_buf = elf_zero_file_buf + (ELF_EXEC_PAGESIZE * 2);

	/* 1. copy ELF Header to memory */
	memcpy(elf_file_buf, elf, sizeof(struct elfhdr));
	elf_foffset += sizeof(struct elfhdr);

	offset += sizeof(*elf);                         /* ELF header */
	offset += segs * sizeof(struct elf_phdr);	/* Program headers */
#else
	offset += sizeof(*elf);				/* Elf header */
	offset += segs * sizeof(struct elf_phdr);	/* Program headers */
	foffset = offset;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

	/* Write notes phdr entry */
	{
#if (MP_DEBUG_TOOL_COREDUMP == 1)
		size_t sz = 0;

		sz = note_size;

#ifndef CORE_DUMP_USE_REGSET
		sz += info.thread_status_size;
#endif
#else
		size_t sz = get_note_info_size(&info);
#endif /*MP_DEBUG_TOOL_COREDUMP*/

		sz += elf_coredump_extra_notes_size();

		phdr4note = kmalloc(sizeof(*phdr4note), GFP_KERNEL);
		if (!phdr4note)
			goto end_coredump;

		fill_elf_note_phdr(phdr4note, sz, offset);
		offset += sz;
#if (MP_DEBUG_TOOL_COREDUMP == 1)
		/* 2. copy ELF Program header to memory */
		memcpy((elf_file_buf+elf_foffset), phdr4note, sizeof(*phdr4note));
		elf_foffset += sizeof(struct elf_phdr);
#endif /*MP_DEBUG_TOOL_COREDUMP*/
	}

	dataoff = offset = roundup(offset, ELF_EXEC_PAGESIZE);

	vma_filesz = kmalloc_array(segs - 1, sizeof(*vma_filesz), GFP_KERNEL);
	if (!vma_filesz)
		goto end_coredump;

	for (i = 0, vma = first_vma(current, gate_vma); vma != NULL;
			vma = next_vma(vma, gate_vma)) {
		unsigned long dump_size;

		dump_size = vma_dump_size(vma, cprm->mm_flags);
		vma_filesz[i++] = dump_size;
		vma_data_size += dump_size;
	}

	offset += vma_data_size;
	offset += elf_core_extra_data_size();
	e_shoff = offset;

	if (e_phnum == PN_XNUM) {
		shdr4extnum = kmalloc(sizeof(*shdr4extnum), GFP_KERNEL);
		if (!shdr4extnum)
			goto end_coredump;
		fill_extnum_info(elf, shdr4extnum, e_shoff, segs);
	}

	offset = dataoff;

#if (MP_DEBUG_TOOL_COREDUMP == 0)
	if (!dump_emit(cprm, elf, sizeof(*elf)))
		goto end_coredump;

	if (!dump_emit(cprm, phdr4note, sizeof(*phdr4note)))
		goto end_coredump;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

	/* Write program headers for segments dump */
	for (i = 0, vma = first_vma(current, gate_vma); vma != NULL;
			vma = next_vma(vma, gate_vma)) {
		struct elf_phdr phdr;

		phdr.p_type = PT_LOAD;
		phdr.p_offset = offset;
		phdr.p_vaddr = vma->vm_start;
		phdr.p_paddr = 0;
		phdr.p_filesz = vma_filesz[i++];
		phdr.p_memsz = vma->vm_end - vma->vm_start;
		offset += phdr.p_filesz;
		phdr.p_flags = vma->vm_flags & VM_READ ? PF_R : 0;
		if (vma->vm_flags & VM_WRITE)
			phdr.p_flags |= PF_W;
		if (vma->vm_flags & VM_EXEC)
			phdr.p_flags |= PF_X;
		phdr.p_align = ELF_EXEC_PAGESIZE;

#if (MP_DEBUG_TOOL_COREDUMP == 1)
		foffset += sizeof(phdr);

		/* 3. copy Segment dump Program Header to memory */
		memcpy((elf_file_buf+elf_foffset), &phdr, sizeof(struct elf_phdr));
		elf_foffset += sizeof(struct elf_phdr);
#else
		if (!dump_emit(cprm, &phdr, sizeof(phdr)))
			goto end_coredump;
#endif /*MP_DEBUG_TOOL_COREDUMP*/
	}

#if (MP_DEBUG_TOOL_COREDUMP == 1)
#ifdef CORE_DUMP_USE_REGSET
	/* 4. copy current Notes section to memory */
	{
		elf_foffset += memcpy_note(&info.psinfo, (elf_file_buf+elf_foffset));
		elf_foffset += memcpy_note(&info.signote, (elf_file_buf+elf_foffset));
		elf_foffset += memcpy_note(&info.auxv, (elf_file_buf+elf_foffset));
		elf_foffset += memcpy_note(&info.files, (elf_file_buf+elf_foffset));
	}
#else
	/* 4. copy current Notes section to memory */
	for (i = 0; i < info.numnote; i++) {
		unsigned long sz = 0;
		sz = memcpy_note(info.notes + i, (elf_file_buf+elf_foffset));
		elf_foffset += sz;
	}
#endif /*MP_DEBUG_TOOL_COREDUMP*/

	if (!elf_core_write_extra_phdrs(cprm, offset))
		goto end_coredump;
#ifdef CORE_DUMP_USE_REGSET
	/* write out the thread status notes section */
	for (t = info.thread; t != NULL; t = t->next) {
		unsigned long sz = 0;
		sz = memcpy_note(&t->notes[0], (elf_file_buf+elf_foffset));
		elf_foffset += sz;

		for (i = 1; i < view->n; ++i) {
			if (t->notes[i].name) {
				sz = memcpy_note(&t->notes[i], (elf_file_buf+elf_foffset));
				elf_foffset += sz;
			}
		}
	}
#else
	/* write out the thread status notes section */
	list_for_each(t, &info.thread_list) {
		struct elf_thread_status *tmp =
			list_entry(t, struct elf_thread_status, list);
		/* 5. copy to thread Notes section to memory */
		for (i = 0; i < tmp->num_notes; i++) {
			unsigned long sz = 0;
			sz = memcpy_note(&tmp->notes[i], (elf_file_buf+elf_foffset));
			elf_foffset += sz;
		}
	}
#endif /* CORE_DUMP_USE_REGSET */
#else

 	/* write out the notes section */
	if (!write_note_info(&info, cprm))
		goto end_coredump;

	if (elf_coredump_extra_notes_write(cprm))
		goto end_coredump;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

	/* Align to page */
#if (MP_DEBUG_TOOL_COREDUMP == 1)
	aligned_elf_foffset = roundup(elf_foffset, ELF_EXEC_PAGESIZE);  /* aligned 4KB */

	if (aligned_elf_foffset == aligned_elfhdr_sect_sz) {
		coredump_debug("##### Not used first lower guard page, elf_foffset : %lld, aligned_elf_foffset : %lld \n",
				elf_foffset, aligned_elf_foffset);
	} else {
		coredump_debug("##### Used first lower guard page, elf_foffset : %lld, aligned_elf_foffset : %lld \n",
				elf_foffset, aligned_elf_foffset);
	}
        if (compress_single == 1)
	{
		if (unlikely(set_gzip_header(cprm) < 0)) {
			printk(KERN_ALERT "##### set_gzip_header() return fail...\n");
			goto end_coredump;
		} else
			coredump_debug("##### set_gzip_header() return success...\n");
        }
	/* alloc zstrem workspace */
	mutex_lock(&deflate_mutex);
	locked = 1;
        if (compress_single == 1)
	{
		if (unlikely(coredump_alloc_workspaces() < 0)) {
			locked = 0;
			printk(KERN_ALERT "##### coredump_alloc_workspaces() return fail...\n");
			goto end_coredump;
		} else
			coredump_debug("##### coredump_alloc_workspaces() return success...\n");
		/* gzip deflate init */
		if (Z_OK != zlib_deflateInit2(&def_strm, 8, Z_DEFLATED, -MAX_WBITS,
					      DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY)) {
			printk(KERN_ALERT "##### gzip deflateInit failed\n");
			goto end_coredump;
	}

	/* CRC32 initialize */
	gzip_crc32_le(NULL, 0);
	}
	/*
	 * alloc vma memory temp buffer size <= ULTIMATE_COMP_BUF_SIZE, default 512KB
	+--------------------------+
	|           . . .          |
	|   saved vma mem region   |
	|  (uncomp buf size ??MB)  |
	|           . . .          |
	+--------------------------+

	* alloc compressed temp buffer size <= ULTIMATE_COMP_BUF_SIZE+12Bytes
	+--------------------------+
	|           . . .          |
	|  saved compressed data   |
	|   (comp buf size ??MB)   |
	|           . . .          |
	+--------------------------+
	| STREAM_END_SPACE 12Bytes |
	+--------------------------+
	*/
	compressed_buf = (unsigned char *)vmalloc(ULTIMATE_COMP_BUF_SIZE+STREAM_END_SPACE);
	uncomp_src_buf = (unsigned char *)vmalloc(ULTIMATE_COMP_BUF_SIZE);

	if (!compressed_buf || !uncomp_src_buf) {
		printk(KERN_ALERT "##### binfmt_flat: no memory for read buffer\n");
		goto end_coredump;
	}
	memset(compressed_buf, 0, ULTIMATE_COMP_BUF_SIZE);
	memset(uncomp_src_buf, 0, ULTIMATE_COMP_BUF_SIZE);
        if (compress_single == 1)
	        ret_comp_val = compress_coredump(cprm, (unsigned char *)elf_file_buf, compressed_buf,
				 aligned_elf_foffset, &crc32_val, &z_finish);
        else
	        ret_comp_val = write_coredump(cprm, (unsigned char *)elf_file_buf, compressed_buf,
                       aligned_elf_foffset, &crc32_val, &z_finish);
	if (ret_comp_val < 0) {
		printk(KERN_ALERT "##### 1nd compress_coredump() return fail...\n");
		goto end_coredump;
	}
#else
	if (!dump_skip(cprm, dataoff - cprm->pos))
		goto end_coredump;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

	for (i = 0, vma = first_vma(current, gate_vma); vma != NULL;
			vma = next_vma(vma, gate_vma)) {
		unsigned long addr;
		unsigned long end;

#if (MP_DEBUG_TOOL_COREDUMP == 1)
		unsigned long vma_size=0;
		unsigned long is_buf_full=0;
		unsigned long is_buf_full_cnt=0;
		unsigned int vma_page_cnt=0;
		unsigned int vma_type=0;
		unsigned int buf_usage_cnt=0;

		++vma_cnt;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

		end = vma->vm_start + vma_filesz[i++];
#if (MP_DEBUG_TOOL_COREDUMP == 1)
		if (next_vma(vma, gate_vma) == NULL)     /* gzip Z_FINISH, coredump last step condition */
			last_vma = 1;

		vma_size = (end - vma->vm_start); /* vma_size */

		if (vma_size == 0)
		{
			//printk(KERN_ALERT "##### vma size is zero, continued...\n");
			continue;
		}

		if (vma_size < ULTIMATE_COMP_BUF_SIZE)
			vma_type = CORE_SMALL_VMA;
		else if ( (vma_size % ULTIMATE_COMP_BUF_SIZE) != 0)
			vma_type = CORE_BIG_VMA;
		else /* buf_size * n == vma_size */
			vma_type = CORE_SAME_VMA;

		buf_usage_cnt = vma_size / (ULTIMATE_COMP_BUF_SIZE);

		if(vma_type != CORE_SAME_VMA)
			buf_usage_cnt++;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

		for (addr = vma->vm_start; addr < end; addr += PAGE_SIZE) {
			struct page *page;
#if (MP_DEBUG_TOOL_COREDUMP == 0)
			int stop;
#endif /*MP_DEBUG_TOOL_COREDUMP*/

#if (MP_DEBUG_TOOL_COREDUMP == 1)
			++vm_page;
#endif /*MP_DEBUG_TOOL_COREDUMP*/
			page = get_dump_page(addr);
			if (page) {
				void *kaddr = kmap(page);
#if (MP_DEBUG_TOOL_COREDUMP == 1)
				++user_page_cnt;
				//++kernel_page_cnt;
				memcpy((uncomp_src_buf+(vma_page_cnt * PAGE_SIZE))
				       , kaddr, PAGE_SIZE);
				elf_foffset += PAGE_SIZE;
#else
				stop = !dump_emit(cprm, kaddr, PAGE_SIZE);
#endif /*MP_DEBUG_TOOL_COREDUMP*/
				kunmap(page);
				put_page(page);
			} else
#if (MP_DEBUG_TOOL_COREDUMP == 1)
				++zero_page_cnt;
#else
				stop = !dump_skip(cprm, PAGE_SIZE);
			if (stop)
				goto end_coredump;
#endif /*MP_DEBUG_TOOL_COREDUMP*/
#if (MP_DEBUG_TOOL_COREDUMP == 1)
			++vma_page_cnt;
			is_buf_full = vma_page_cnt * PAGE_SIZE; /* check buf size */

			/* if buf is full, call compress_coredump() */
			if (is_buf_full >= ULTIMATE_COMP_BUF_SIZE) {
				++is_buf_full_cnt;
				printk(KERN_ALERT "##### default buf is full...cnt : %lu\n", is_buf_full_cnt);

				--buf_usage_cnt;
				/* Check real finish state  for CORE_SAME_VMA */
				if (buf_usage_cnt == 0 && last_vma == 1) {
					if(vma_type != CORE_SAME_VMA)   /* logic error check */
						printk(KERN_ALERT "##### coredump logic Bug,"
						       " vma_type :%d, line : %d\n", vma_type, __LINE__);

					z_finish = 1;
				}
                                if (compress_single == 1)
   	                                ret_comp_val = compress_coredump(cprm, uncomp_src_buf, compressed_buf,
								 ULTIMATE_COMP_BUF_SIZE, &crc32_val, &z_finish); 
                                else
                                        ret_comp_val = write_coredump(cprm, uncomp_src_buf, compressed_buf,
                                               ULTIMATE_COMP_BUF_SIZE, &crc32_val, &z_finish);

				if (ret_comp_val < 0) {
					printk(KERN_ALERT "##### 2nd compress_coredump() return fail...\n");
					goto end_coredump;
		                }

				vma_page_cnt = 0;   /* uncomp_src_buf full, so reset vma_page_cnt */
				is_buf_full = 0;

				memset(uncomp_src_buf, 0, ULTIMATE_COMP_BUF_SIZE);
			}
#endif /*MP_DEBUG_TOOL_COREDUMP*/
		}
#if (MP_DEBUG_TOOL_COREDUMP == 1)
		if (buf_usage_cnt > 1)   /* logic error check */
		{
			printk(KERN_ALERT "##### coredump logic Bug, buf_usage_cnt "
			       ":%d, line : %d\n", buf_usage_cnt, __LINE__);
		} else if (buf_usage_cnt == 1) {
			if(last_vma == 1)
				z_finish = 1;

			switch (vma_type) {
				case CORE_SMALL_VMA:
                                        if (compress_single == 1)
		                                ret_comp_val = compress_coredump(cprm,uncomp_src_buf,compressed_buf,vma_size,&crc32_val, &z_finish);
                                        else
	                                        ret_comp_val = write_coredump(cprm, uncomp_src_buf, compressed_buf,vma_size,&crc32_val, &z_finish);

					if (ret_comp_val < 0) {
						printk(KERN_ALERT "##### 3nd compress_coredump() return fail...\n");
						goto end_coredump;
					}

					break;

				case CORE_BIG_VMA:
                                        if (compress_single == 1)
					        ret_comp_val = compress_coredump(cprm, uncomp_src_buf, compressed_buf,
									 vma_size - ULTIMATE_COMP_BUF_SIZE*is_buf_full_cnt,
									 &crc32_val, &z_finish);
                                        else
                                                ret_comp_val = write_coredump(cprm, uncomp_src_buf, compressed_buf,
                                                       vma_size - ULTIMATE_COMP_BUF_SIZE*is_buf_full_cnt, &crc32_val, &z_finish);

					if (ret_comp_val < 0) {
						printk(KERN_ALERT "##### 4nd compress_coredump() return fail...\n");
						goto end_coredump;
					}

					break;

				default: /* logic error check */
					printk(KERN_ALERT "##### coredump logic Bug, vma_type :%d, line : %d\n", vma_type, __LINE__);
					break;
			}
		}
		/* vma mem buffer init */
	//	memset(uncomp_src_buf, 0, ULTIMATE_COMP_BUF_SIZE);
	//	vma_size = 0;
    //	is_buf_full_cnt = 0;
#endif /*MP_DEBUG_TOOL_COREDUMP*/
	}
#if (MP_DEBUG_TOOL_COREDUMP == 1)
	coredump_debug(" ##### Process addr space debug Info #####\n");
	coredump_debug(" ##### vma_cnt : %u\n",vma_cnt);
	coredump_debug(" ##### vm_page : %u\n",vm_page);
	coredump_debug(" ##### user_page_cnt : %u\n",user_page_cnt);
	coredump_debug(" ##### zero_page_cnt : %u\n",zero_page_cnt);
	coredump_debug(" ##### kernel_page_cnt : %u\n",kernel_page_cnt);

#ifndef CONFIG_MP_DEBUG_TOOL_COREDUMP_WITHOUT_COMPRESS
	/* gzip deflate end */
	zlib_deflateEnd(&def_strm);
#endif
	/* calc original coredump file size, used in GZIP tailer */
	uncomp_coredump_file_size = aligned_elf_foffset;
	uncomp_coredump_file_size += (vm_page * PAGE_SIZE);

	coredump_debug("##### uncomp_coredump_file_size : %lu\n", uncomp_coredump_file_size);
#ifndef CONFIG_MP_DEBUG_TOOL_COREDUMP_WITHOUT_COMPRESS
	if (set_gzip_tailer(cprm, &crc32_val, &uncomp_coredump_file_size) < 0)
		printk(KERN_ALERT "##### set_gzip_tailer() return fail... \n");
#endif
#else
	dump_truncate(cprm);

	if (!elf_core_write_extra_data(cprm))
		goto end_coredump;

	if (e_phnum == PN_XNUM) {
		if (!dump_emit(cprm, shdr4extnum, sizeof(*shdr4extnum)))
			goto end_coredump;
	}
#endif /*MP_DEBUG_TOOL_COREDUMP*/

end_coredump:
	set_fs(fs);

cleanup:
	free_note_info(&info);
	kfree(shdr4extnum);
	kfree(vma_filesz);
	kfree(phdr4note);
	kfree(elf);
#if (MP_DEBUG_TOOL_COREDUMP == 1)
	/* Ultimate Coredump */
	vfree(elf_zero_file_buf);
	vfree(compressed_buf);
	vfree(uncomp_src_buf);
#ifndef CONFIG_MP_DEBUG_TOOL_COREDUMP_WITHOUT_COMPRESS
	if (compress_single && locked) {
		coredump_free_workspaces();
	}
#endif
	mutex_unlock(&deflate_mutex);
#endif /*MP_DEBUG_TOOL_CORDEDUMP*/
out:
	return has_dumped;
}

#endif		/* CONFIG_ELF_CORE */

static int __init init_elf_binfmt(void)
{
	register_binfmt(&elf_format);
	return 0;
}

static void __exit exit_elf_binfmt(void)
{
	/* Remove the COFF and ELF loaders. */
	unregister_binfmt(&elf_format);
}

core_initcall(init_elf_binfmt);
module_exit(exit_elf_binfmt);
MODULE_LICENSE("GPL");
