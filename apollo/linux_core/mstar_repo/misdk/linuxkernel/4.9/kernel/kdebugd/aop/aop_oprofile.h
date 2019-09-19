
#ifndef _LINUX_AOP_OPROFILE_H
#define _LINUX_AOP_OPROFILE_H

#include "kdbg_util.h"
#include <../drivers/oprofile/oprof.h>
#include <../drivers/oprofile/event_buffer.h>

/*size of the unsigned long integer*/
#define AOP_ULONG_SIZE	(sizeof(unsigned long))
#define AOP_KB	1024

#define  COUNT_SAMPLES(struct_name, field , n)  ({\
	int i;\
	int sum = 0;\
	for (i = 0; i < n; ++i) {\
		sum += (struct_name)->field[i];\
	} \
	sum;\
})

/*function pointer to the various handlers which
are used to decode the samples in oprofiling*/
typedef void (*aop_handler_t) (void);

/* generic type for holding cookie value */
typedef unsigned long long aop_cookie_t;

/* generic type for holding addresses */
typedef unsigned long aop_vma_t;

/*raw oprofile data is stored*/
struct aop_cache_type {
	unsigned long *buffer;	/*pointer to the buffer in which the raw data is collected */
	int wr_offset;		/*write offset at which the raw data value is written */
	int rd_offset;		/*read offset at which the raw data value is read */
};

/*count of the total kernel samples collected on
processing the buffer aop_cache*/
extern unsigned long aop_nr_kernel_samples;

/* aop report symbol demangle option, by default it is none.
It may be changed using setup configuration option.
By default demangling function name is ON */
extern int aop_config_demangle;

void aop_kdebug_start(void);

#ifdef KDBG_MEM_DBG
/*free all the resources taken by the Adv. Oprofile*/
extern void aop_shutdown(void);
#endif

#ifdef CONFIG_CACHE_ANALYZER
/* Function for control perf-events counters */
unsigned long aop_get_counter_config(unsigned int i);
unsigned long aop_get_counter_count(unsigned int i);
unsigned long aop_get_counter_type(unsigned int i);
int aop_get_counter_enabled(unsigned int i);
int aop_setup_counter(unsigned int i, unsigned long count,
	unsigned long config, unsigned long type, int enable);
#endif

/* aop report sort option, by default it is none.
It may be changed using setup configuration option */
extern int aop_config_sort_option;

/*total samples = user+kernel*/
extern unsigned long aop_nr_total_samples;

/*structure variable to store the pointer to the
raw data buffer and write and read positions.*/
extern struct aop_cache_type aop_cache;

/*This is called from the kernel space to decode
the cookie value into the directory PATH*/
/* Note the use of the asmlinkage modifier.
This macro (defined in linux/include/(arm/mips) /linkage.h)
tells the compiler to pass all function arguments on the stack*/
extern asmlinkage long
aop_sys_lookup_dcookie(u64 cookie64, char *buf, size_t len);

/*TRACING enum*/
enum tracing_type {
	AOP_TRACING_OFF,
	AOP_TRACING_START,
	AOP_TRACING_ON
};

/*This data structure will hold the transient data while
processing the raw data collected in profiling*/
struct op_data {
	enum tracing_type tracing;	/*Tracing the value in Library */
	aop_cookie_t cookie;	/*coolie value of library */
	aop_cookie_t app_cookie;	/*coolie value of application */
	aop_vma_t pc;		/*program counter value */
	unsigned long event;	/*event value */
	int in_kernel;		/*in_kernel = 1 if the context is kernel, =0 if it is in user context */
	unsigned long cpu;	/*cpu number */
	pid_t tid;		/*thread ID */
	pid_t tgid;		/*thread group ID */
};

