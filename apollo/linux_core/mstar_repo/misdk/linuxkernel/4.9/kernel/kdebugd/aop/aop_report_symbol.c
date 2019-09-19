/*
 *  kernel/kdebugd/aop/aop_report_symbol.c
 *
 *  Advance oprofile system wide symbol report  releated functions are declared here
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-09-01  Created by karuna.nithy@samsung.com.
 *
 */

#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <linux/dcache.h>
#include <linux/kallsyms.h>
#include <linux/ctype.h>

#include <kdebugd.h>
#include "aop_oprofile.h"
#include "aop_report_symbol.h"

#ifdef	CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#include "aop_debug.h"

/* to show report with function name only */
#define AOP_SHOW_FUNCTION_N_LIB	1

/* to define max kernel symbol length based on ksym length and module name length */
#define AOP_MAX_SYM_LEN (sizeof("%s [%s]")          \
				     + 22 /* KSYM_NAME_LEN */    \
				     + 10 /* OFFSET */ \
				     + 10 /* MODULE_NAME_LEN */ + 1)

/*To get the kernel indication string
If the given sample is for kernel then show (k) after the vma address otherwise leave blank */
#define AOP_KERNEL_IDENTIFICATION(X) ((X) ? "(k)" : "   ")

/*link list to store the symbol related data which
is collected after processing the raw data.
This includes cookie value which is decoded into the PATH of the library & appl.
sample count denotes the number of samples collected for that library,
vma and pc to store symbol program counter. */
struct aop_sym_list {
	struct list_head sym_list;
	aop_cookie_t img_cookie;	/*cookie value of library */
	aop_cookie_t app_cookie;	/*cookie value of application */
	unsigned int addr;	/*program counter value */
	unsigned int samples;
	int in_kernel;		/*in_kernel = 1 if the context is kernel, =0 if it is in user context */
	pid_t tid;
};

/*The samples are processed and the data specific to kernel which
are refereed are extracted and collected in this structure. aop_kernel_list_head is the
head of node of the linked list*/
/* static struct aop_sym_list *aop_sym_list_head = NULL; */
static struct list_head *aop_sym_list_head;
static struct list_head *aop_sym_elf_list_head;

static int aop_sym_elf_load_done;

/* aop report sort option, by default it is none.
It may be changed using setup configuration option */
static int aop_prev_sort_option;

/*decode the cookie into the name of application and libraries.*/
char *aop_decode_cookie_without_path(aop_cookie_t cookie, char *buf,
					    size_t buf_size)
{
	long ret = 0;

	if (buf) {
		if (cookie > 0)
			/*call the function to decode the cookie value without directory PATH */
			ret = aop_lookup_dcookie(cookie, buf, buf_size);
		else
			/* if cookie is not available then return KERNEL */
			ret =
			    snprintf(buf, DNAME_INLINE_LEN,
				     "Unknown sample type");

		/* append null character at the end  */
		if (ret != -EINVAL)
			buf[ret] = '\0';
	}

	/* return buf, it is usefull, incase if this function called in PRINT_KD function */
	return buf;
}

/*decode the cookie into name of kernel application
It is based on logic of __print_symbol() function in kallsyms.c file.
Store the symbol name corresponding to address in buffer */
static void
aop_decode_cookie_kernel_symbol(char *buffer, int buf_len,
				unsigned long address, unsigned long *offset)
{
	char *modname;
	const char *name;
	unsigned long size;
	char namebuf[KSYM_NAME_LEN + 1];

	/* validate the buffer length */
	if (buf_len < (AOP_MAX_SYM_LEN)) {
		aop_printk("insufficient length buf_len= %d\n", buf_len);
		snprintf(buffer, AOP_MAX_SYM_LEN, "0x%lx", address);
		return;
	}

	*offset = 0;

	/* get the symbol name, module name etc for given kernel address */
	name = kallsyms_lookup(address, &size, offset, &modname, namebuf);

	if (!name)
		/* we require only 11 bytes for storing this, safely assume
		   that buf_len is sufficiently more than 11, considering
		   KSYM_NAME_LEN is itself 127 (normally) and we already
		   checked above */
		snprintf(buffer, AOP_MAX_SYM_LEN, "0x%lx", address);
	else {
		/* Note: the precision values are hard-coded here, any
		   change in the related macro should be corrected here
		   also. */
		if (modname)
			snprintf(buffer, AOP_MAX_SYM_LEN,
				 "[%.10s] %.22s", modname, name);
		else
			snprintf(buffer, AOP_MAX_SYM_LEN, "%.32s", name);
	}

	/* On overflow of buffer, it would have already corrupted
	   memory, but anyway this check may be helpful. BTW, it
	   should never overflow because we already handled that in
	   the beginning. */
	WARN_ON(strlen(buffer) >= buf_len);
}

/* symbol report info sort compare function */
static int __aop_sym_report_cmp(const struct list_head *a,
				const struct list_head *b)
{
	struct aop_sym_list *a_item;
	struct aop_sym_list *b_item;
	unsigned long first_cmd_data;
	unsigned long second_cmd_data;

	a_item = list_entry(a, struct aop_sym_list, sym_list);
	if (!a_item) {
		aop_errk("a_item is NULL\n");
		return 1;
	}

	b_item = list_entry(b, struct aop_sym_list, sym_list);
	if (!b_item) {
		aop_errk("b_item is NULL\n");
		return 1;
	}

	/* as we have only two option we choose if stmt to compare this */
	switch (aop_config_sort_option) {
	case AOP_SORT_BY_VMA:
		first_cmd_data = a_item->addr;
		second_cmd_data = b_item->addr;

		/* to do ascending order */
		if (first_cmd_data < second_cmd_data)
			return -1;
		break;
	case AOP_SORT_BY_SAMPLES:
		first_cmd_data = a_item->samples;
		second_cmd_data = b_item->samples;

		/* to do descending order */
		if (first_cmd_data > second_cmd_data)
			return -1;
		break;

	default:
		aop_printk("sample collection order....\n");
		return 0;	/* to keep the order same as they are collected */
	}

	/* rest of comparision */
	return (first_cmd_data == second_cmd_data) ? 0 : 1;
}

/* create new list head */
struct list_head *__aop_sym_report_alloc_list_head(void)
{
	struct list_head *sym_list_head;

	sym_list_head = (struct list_head *) KDBG_MEM_DBG_KMALLOC
		(KDBG_MEM_REPORT_MODULE, sizeof(struct list_head), GFP_KERNEL);
	if (!sym_list_head) {
		return NULL;
	} else
		return sym_list_head;
}

/* delete all allocated list entry */
static void __aop_sym_report_free_list_entry(struct list_head *plist_head)
{
	struct aop_sym_list *plist;
	struct list_head *pos, *q;

	if (plist_head) {
		list_for_each_safe(pos, q, plist_head) {
			/* loop thru all the nodes and free the allocated memory */
			plist = list_entry(pos, struct aop_sym_list, sym_list);
			if (plist) {
				list_del(pos);

				/* free sym item memory */
				KDBG_MEM_DBG_KFREE(plist);
			}
		}
	}
}

/* delete all allocated list entry */
static int __aop_sym_report_no_of_list_entry(struct list_head *plist_head)
{
	struct aop_sym_list *plist;
	struct list_head *pos;
	int no_of_list_entry = 0;

	if (plist_head) {
		list_for_each(pos, plist_head) {
			/* loop thru all the nodes and free the allocated memory */
			plist = list_entry(pos, struct aop_sym_list, sym_list);
			if (plist)
				no_of_list_entry++;
		}
	}

	return no_of_list_entry;
}

