

#ifndef _LINUX_AOP_KERNEL_H
#define _LINUX_AOP_KERNEL_H

/* extern the get module struct function which is defined in module.c file */
struct module *aop_get_module_struct(unsigned long addr);

/* update kernel sample function which is called from oprofile sample parse function */
void aop_update_kernel_sample(struct op_data *aop_data);

/* to create vmlinux or no-vmlinux file and update it to kernel_data pointer */
void aop_create_vmlinux(void);

/* free allocated kernel data (free kernel_data, kernel_image & kernel_image name memory) */
void aop_free_kernel_data(void);

/* kernel report info sort compare function */
int aop_kernel_report_cmp(const struct list_head *a, const struct list_head *b);

/* to show the kernel samples */
int aop_kernel_prepare_report(struct list_head *kern_list_head, int cpu);

/* to show the kernel samples with kernel sample header */
int aop_generate_kernel_samples(void);

#endif /* !_LINUX_AOP_KERNEL_H */