/* Callgraph */
/*This data structure will hold the transient data while
processing the raw data collected in profiling*/
typedef struct aop_caller_list {
       aop_cookie_t cookie;    /*coolie value of library */
       aop_cookie_t app_cookie;        /*coolie value of application */
       aop_vma_t pc;           /*program counter value */
	aop_vma_t start_addr;
       int in_kernel;          /*in_kernel = 1 if the context is kernel, =0 if it is in user context */
	int self_sample_cnt;
	int total_sample_cnt;
	pid_t tid;		/*thread ID */
	pid_t tgid;		/*thread group ID */
       struct aop_caller_list *caller;
} aop_caller_list;

/*link list to store the tgid and tid related data which is collected after processing the raw data.
This includes tgid value*/
typedef struct aop_caller_head_list {
       int cnt;
       aop_caller_list *sample;
       struct aop_caller_head_list *next;
} aop_caller_head_list;


/* enum for kdebugd menu num */
typedef enum {
	AOP_CP_PROCESS_FUNC_WISE = 1,
	AOP_CP_PROCESS_THREAD_FUNC_WISE,
	AOP_CP_PROCESS_THREAD_WISE,
	AOP_CP_MAX
} AOP_CP_REPORT_SHOW_OPTION;

int  aop_sym_report_caller_sample(aop_caller_list *data);
int  aop_cp_add_start_addr(aop_caller_list *data);
int  aop_cp_report_process_func_wise(aop_caller_list *data, int index, int cp_show_option);
void aop_cp_free_mem(void);
void aop_get_comm_name(int flag, pid_t pid, char *t_name);
/*decode the cookie into the name of application and libraries.*/
char *aop_decode_cookie_without_path(aop_cookie_t cookie, char *buf,
		size_t buf_size);
/* Callgraph */

/*store the kernel image or kernel module name
with start and end address*/
struct kernel_image {
	char *name;		/*kernel image name */
	aop_vma_t start;	/*start address of the kernel image */
	aop_vma_t end;		/*end address of the kernel image */
};

/*link list to store the tgid and tid related data which is collected after processing the raw data.
This includes tgid value.
sample count denotes the number of samples collected for that pid*/
typedef struct aop_pid_list {
	pid_t pid;		/*thread ID */
	pid_t tgid;		/*thread group ID */
	unsigned int samples_count[NR_CPUS];
	char *thread_name;
	struct aop_pid_list *next;
} aop_pid_list;

/*link list to store the tgid and tid related data which is collected after processing the raw data.
This includes tgid value*/
typedef struct aop_dead_list {
	pid_t pid;		/*thread ID */
	pid_t tgid;		/*thread group ID */
	char *thread_name;
	struct aop_dead_list *next;
} aop_dead_list;

/* Linked list to store the application and library related data which is collected
after processing the raw data.
This includes cookie value which is decoded into the PATH of the application or library.
sample count denotes the number of samples collected for that application or library */
typedef struct aop_image_list {
	aop_cookie_t cookie_value;	/*cookie value which is decoded into application */
	unsigned int samples_count[NR_CPUS];
	struct aop_image_list *next;
} aop_image_list;

/*The samples are processed and the data specific to application which
are run are extracted and collected in this structure. aop_app_list_head is
the head of node of the linked list*/
extern aop_image_list *aop_app_list_head;

struct aop_report_all_list {
	struct list_head report_list;
	int is_kernel;		/*is_kernel = 1 if this report is for kernel, =0 otherwise */
	union {
		aop_cookie_t cookie_value;	/*cookie value which is decoded into img name */
		char *kernel_name;
	} report_type;
	unsigned int samples_count;
};

/* adv oprofile report types */
enum {
	AOP_TYPE_TGID,
	AOP_TYPE_TID,
	AOP_TYPE_APP,
	AOP_TYPE_LIB,
	AOP_TYPE_KERN
};

/* adv oprofile sort options */
enum {
	AOP_SORT_DEFAULT,	/* sort option none */
	AOP_SORT_BY_VMA,	/* sort by vma address */
	AOP_SORT_BY_SAMPLES	/* sort report by samples */
};

#endif /* !_LINUX_AOP_OPROFILE_H */