/* Function to sort the symbol list
If the sort option is same as prev, then skip the sorting again */
static void __aop_sym_report_sort(struct list_head *sym_head)
{
	/* aop_sym_list_head is always initialize with null pointer
	   so not needed to verify here */
	aop_list_sort(sym_head, __aop_sym_report_cmp);
}

/* update samples to generate symbol wise report */
static void aop_sym_report_update_sample(struct list_head *func_list_head,
					 struct op_data *aop_data,
					 unsigned int sample, int check_pid)
{
	struct aop_sym_list *new_sym_data = NULL;
	struct aop_sym_list *plist;
	struct list_head *pos;
	aop_cookie_t cookie;	/*coolie value of library */

	/* validate the aop_data before dereference it */
	if (!aop_data || !func_list_head) {
		aop_printk("Invalid arguments\n");
		return;
	}

	/* to compare the cookie value with existing list */
	if (aop_data->in_kernel)
		/* The logic here: if we're in the kernel, the cached cookie is
		 * meaningless (though not the app_cookie if separate_kernel)
		 */
		cookie = NO_COOKIE;
	else
		cookie = aop_data->cookie;

	/* check whether the given pc match with previous kernel img */
	list_for_each(pos, func_list_head) {
		/* loop thru all the nodes */
		plist = list_entry(pos, struct aop_sym_list, sym_list);
		if (plist) {
			/* verify pc is within any of stored kernel range */
			if ((cookie == plist->img_cookie) &&
			    (aop_data->app_cookie == plist->app_cookie) &&
			    ((check_pid) ? (aop_data->tid == plist->tid) : 1) &&
			    (aop_data->pc == plist->addr)) {
				plist->samples += sample;
				return;
			}
		}
	}

	/* symbol data is not exists, so create new node and update it in aop_kernel_list_head */
	new_sym_data = (struct aop_sym_list *)KDBG_MEM_DBG_KMALLOC
	    (KDBG_MEM_REPORT_MODULE, sizeof(struct aop_sym_list), GFP_KERNEL);
	if (!new_sym_data) {
		aop_errk("Add sym data failed: no memory\n");
		return;		/* no memory!! */
	}

	/* assign the sample information to  new sym data */
	new_sym_data->addr = aop_data->pc;
	new_sym_data->in_kernel = aop_data->in_kernel;
	new_sym_data->tid = aop_data->tid;
	new_sym_data->img_cookie = cookie;
	new_sym_data->app_cookie = aop_data->app_cookie;
	new_sym_data->samples = sample;
	list_add_tail(&new_sym_data->sym_list, func_list_head);
}

/*It list the application name and copy the app cookie value for further process */
static int aop_show_application_img(aop_cookie_t **app_cookie)
{
	aop_image_list *tmp_app_data = aop_app_list_head;
	int count = 0, i = 0;
	aop_cookie_t *tmp_app_cookie;

	/* loop thru app data and show the application name */
	while (tmp_app_data) {
		char app_name[DNAME_INLINE_LEN + 1];

		if (!count) {
			PRINT_KD("\n\n");
			PRINT_KD("\tIndex Application Images\n");
			PRINT_KD("------------------------------------\n");
		}

		/* get the application name and show to user */
		aop_decode_cookie_without_path(tmp_app_data->cookie_value,
					       app_name, DNAME_INLINE_LEN);
		PRINT_KD("\t[%d] - %s\n", count + 1, app_name);
		tmp_app_data = tmp_app_data->next;
		count++;
	}

	/* to show kernel names for the all application */
	tmp_app_data = aop_app_list_head;
	while (tmp_app_data) {
		char app_name[DNAME_INLINE_LEN + 1];

		/* get the application name and show to user */
		aop_decode_cookie_without_path(tmp_app_data->cookie_value,
					       app_name, DNAME_INLINE_LEN);
		PRINT_KD("\t[%d] - %s (KERNEL)\n", count + i + 1, app_name);
		tmp_app_data = tmp_app_data->next;
		i++;
	}

	/* allocate memory for app_cookie to store appl img cookie value */
	if (count) {
		/* Not need to verify before copy data to *app_cookie
		   (as app_cookie point to address of the caller functions local variable
		   not needed to verify it here) */
		*app_cookie =
		    (aop_cookie_t *)
		    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					 count * sizeof(aop_cookie_t),
					 GFP_KERNEL);
		if (!*app_cookie) {
			aop_errk("No memory\n");
			return 0;
		}
		PRINT_KD("------------------------------------\n\n");
	} else {
		PRINT_KD("No Application Image Found!!!\n");
	}

	/* copy the pointer  to tmp, to fill the app cookie value */
	tmp_app_cookie = *app_cookie;

	/* get list head to copy app cookie value */
	tmp_app_data = aop_app_list_head;
	i = 0;
	while (tmp_app_data) {
		/* as the max no of items to copy is calculated earlier,
		   memory verification is not done here */
		*(tmp_app_cookie + i) = tmp_app_data->cookie_value;
		tmp_app_data = tmp_app_data->next;
		i++;
	}

	/* return the no of application image found with kernel */
	return count + count;
}

/* build system wide function name & samples */
static struct list_head *aop_sym_report_build_function_samples(void)
{
	if (aop_sym_elf_list_head) {

		if (aop_config_sort_option != aop_prev_sort_option) {
			__aop_sym_report_sort(aop_sym_elf_list_head);

			/* update the current sort option */
			aop_prev_sort_option = aop_config_sort_option;
		}
		return aop_sym_elf_list_head;
	} else {
		struct aop_sym_list *plist;
		struct list_head *pos;
		char img_name[DNAME_INLINE_LEN + 1];
		char *func_name;
		int list_index = 1, total_no_of_entries =
		    __aop_sym_report_no_of_list_entry(aop_sym_list_head);
		int prev_per = 0;

		BUG_ON(!total_no_of_entries);

		/* Below written block is never executed, as BUG_ON will be called if
		 * no entries existed only to remove prevent warning this code is inserted*/
		if (!total_no_of_entries) {
			aop_printk("List Empty\n");
			return aop_sym_list_head;
		}

		/* rebuild new list with function name wise samples */
		aop_sym_elf_list_head =  __aop_sym_report_alloc_list_head();
		if (!aop_sym_elf_list_head) {
			aop_printk("out of memory - [aop_sym_elf_list_head]\n");
			return aop_sym_list_head;
		}

		func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
							 AOP_MAX_SYM_NAME_LENGTH,
							 GFP_KERNEL);
		if (!func_name) {
			aop_errk("func_name: no memory\n");
			KDBG_MEM_DBG_KFREE(aop_sym_elf_list_head);
			return aop_sym_list_head;
		}

		INIT_LIST_HEAD(aop_sym_elf_list_head);

