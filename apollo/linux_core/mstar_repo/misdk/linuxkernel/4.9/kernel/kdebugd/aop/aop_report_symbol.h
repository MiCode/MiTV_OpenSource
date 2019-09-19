

#ifndef _LINUX_AOP_REPORT_SYMBOL_H
#define _LINUX_AOP_REPORT_SYMBOL_H

extern asmlinkage long aop_lookup_dcookie(u64 cookie64, char *buf, size_t len);

/* update symbol sample function which is called from oprofile sample parse function */
void aop_sym_report_update_sample_data(struct op_data *aop_data);

/* free allocated kernel data releated to symbol report */
void aop_sym_report_free_sample_data(void);

/* Dump the application wise function name including library name */
int aop_sym_report_per_application_n_lib(void);

/* initialize apo symbol report head list */
int aop_sym_report_init(void);

/*Report the symbol information of the image(application or library) specified
by image_type. */
int aop_sym_report_per_image_user_samples(int image_type,
					  aop_cookie_t img_cookie);

/*Report the symbol information of the thread ID(TID) specified by user.*/
int aop_sym_report_per_tid(pid_t pid_user);

/* Dump system wide function name & samples */
int aop_sym_report_system_wide_function_samples(void);

/* elf symbol load/unload notification callback function */
void aop_sym_elf_load_notification_callback(int elf_load_flag);

int aop_sym_report_system_wide_samples(void);

#endif /* !_LINUX_AOP_REPORT_SYMBOL_H */