		/* update function wise samples */
		list_for_each(pos, aop_sym_list_head) {
			/* loop thru all the nodes */
			plist = list_entry(pos, struct aop_sym_list, sym_list);
			if (plist) {
				unsigned long offset = 0;
				unsigned int sym_addr = plist->addr;
				struct op_data aop_data;

				if (aop_sym_elf_load_done) {
					int per =
					    ((list_index * 100) /
					     total_no_of_entries);
					if (!(per % 10) && (prev_per != per)) {
						prev_per = per;
						PRINT_KD
						    ("Creating Database...%d%%\r",
						     per);
					}
					list_index++;
				}

				/* get the function name of the given PC & elf file */
				func_name[0] = '\0';
				if (plist->in_kernel) {
					aop_decode_cookie_kernel_symbol
					    (func_name, AOP_MAX_SYM_NAME_LENGTH,
					     plist->addr, &offset);
					sym_addr = plist->addr - offset;
				} else {
					struct aop_symbol_info symbol_info;
					/* get library image name */
					aop_decode_cookie_without_path(plist->
								       img_cookie,
								       img_name,
								       DNAME_INLINE_LEN);
					symbol_info.pfunc_name = func_name;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
					symbol_info.df_info_flag = 0;
					symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
					kdbg_elf_get_symbol(img_name,
							    plist->addr,
							    AOP_MAX_SYM_NAME_LENGTH,
							    &symbol_info);
					sym_addr = symbol_info.start_addr;
#else
					sym_addr = plist->addr;
#endif /* CONFIG_ELF_MODULE */
				}

				/* assign the new addr & other member value to aop data */
				aop_data.pc = sym_addr;
				aop_data.in_kernel = plist->in_kernel;
				aop_data.tid = plist->tid;
				aop_data.cookie = plist->img_cookie;
				aop_data.app_cookie = plist->app_cookie;

				aop_sym_report_update_sample
				    (aop_sym_elf_list_head, &aop_data,
				     plist->samples, 0);
			}
		}

		/* to sort the symbol report before print */
		__aop_sym_report_sort(aop_sym_elf_list_head);

		if (aop_sym_elf_load_done)
			PRINT_KD("\n");

		KDBG_MEM_DBG_KFREE(func_name);

		return aop_sym_elf_list_head;
	}
}

/* aop symbol report function which shows per application & library names
if show_lib is AOP_SHOW_FUNCTION_N_LIB, then show application wise sample
including library name otherwise just show function names of the selected application
*/
static int aop_sym_report_internel(int show_lib)
{
	struct aop_sym_list *plist;
	struct list_head *pos;
	unsigned int perc, total_samples = 0;
	char img_name[DNAME_INLINE_LEN + 1];
	char app_name[DNAME_INLINE_LEN + 1];
	int no_of_appl_img, no_of_org_appl_img, choice;
	aop_cookie_t *app_cookie = NULL;
	int appl_index, kernel;
	char *func_name, *d_fname;
	struct list_head *func_list_head;


	if (!aop_nr_total_samples) {
		PRINT_KD("No Samples found\n");
		return 1;
	}

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 AOP_MAX_SYM_NAME_LENGTH,
						 GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					       AOP_MAX_SYM_NAME_LENGTH,
					       GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		KDBG_MEM_DBG_KFREE(func_name);
		return 1;
	}

	/* list appl img and find the total no of appl img profiled */
	no_of_appl_img = aop_show_application_img(&app_cookie);
	if (!no_of_appl_img) {
		aop_printk("No sample or memory\n");
		goto __END;
	}

	no_of_org_appl_img = no_of_appl_img / 2;

	/* list all application img and take copy of that dcookie */
	PRINT_KD("Select the Appl Image Index ==> ");
	choice = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD("\n");

	/* validate the choice */
	if ((choice < 1) || (choice > no_of_appl_img)) {
		PRINT_KD("Invalid application image index no\n");
		goto __END;
	}

	choice--;
	appl_index = choice % no_of_org_appl_img;
	kernel = choice / no_of_org_appl_img;

	/* get the user selection application name */
	aop_decode_cookie_without_path(*(app_cookie + appl_index), app_name,
				       DNAME_INLINE_LEN);

	func_list_head = aop_sym_report_build_function_samples();

	/* to draw header text */
	PRINT_KD("\n\n");
	PRINT_KD("Symbol profiling for application \"%s%s\" %s:\n",
		 app_name, ((kernel) ? "(KERNEL)" : " "),
		 ((show_lib ==
		   AOP_SHOW_FUNCTION_N_LIB) ? "including library " : " "));
	if (show_lib == AOP_SHOW_FUNCTION_N_LIB)
		PRINT_KD("vma\t\tSamples  %%  image name\t\tsymbol name");
	else
		PRINT_KD("vma\t\tSamples  %%  symbol name");
	PRINT_KD("\n");
	PRINT_KD("---------------------------------------------------"
		 "-----------------------\n");

	/* print symbol wise samples */
	list_for_each(pos, func_list_head) {
		/* loop thru all the nodes */
		plist = list_entry(pos, struct aop_sym_list, sym_list);
		if (plist) {
			int show_sample = 1;
			unsigned int sym_addr = 0, virtual_addr = 0;
			struct aop_symbol_info symbol_info;
			unsigned long offset = 0;
			perc = ((plist->samples * 100) / aop_nr_total_samples);

			/* verify application is same as user selected application */
			if (plist->app_cookie == *(app_cookie + appl_index)) {
				/* get the app name */
				aop_decode_cookie_without_path(plist->
							       app_cookie,
							       app_name,
							       DNAME_INLINE_LEN);

				/* get the function name of the given PC & elf file */
				d_fname[0] = '\0';
				func_name[0] = '\0';

				if (plist->in_kernel) {
					if (!kernel)
						show_sample = 0;	/* don't show samples */
					else {
						snprintf(img_name,
							 DNAME_INLINE_LEN,
							 "[KERNEL]");

						/* get symbol name from kallsymbol */
						aop_decode_cookie_kernel_symbol
						    (d_fname,
						     AOP_MAX_SYM_NAME_LENGTH,
						     plist->addr, &offset);
						snprintf(func_name,
							 AOP_MAX_SYM_NAME_LENGTH,
							 d_fname);
					}
				} else {
					if (kernel)
						show_sample = 0;	/* don't show samples */
					else {
						/* get the img name */
						aop_decode_cookie_without_path
						    (plist->img_cookie,
						     img_name,
						     DNAME_INLINE_LEN);
						symbol_info.pfunc_name =
						    func_name;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
						symbol_info.df_info_flag = 0;
						symbol_info.pdf_info = NULL;
#endif
						if (kdbg_elf_get_symbol
						    (img_name, plist->addr,
						     AOP_MAX_SYM_NAME_LENGTH,
						     &symbol_info) != 0) {
							/* get symbol name from elf file */
							snprintf(d_fname,
								 AOP_MAX_SYM_NAME_LENGTH,
								 "???");
							snprintf(func_name,
								 AOP_MAX_SYM_NAME_LENGTH,
								 d_fname);
						} else if (aop_config_demangle) {
							kdbg_elf_sym_demangle
							    (func_name, d_fname,
							     AOP_MAX_SYM_NAME_LENGTH);
						}
						sym_addr =
						    symbol_info.start_addr;
						virtual_addr =
						    symbol_info.virt_addr;
#else
						snprintf(d_fname,
							 AOP_MAX_SYM_NAME_LENGTH,
							 "???");
						snprintf(func_name,
							 AOP_MAX_SYM_NAME_LENGTH,
							 d_fname);
						sym_addr = plist->addr;
#endif /* CONFIG_ELF_MODULE */
					}
				}

				if (show_sample) {

					/* if report option with library then show library */
					if (show_lib == AOP_SHOW_FUNCTION_N_LIB) {
						PRINT_KD("\n");
						PRINT_KD
						    ("%08x%s %8u   %3u%% %-20s %s",
						     (plist->addr +
						      virtual_addr),
						     AOP_KERNEL_IDENTIFICATION
						     (plist->in_kernel),
						     plist->samples, perc,
						     img_name,
						     (aop_config_demangle) ?
						     d_fname : func_name);
					} else {
						/* show sample without library name */
						PRINT_KD("\n");
						PRINT_KD
						    ("%08x%s %8u   %3u%% %s",
						     plist->addr + virtual_addr,
						     AOP_KERNEL_IDENTIFICATION
						     (plist->in_kernel),
						     plist->samples, perc,
						     (aop_config_demangle) ?
						     d_fname : func_name);
					}

					/* sum the samples of particular application */
					total_samples += plist->samples;
				}
			}

		}
	}
	PRINT_KD("\n");
	PRINT_KD("Total Samples found %d\n", total_samples);

__END:

	/* free all allocated resouce */
	KDBG_MEM_DBG_KFREE(func_name);
	KDBG_MEM_DBG_KFREE(d_fname);
	if (app_cookie)
		KDBG_MEM_DBG_KFREE(app_cookie);

	return 0;
}

#ifdef CONFIG_ELF_MODULE
/* elf symbol load/unload notification callback function */
void aop_sym_elf_load_notification_callback(int elf_load_flag)
{
	aop_printk("Elf database%sloaded\n", (elf_load_flag) ? " " : " un");

	/* this variable is not used now, but in future it may be used, so i keep it */
	aop_sym_elf_load_done = elf_load_flag;

	/* if elf database is modified we need to create new list, so free the elf list data */
	if (aop_sym_elf_list_head) {
		__aop_sym_report_free_list_entry(aop_sym_elf_list_head);
		KDBG_MEM_DBG_KFREE(aop_sym_elf_list_head);
		aop_sym_elf_list_head = NULL;
	}
}
#endif /* CONFIG_ELF_MODULE */

/* free allocated symbo data */
void aop_sym_report_free_sample_data(void)
{
	__aop_sym_report_free_list_entry(aop_sym_list_head);

	if (aop_sym_elf_list_head) {
		__aop_sym_report_free_list_entry(aop_sym_elf_list_head);
		KDBG_MEM_DBG_KFREE(aop_sym_elf_list_head);
		aop_sym_elf_list_head = NULL;
	}

	if (aop_sym_list_head) {
		KDBG_MEM_DBG_KFREE(aop_sym_list_head);
		aop_sym_list_head = NULL;
	}

}

/* initialize apo symbol report head list */
int aop_sym_report_init(void)
{
	aop_sym_list_head = __aop_sym_report_alloc_list_head();
	if (!aop_sym_list_head) {
		aop_printk("out of memory - [aop_sym_list_head]\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(aop_sym_list_head);
	return 0;
}

/* update all samples to generate symbol wise report */
void aop_sym_report_update_sample_data(struct op_data *aop_data)
{
	return aop_sym_report_update_sample(aop_sym_list_head, aop_data, 1, 1);
}

/* Dump the application wise function name including library name */
int aop_sym_report_per_application_n_lib(void)
{
	/* to report per application wise function name */
	aop_sym_report_internel(AOP_SHOW_FUNCTION_N_LIB);

	return 1;		/* to show menu */
}

/*Report the symbol information of the image(application or library) specified
by image_type. This will print only the user context samples and not the kernel one.*/
int aop_sym_report_per_image_user_samples(int image_type,
					  aop_cookie_t img_cookie)
{
	int perc;
	aop_cookie_t cookie = NO_COOKIE;
	char img_name[DNAME_INLINE_LEN + 1];
	struct aop_sym_list *tmp_sym_data;
	struct list_head *pos;
	char *func_name, *d_fname;
	struct list_head *func_list_head;
	unsigned int total_samples = 0;
	struct aop_symbol_info symbol_info;

	if (!aop_nr_total_samples) {
		PRINT_KD("No Samples found\n");
		return 1;
	}

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 AOP_MAX_SYM_NAME_LENGTH,
						 GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					       AOP_MAX_SYM_NAME_LENGTH,
					       GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		KDBG_MEM_DBG_KFREE(func_name);
		return 1;
	}

	func_list_head = aop_sym_report_build_function_samples();
	PRINT_KD("\n");
	PRINT_KD("vma\t    Samples   %%  symbol name\n");
	PRINT_KD("------------------------------------------------\n");

	list_for_each(pos, func_list_head) {
		/* loop thru all the nodes */
		tmp_sym_data = list_entry(pos, struct aop_sym_list, sym_list);
		if (tmp_sym_data) {
			if (image_type == 0) {
				cookie = tmp_sym_data->app_cookie;
			} else if (image_type == 1) {
				cookie = tmp_sym_data->img_cookie;
			} else {
				PRINT_KD("\n");
				PRINT_KD("Invalid Image Type Requested...\n");
				KDBG_MEM_DBG_KFREE(func_name);
				KDBG_MEM_DBG_KFREE(d_fname);
				return -1;
			}
			if (cookie == img_cookie
			    && tmp_sym_data->in_kernel == 0) {
				/* get appl name */
				aop_decode_cookie_without_path(tmp_sym_data->
							       img_cookie,
							       img_name,
							       DNAME_INLINE_LEN);
				func_name[0] = '\0';
				symbol_info.pfunc_name = func_name;
				d_fname[0] = '\0';

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				if (kdbg_elf_get_symbol
				    (img_name, tmp_sym_data->addr,
				     AOP_MAX_SYM_NAME_LENGTH,
				     &symbol_info) != 0) {
					strncpy(d_fname, "???",
						AOP_MAX_SYM_NAME_LENGTH);
					strncpy(func_name, d_fname,
						AOP_MAX_SYM_NAME_LENGTH);
					d_fname[AOP_MAX_SYM_NAME_LENGTH - 1] =
					    '\0';
				} else if (aop_config_demangle) {
					kdbg_elf_sym_demangle(func_name,
							      d_fname,
							      AOP_MAX_SYM_NAME_LENGTH);
					d_fname[AOP_MAX_SYM_NAME_LENGTH - 1] =
					    '\0';
				}
#else
				strncpy(d_fname, "???",
					AOP_MAX_SYM_NAME_LENGTH);
				strncpy(func_name, d_fname,
					AOP_MAX_SYM_NAME_LENGTH);
				d_fname[AOP_MAX_SYM_NAME_LENGTH - 1] = '\0';
				symbol_info.virt_addr = 0;
#endif /* CONFIG_ELF_MODULE */

				func_name[AOP_MAX_SYM_NAME_LENGTH - 1] = '\0';

				perc =
				    ((tmp_sym_data->samples * 100) /
				     aop_nr_total_samples);
				PRINT_KD("%08x %8u   %3u%% %s\n",
					 (tmp_sym_data->addr +
					  symbol_info.virt_addr),
					 tmp_sym_data->samples, perc,
					 (aop_config_demangle) ? d_fname :
					 func_name);
				total_samples += tmp_sym_data->samples;
			}
		}
	}

	PRINT_KD("Total Samples %d\n", total_samples);

	KDBG_MEM_DBG_KFREE(func_name);
	KDBG_MEM_DBG_KFREE(d_fname);
	return 0;
}

/*Report the symbol information of the thread ID(TID) specified by user.*/
int aop_sym_report_per_tid(pid_t pid_user)
{
	int perc;
	unsigned int sym_addr;
	unsigned int virt_addr = 0;
	char img_name[DNAME_INLINE_LEN + 1];
	struct aop_sym_list *tmp_sym_data;
	struct list_head *pos;
	char *func_name, *d_fname;
	struct list_head *func_list_head;
	int report_found = 0;
	int list_index = 1, total_no_of_entries =
	    __aop_sym_report_no_of_list_entry(aop_sym_list_head);
	int prev_per = 0;
	unsigned int total_samples = 0;
	struct aop_symbol_info symbol_info;


	if (!total_no_of_entries) {
		aop_errk("List empty\n");
		return 1;
	}

	if (!aop_nr_total_samples) {
		PRINT_KD("No Samples found\n");
		return 1;
	}

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 AOP_MAX_SYM_NAME_LENGTH,
						 GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					       AOP_MAX_SYM_NAME_LENGTH,
					       GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		KDBG_MEM_DBG_KFREE(func_name);
		return 1;
	}

	/* rebuild new list with function name wise samples */
	func_list_head = __aop_sym_report_alloc_list_head();
	if (!func_list_head) {
		aop_errk("out of memory - [func_list_head]\n");
		KDBG_MEM_DBG_KFREE(func_name);
		KDBG_MEM_DBG_KFREE(d_fname);
		return 1;
	}

	INIT_LIST_HEAD(func_list_head);

	/* update function wise samples */
	list_for_each(pos, aop_sym_list_head) {
		if (aop_sym_elf_load_done) {
			int per = ((list_index * 100) / total_no_of_entries);
			if (!(per % 10) && (prev_per != per)) {
				prev_per = per;
				PRINT_KD("Creating Database...%d%%\r", per);
			}
			list_index++;
		}

		/* loop thru all the nodes */
		tmp_sym_data = list_entry(pos, struct aop_sym_list, sym_list);
		if (tmp_sym_data && (tmp_sym_data->tid == pid_user)) {
			unsigned long offset = 0;
			unsigned int sym_addr = tmp_sym_data->addr;
			struct aop_sym_list *plist;
			struct aop_sym_list *new_sym_data;
			int add_new_entry = 1;
			struct list_head *pos1;

			/* get the function name of the given PC & elf file */
			func_name[0] = '\0';
			if (tmp_sym_data->in_kernel) {
				aop_decode_cookie_kernel_symbol(func_name,
								AOP_MAX_SYM_NAME_LENGTH,
								tmp_sym_data->
								addr, &offset);
				sym_addr = tmp_sym_data->addr - offset;
			} else {
				struct aop_symbol_info symbol_info;
				symbol_info.pfunc_name = func_name;
				/* get library image name */
				aop_decode_cookie_without_path(tmp_sym_data->
							       img_cookie,
							       img_name,
							       DNAME_INLINE_LEN);
#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				kdbg_elf_get_symbol(img_name,
						    tmp_sym_data->addr,
						    AOP_MAX_SYM_NAME_LENGTH,
						    &symbol_info);
				sym_addr = symbol_info.start_addr;
				virt_addr = symbol_info.virt_addr;
#else
				sym_addr = tmp_sym_data->addr;
#endif /* CONFIG_ELF_MODULE */
			}

			/* check whether the given pc match with previous kernel img */
			list_for_each(pos1, func_list_head) {
				/* loop thru all the nodes */
				plist =
				    list_entry(pos1, struct aop_sym_list,
					       sym_list);
				if (plist) {
					if ((tmp_sym_data->img_cookie ==
					     plist->img_cookie)
					    && (tmp_sym_data->app_cookie ==
						plist->app_cookie)
					    && (sym_addr == plist->addr)
					    && (tmp_sym_data->tid ==
						plist->tid)) {
						plist->samples +=
						    tmp_sym_data->samples;
						add_new_entry = 0;
						break;
					}
				}
			}

			/* symbol data is not exists, so create new node and update it */
			if (add_new_entry) {
				new_sym_data =
				    (struct aop_sym_list *)
				    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
							 sizeof(struct
								aop_sym_list),
							 GFP_KERNEL);
				if (new_sym_data) {
					/* assign the sample information to  new sym data */
					new_sym_data->addr = sym_addr;
					new_sym_data->in_kernel =
					    tmp_sym_data->in_kernel;
					new_sym_data->tid = tmp_sym_data->tid;
					new_sym_data->img_cookie =
					    tmp_sym_data->img_cookie;
					new_sym_data->app_cookie =
					    tmp_sym_data->app_cookie;
					new_sym_data->samples =
					    tmp_sym_data->samples;
					list_add_tail(&new_sym_data->sym_list,
						      func_list_head);
				}
			}

			report_found++;
		}
	}

	if (!report_found) {
		PRINT_KD("Given PID is not valid...\n");
		goto __END;
	}

	/* to sort the symbol report before print */
	__aop_sym_report_sort(func_list_head);

	if (aop_sym_elf_load_done)
		PRINT_KD("\n");

	PRINT_KD("vma\t\tSamples  %% image name\t\tsymbol name\n");
	PRINT_KD("-------------------------------------------------------------"
		 "------------\n");

	list_for_each(pos, func_list_head) {
		/* loop thru all the nodes */
		tmp_sym_data = list_entry(pos, struct aop_sym_list, sym_list);
		if (tmp_sym_data) {
			unsigned long offset = 0;
			d_fname[0] = '\0';
			func_name[0] = '\0';

			if (tmp_sym_data->in_kernel) {
				/* for kernel samples the image name must be KERNEL */
				snprintf(img_name, DNAME_INLINE_LEN,
					 "[KERNEL]");

				aop_decode_cookie_kernel_symbol(d_fname,
								AOP_MAX_SYM_NAME_LENGTH,
								tmp_sym_data->
								addr, &offset);
				snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH,
					 d_fname);
			} else {
				/* get appl name */
				aop_decode_cookie_without_path(tmp_sym_data->
							       img_cookie,
							       img_name,
							       DNAME_INLINE_LEN);

				symbol_info.pfunc_name = func_name;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				if (kdbg_elf_get_symbol
				    (img_name, tmp_sym_data->addr,
				     AOP_MAX_SYM_NAME_LENGTH,
				     &symbol_info) != 0) {
					snprintf(d_fname,
						 AOP_MAX_SYM_NAME_LENGTH,
						 "???");
					snprintf(func_name,
						 AOP_MAX_SYM_NAME_LENGTH,
						 d_fname);
				} else if (aop_config_demangle) {
					kdbg_elf_sym_demangle(func_name,
							      d_fname,
							      AOP_MAX_SYM_NAME_LENGTH);
				}
				sym_addr = symbol_info.start_addr;
				virt_addr = symbol_info.virt_addr;
#else
				sym_addr = tmp_sym_data->addr;
				virt_addr = 0;
#endif /* CONFIG_ELF_MODULE */
			}

			perc =
			    ((tmp_sym_data->samples * 100) /
			     aop_nr_total_samples);
			PRINT_KD("%08x%s %8u   %3u%% %-20s %s\n",
				 (tmp_sym_data->addr + virt_addr),
				 AOP_KERNEL_IDENTIFICATION(tmp_sym_data->
							   in_kernel),
				 tmp_sym_data->samples, perc, img_name,
				 (aop_config_demangle) ? d_fname : func_name);

			total_samples += tmp_sym_data->samples;
		}
	}

	PRINT_KD("Total Samples %d\n", total_samples);

__END:
	KDBG_MEM_DBG_KFREE(func_name);
	KDBG_MEM_DBG_KFREE(d_fname);
	__aop_sym_report_free_list_entry(func_list_head);
	KDBG_MEM_DBG_KFREE(func_list_head);

	return 0;
}

/* dump system wide function name & samples */
int aop_sym_report_system_wide_function_samples(void)
{
	struct aop_sym_list *plist;
	struct list_head *pos;
	unsigned int total_samples = 0;
	char img_name[DNAME_INLINE_LEN + 1];
	char *func_name, *d_fname;
	struct list_head *func_list_head;
	int list_index = 1, total_no_of_entries =
	    __aop_sym_report_no_of_list_entry(aop_sym_list_head);
	int prev_per = 0;
	struct aop_symbol_info symbol_info = { 0,};

	if (!aop_nr_total_samples) {
		PRINT_KD("No Samples found\n");
		return 1;
	}
	if (!total_no_of_entries) {
		PRINT_KD("No Entries found\n");
		return 1;
	}


	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 AOP_MAX_SYM_NAME_LENGTH,
						 GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					       AOP_MAX_SYM_NAME_LENGTH,
					       GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		KDBG_MEM_DBG_KFREE(func_name);
		return 1;
	}

	/* rebuild new list with function name wise samples */
	func_list_head = __aop_sym_report_alloc_list_head();
	if (!func_list_head) {
		aop_errk("out of memory - [func_list_head]\n");
		KDBG_MEM_DBG_KFREE(func_name);
		KDBG_MEM_DBG_KFREE(d_fname);
		return 1;
	}

	INIT_LIST_HEAD(func_list_head);

	/* update function wise samples */
	list_for_each(pos, aop_sym_list_head) {
		if (aop_sym_elf_load_done) {
			int per = ((list_index * 100) / total_no_of_entries);
			if (!(per % 10) && (prev_per != per)) {
				prev_per = per;
				PRINT_KD("Creating Database...%d%%\r", per);
			}
			list_index++;
		}

		/* loop thru all the nodes */
		plist = list_entry(pos, struct aop_sym_list, sym_list);
		if (plist) {
			unsigned long offset = 0;
			unsigned int sym_addr = plist->addr;
			unsigned int virtual_addr = 0;
			struct op_data aop_data;

			/* get the function name of the given PC & elf file */
			func_name[0] = '\0';
			if (plist->in_kernel) {
				aop_decode_cookie_kernel_symbol(func_name,
								AOP_MAX_SYM_NAME_LENGTH,
								plist->addr,
								&offset);
				sym_addr = plist->addr - offset;
				aop_data.cookie = 0;
				aop_data.app_cookie = 0;
			} else {
				aop_data.cookie = plist->img_cookie;
				aop_data.app_cookie = plist->app_cookie;
				/* get library image name */
				aop_decode_cookie_without_path(plist->
							       img_cookie,
							       img_name,
							       DNAME_INLINE_LEN);
				symbol_info.pfunc_name = func_name;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				kdbg_elf_get_symbol(img_name, plist->addr,
						    AOP_MAX_SYM_NAME_LENGTH,
						    &symbol_info);
				sym_addr = symbol_info.start_addr;
				virtual_addr = symbol_info.virt_addr;
#else
				sym_addr = plist->addr;
				virtual_addr = 0;
#endif /* CONFIG_ELF_MODULE */
			}

			/* assign the new addr & other member value to aop data */
			aop_data.pc = sym_addr;
			aop_data.in_kernel = plist->in_kernel;
			aop_data.tid = 0;

			aop_sym_report_update_sample(func_list_head,
						     &aop_data, plist->samples,
						     0);
		}
	}

	/* to sort the symbol report before print */
	__aop_sym_report_sort(func_list_head);

	if (aop_sym_elf_load_done)
		PRINT_KD("\n");

	PRINT_KD("vma\t\tSamples   %%   image name\t   symbol name\n");
	PRINT_KD
	    ("--------------------------------------------------------------\n");

	total_samples = 0;
	/* print symbol wise samples */
	list_for_each(pos, func_list_head) {
		/* loop thru all the nodes */
		plist = list_entry(pos, struct aop_sym_list, sym_list);
		if (plist) {
			unsigned int sym_addr = 0, virt_addr = 0;
			int perc =
			    ((plist->samples * 100) / aop_nr_total_samples);

			/* get the function name of the given PC & elf file */
			d_fname[0] = '\0';
			func_name[0] = '\0';
			if (plist->in_kernel) {
				unsigned long offset = 0;

				/* for kernel samples the image name must be KERNEL */
				snprintf(img_name, DNAME_INLINE_LEN,
					 "[KERNEL]");

				aop_decode_cookie_kernel_symbol(d_fname,
								AOP_MAX_SYM_NAME_LENGTH,
								plist->addr,
								&offset);
				snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH,
					 d_fname);
			} else {
				/* get library image name */
				aop_decode_cookie_without_path(plist->
							       img_cookie,
							       img_name,
							       DNAME_INLINE_LEN);

				symbol_info.pfunc_name = func_name;
#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				if (kdbg_elf_get_symbol(img_name, plist->addr,
							AOP_MAX_SYM_NAME_LENGTH,
							&symbol_info) != 0) {
					snprintf(d_fname,
						 AOP_MAX_SYM_NAME_LENGTH,
						 "???");
					snprintf(func_name,
						 AOP_MAX_SYM_NAME_LENGTH,
						 d_fname);
				} else if (aop_config_demangle) {
					kdbg_elf_sym_demangle(func_name,
							      d_fname,
							      AOP_MAX_SYM_NAME_LENGTH);
				}
				sym_addr = symbol_info.start_addr;
				virt_addr = symbol_info.virt_addr;
#else
				sym_addr = plist->addr;
#endif /* CONFIG_ELF_MODULE */

			}

			PRINT_KD("%08x%s %8u	 %3u%% %-20s %s\n",
				 (plist->addr + virt_addr),
				 AOP_KERNEL_IDENTIFICATION(plist->in_kernel),
				 plist->samples, perc, img_name,
				 (aop_config_demangle) ? d_fname : func_name);

			total_samples += plist->samples;
		}
	}
	PRINT_KD("Total Samples %d\n", total_samples);

	KDBG_MEM_DBG_KFREE(func_name);
	KDBG_MEM_DBG_KFREE(d_fname);

	__aop_sym_report_free_list_entry(func_list_head);
	KDBG_MEM_DBG_KFREE(func_list_head);

	return 0;
}

/* dump system wide function name & samples */
int aop_sym_report_system_wide_samples(void)
{
	struct aop_sym_list *plist;
	struct list_head *pos;
	unsigned int total_samples = 0;
	char img_name[DNAME_INLINE_LEN + 1];
	char *func_name, *d_fname;
	struct aop_symbol_info symbol_info = { 0,};

	if (!aop_nr_total_samples) {
		PRINT_KD("No Samples found\n");
		return 1;
	}

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
						 AOP_MAX_SYM_NAME_LENGTH,
						 GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
					       AOP_MAX_SYM_NAME_LENGTH,
					       GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		KDBG_MEM_DBG_KFREE(func_name);
		return 1;
	}

	/* to sort the symbol report before print */
	__aop_sym_report_sort(aop_sym_list_head);

	PRINT_KD("vma\t\tSamples   %%   image name\t   symbol name\n");
	PRINT_KD
	    ("--------------------------------------------------------------\n");

	/* print symbol wise samples */
	list_for_each(pos, aop_sym_list_head) {
		/* loop thru all the nodes */
		plist = list_entry(pos, struct aop_sym_list, sym_list);
		if (plist) {
			unsigned int sym_addr = 0, virt_addr = 0;
			int perc =
			    ((plist->samples * 100) / aop_nr_total_samples);
			unsigned long offset = 0;

			/* get the function name of the given PC & elf file */
			d_fname[0] = '\0';
			func_name[0] = '\0';
			if (plist->in_kernel) {
				/* for kernel samples the image name must be KERNEL */
				snprintf(img_name, DNAME_INLINE_LEN,
					 "[KERNEL]");

				aop_decode_cookie_kernel_symbol(d_fname,
								AOP_MAX_SYM_NAME_LENGTH,
								plist->addr,
								&offset);
				snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH,
					 d_fname);
			} else {
				/* get library image name */
				aop_decode_cookie_without_path(plist->
							       img_cookie,
							       img_name,
							       DNAME_INLINE_LEN);

				symbol_info.pfunc_name = func_name;
				sym_addr = 0;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				if (kdbg_elf_get_symbol(img_name, plist->addr,
							AOP_MAX_SYM_NAME_LENGTH,
							&symbol_info) != 0) {
					snprintf(d_fname,
						 AOP_MAX_SYM_NAME_LENGTH,
						 "???");
					snprintf(func_name,
						 AOP_MAX_SYM_NAME_LENGTH,
						 d_fname);
				} else if (aop_config_demangle) {
					kdbg_elf_sym_demangle(func_name,
							      d_fname,
							      AOP_MAX_SYM_NAME_LENGTH);
				}

				sym_addr = symbol_info.start_addr;
				virt_addr = symbol_info.virt_addr;
#else
				sym_addr = plist->addr;
#endif /* CONFIG_ELF_MODULE */

				if (sym_addr <= plist->addr)
					offset = plist->addr - sym_addr;
			}

			PRINT_KD("%08x%s %8u	 %3u%% %-20s %s+0x%lx\n",
				 plist->addr + virt_addr,
				 AOP_KERNEL_IDENTIFICATION(plist->in_kernel),
				 plist->samples, perc, img_name,
				 (aop_config_demangle) ? d_fname : func_name,
				 offset);

			total_samples += plist->samples;
		}
	}
	PRINT_KD("Total Samples %d\n", total_samples);

	KDBG_MEM_DBG_KFREE(func_name);
	KDBG_MEM_DBG_KFREE(d_fname);

	return 0;
}

/* Callgraph */
int  aop_cp_add_start_addr(aop_caller_list *data)
{
	char lib_name[DNAME_INLINE_LEN+1];
	char *func_name;
	struct aop_symbol_info  symbol_info = {0,};

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
			AOP_MAX_SYM_NAME_LENGTH, GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	while (data) {
		unsigned int sym_addr = 0;
		if (data->in_kernel) {
			data->start_addr = data->pc;
		} else {
			/* get library image name */
			aop_decode_cookie_without_path(data->cookie,
					lib_name, DNAME_INLINE_LEN);
			symbol_info.pfunc_name = func_name;
			sym_addr = 0;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
			symbol_info.df_info_flag = 0;
			symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
			if (kdbg_elf_get_symbol(lib_name, data->pc, AOP_MAX_SYM_NAME_LENGTH, &symbol_info) < 0)
					aop_printk("kdbg_elf_get_symbol failed\n");

			data->start_addr = sym_addr = symbol_info.start_addr;
#else
			data->start_addr = data->pc;
#endif /* CONFIG_ELF_MODULE */
		}
		data = data->caller;
	}
	KDBG_MEM_DBG_KFREE(func_name);
	return 0;
}
extern int aop_cp_item_idx;

static char *aop_convert_taskname(const char *comm, char *out)
{
	int i = 0;

	if (!comm || !out)
		return NULL;

	while (comm[i] && i < TASK_COMM_LEN) {
		if (isprint (comm[i]))
			out[i] = comm[i];
		else
			out[i] = '.';
		i++;
	}

	if (i < TASK_COMM_LEN)
		out[i] = '\0';
	else
		out[TASK_COMM_LEN-1] = '\0';

	return out;
}

int  aop_cp_report_process_func_wise(aop_caller_list *data, int index, int cp_show_option)
{
	char lib_name[DNAME_INLINE_LEN+1];
	char *func_name, *d_fname;
	struct aop_symbol_info  symbol_info = {0,};
	int cp_flag = 0;
#ifdef CONFIG_DWARF_MODULE
	static struct aop_df_info df_info;
#endif
	/* thread_name can be taken in stack also,
	no issues with malloc as its not in data path */
	char *thread_name = NULL;
	char out[TASK_COMM_LEN] = {0,};
	char print_name[12];


	thread_name =
		(char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
				TASK_COMM_LEN, GFP_KERNEL);
	if (!thread_name) {
		aop_errk("tmp_tgid_data->thread_name: no memory\n");
		return -1;	/* no memory!! */
	}

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
			AOP_MAX_SYM_NAME_LENGTH, GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		if (thread_name)
			KDBG_MEM_DBG_KFREE(thread_name);
		return -1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
			AOP_MAX_SYM_NAME_LENGTH, GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		if (thread_name)
			KDBG_MEM_DBG_KFREE(thread_name);
		if (func_name)
			KDBG_MEM_DBG_KFREE(func_name);
		return -1;
	}

	while (data) {
		unsigned int sym_addr = 0, virt_addr = 0;
		unsigned long offset = 0;
		/* get the function name of the given PC & elf file */
		d_fname[0] = '\0';
		func_name[0] = '\0';

		if (data->in_kernel) {
			/* for kernel samples the image name must be KERNEL */
			snprintf(lib_name, DNAME_INLINE_LEN, "[KERNEL]");

			aop_decode_cookie_kernel_symbol(
					d_fname, AOP_MAX_SYM_NAME_LENGTH, data->pc,
					&offset);
			snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH, d_fname);
#if AOP_DEBUG_ON
			PRINT_KD("%08lx%s \t%s \t%s \t%s+0x%lx\n\t",
					data->pc + virt_addr,
					AOP_KERNEL_IDENTIFICATION(data->in_kernel),
					"KERNEL: IMAGE",
					lib_name, (aop_config_demangle) ? d_fname : func_name, offset);
#endif
			data->start_addr = data->pc;
		} else {
			aop_cp_item_idx++;
			/* get library image name */
			aop_decode_cookie_without_path(data->cookie,
					lib_name, DNAME_INLINE_LEN);
			aop_get_comm_name(0, data->tid, thread_name);

			sym_addr = 0;

#ifdef CONFIG_ELF_MODULE
			symbol_info.pfunc_name = func_name;
#ifdef CONFIG_DWARF_MODULE
			symbol_info.df_info_flag = 1;
			symbol_info.pdf_info =  &df_info;
#endif /* CONFIG_DWARF_MODULE */
			if (kdbg_elf_get_symbol(lib_name, data->pc, AOP_MAX_SYM_NAME_LENGTH, &symbol_info) != 0) {
				snprintf(d_fname, AOP_MAX_SYM_NAME_LENGTH, "???");
				snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH, d_fname);
			} else if (aop_config_demangle) {
				kdbg_elf_sym_demangle(func_name,
						d_fname, AOP_MAX_SYM_NAME_LENGTH);
			}

			sym_addr = symbol_info.start_addr;
			virt_addr = symbol_info.virt_addr;
			data->start_addr = sym_addr;
#else
			sym_addr = data->start_addr = data->pc;
#endif /* CONFIG_ELF_MODULE */

			if (sym_addr <= data->pc)
				offset = data->pc - sym_addr;

			if (index < 1) {
#ifdef CONFIG_DWARF_MODULE
				if ((cp_show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) && !cp_flag) {
					PRINT_KD("\n%-5d %4d  %5d    %c  %3d  %s%-10.10s    %10.10s:%d    %s",
								aop_cp_item_idx++,
								0,
								data->total_sample_cnt,
								'T',
								data->tid,
								"    |-",
								aop_convert_taskname(thread_name, out),
								kdbg_elf_base_elf_name(symbol_info.pdf_info->df_file_name), symbol_info.pdf_info->df_line_no,
								lib_name);
				}
				PRINT_KD("\n%-5d %4d  %5d    %c  %3d  %s", aop_cp_item_idx,
							data->self_sample_cnt,
							(!cp_flag && (cp_show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE)) ? 0 : data->total_sample_cnt,
							((cp_show_option == AOP_CP_PROCESS_THREAD_WISE) ? 'T' : 'F'),
							data->tid,
							((!cp_flag && (cp_show_option != AOP_CP_PROCESS_THREAD_FUNC_WISE)) ?
								"    |-" : "      |-"));

				if (cp_show_option == AOP_CP_PROCESS_THREAD_WISE) {
					if (!strncmp(d_fname, "???", strlen("???")))
						PRINT_KD("%-10.10s", (aop_config_demangle) ? d_fname : func_name);
					else
						PRINT_KD("%-10.10s", aop_convert_taskname(thread_name, out));
				} else {
					if  (((aop_config_demangle) ? (strlen(d_fname) > 12) : (strlen(func_name) > 12))) {
						if (aop_config_demangle)
							snprintf(print_name, 12, "%-8.8s%2s", d_fname, "..");
						else
							snprintf(print_name, 12, "%-8.8s%2s", func_name, "..");
						print_name[11] = '\0';
						PRINT_KD("%-10.10s", print_name);
					} else
						PRINT_KD("%-10.10s", (aop_config_demangle) ? d_fname : func_name);
				}

				PRINT_KD("    %10.10s:%d    %s",
							kdbg_elf_base_elf_name(symbol_info.pdf_info->df_file_name), symbol_info.pdf_info->df_line_no,
							lib_name);
#else
				if ((cp_show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) && !cp_flag) {
					PRINT_KD("\n%-5d %4d  %5d    %c  %3d  %s%-10.10s    %s",
								aop_cp_item_idx++,
								0,
								data->total_sample_cnt,
								'T',
								data->tid,
								"    |-",
								aop_convert_taskname(thread_name, out),
								lib_name);
				}
				PRINT_KD("\n%-5d %4d  %5d    %c  %3d  %s%-10.10s    %s",
							aop_cp_item_idx++,
							data->self_sample_cnt,
							data->total_sample_cnt,
							'F',
							data->tid,
							((!cp_flag && (cp_show_option != AOP_CP_PROCESS_THREAD_FUNC_WISE)) ? "    |-" : "      |-"),
							(((cp_show_option == AOP_CP_PROCESS_FUNC_WISE) ?
								((aop_config_demangle) ? d_fname : func_name) : aop_convert_taskname(thread_name, out))),
							lib_name);
#endif
			} else if (index == aop_cp_item_idx) {
					PRINT_KD("\n--------------------------------------------------------------------------------------------");
					PRINT_KD("\n Thread Name      =>\t%s", aop_convert_taskname(thread_name, out));
					PRINT_KD("\n Function Name    =>\t%s", ((aop_config_demangle) ? d_fname : func_name));
#ifdef CONFIG_DWARF_MODULE
					PRINT_KD("\n Abs Path:Line no =>\t%s:%d", symbol_info.pdf_info->df_file_name, symbol_info.pdf_info->df_line_no);
#endif
					PRINT_KD("\n--------------------------------------------------------------------------------------------");
					if (thread_name)
						KDBG_MEM_DBG_KFREE(thread_name);
					if (func_name)
						KDBG_MEM_DBG_KFREE(func_name);
					if (d_fname)
						KDBG_MEM_DBG_KFREE(d_fname);
					return 1;
				}
		}
		aop_printk("\ncp = %d, Index = %d\n", aop_cp_item_idx, index);
		if (index > 1) {
			aop_printk("\ninside index, cp = %d", cp_flag);
			if ((cp_show_option == AOP_CP_PROCESS_THREAD_FUNC_WISE) && !cp_flag) {
				cp_flag = 1;
				aop_printk("\nescape caller next");
				continue;
			}
		}

		cp_flag = 1;
		data = data->caller;
	}
	if (index < 1)
		PRINT_KD("\n--------------------------------------------------------------------------------------------");

	if (thread_name)
		KDBG_MEM_DBG_KFREE(thread_name);
	if (func_name)
		KDBG_MEM_DBG_KFREE(func_name);
	if (d_fname)
		KDBG_MEM_DBG_KFREE(d_fname);
	return 0;
}

#ifdef AOP_DEBUG_ON
int  aop_sym_report_caller_sample(aop_caller_list *data)
{
	char lib_name[DNAME_INLINE_LEN+1];
	char app_name[DNAME_INLINE_LEN+1];
	char *func_name, *d_fname;
	struct aop_symbol_info  symbol_info = {0,};

	func_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
			AOP_MAX_SYM_NAME_LENGTH, GFP_KERNEL);
	if (!func_name) {
		aop_errk("func_name: no memory\n");
		return 1;
	}

	d_fname = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_REPORT_MODULE,
			AOP_MAX_SYM_NAME_LENGTH, GFP_KERNEL);
	if (!d_fname) {
		aop_errk("d_fname: no memory\n");
		KDBG_MEM_DBG_KFREE(func_name);
		return 1;
	}

	while (data) {
		unsigned int sym_addr = 0, virt_addr = 0;
		unsigned long offset = 0;

		/* get the function name of the given PC & elf file */
		d_fname[0] = '\0';
		func_name[0] = '\0';

		if (data->in_kernel) {
			/* for kernel samples the image name must be KERNEL */
			snprintf(lib_name, DNAME_INLINE_LEN, "[KERNEL]");

			aop_decode_cookie_kernel_symbol(
					d_fname, AOP_MAX_SYM_NAME_LENGTH, data->pc,
					&offset);
			snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH, d_fname);

			PRINT_KD("%08lx%s \t%s \t%s \t%s+0x%lx\n\t",
					data->pc + virt_addr,
					AOP_KERNEL_IDENTIFICATION(data->in_kernel),
					"KERNEL: IMAGE",
					lib_name, (aop_config_demangle) ? d_fname : func_name, offset);
		} else {

			/* get library image name */
			aop_decode_cookie_without_path(data->cookie,
					lib_name, DNAME_INLINE_LEN);

			symbol_info.pfunc_name = func_name;
			sym_addr = 0;

#ifdef CONFIG_ELF_MODULE
#ifdef CONFIG_DWARF_MODULE
			symbol_info.df_info_flag = 0;
			symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
			if (kdbg_elf_get_symbol(lib_name, data->pc, AOP_MAX_SYM_NAME_LENGTH, &symbol_info) != 0) {
				snprintf(d_fname, AOP_MAX_SYM_NAME_LENGTH, "???");
				snprintf(func_name, AOP_MAX_SYM_NAME_LENGTH, d_fname);
			} else if (aop_config_demangle) {
				kdbg_elf_sym_demangle(func_name,
						d_fname, AOP_MAX_SYM_NAME_LENGTH);
			}

			sym_addr = symbol_info.start_addr;
			virt_addr = symbol_info.virt_addr;
#else
			sym_addr = data->pc;
#endif /* CONFIG_ELF_MODULE */

			if (sym_addr <= data->pc)
				offset = data->pc - sym_addr;

			PRINT_KD("\t%08lx(%08lx)%s \t%s(%x) \t%s(%x) \t%s+0x%lx\t%d\t%d\n\t",
					data->pc, /*sym_addr*/data->start_addr,
					AOP_KERNEL_IDENTIFICATION(data->in_kernel),
					aop_decode_cookie_without_path(data->app_cookie, app_name, DNAME_INLINE_LEN), (unsigned)data->app_cookie,
					lib_name, (unsigned)data->cookie,
					(aop_config_demangle) ? d_fname : func_name, offset,
					data->self_sample_cnt,
					data->total_sample_cnt);
		}

		data = data->caller;
	}
	KDBG_MEM_DBG_KFREE(func_name);
	KDBG_MEM_DBG_KFREE(d_fname);

	return 0;
}
/* Callgraph */
#endif
