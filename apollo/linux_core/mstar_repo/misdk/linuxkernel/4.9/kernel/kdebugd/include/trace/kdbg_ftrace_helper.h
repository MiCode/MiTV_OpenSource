/*
 * trace_kdbg.h
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

#ifndef _LINUX_TRACE_KDBG_H
#define _LINUX_TRACE_KDBG_H


/* #define KDBG_FTRACE_DEBUG */
#define warn(x...)  PRINT_KD("[WARN] " x)
#define info(x...)  PRINT_KD("[INFO] " x)
#define error(x...) PRINT_KD("[ERROR] " x)
#define menu(x...)  PRINT_KD(x)

#ifdef KDBG_FTRACE_DEBUG
#define debug(x...) PRINT_KD("[DEBUG] " x)
#else
#define debug(x...)
#endif

/* maximum number of tracers supported */
#define FTRACE_SUPPORTED_TRACERS    (14)

/* tracer flags */
#define FTRACE_TRACE_PSEUDO			(2)
#define FTRACE_TRACE_AVAILABLE          (1)
#define FTRACE_TRACE_UNAVAILABLE        (0)

/* ftrace output modes */
#define FTRACE_OUTPUT_MODE_PRINT		(1)
#define FTRACE_OUTPUT_NUM_LINES			(0)
#define FTRACE_MAX_OUTPUT_NUM_LINES		(1000)

#define FTRACE_OUTPUT_MODE_LOG			(2)
#define FTRACE_OUTPUT_FILE_NAME			"/mtd_rwarea/trace.dat"

/* default size of ftrace buffer */
#define FTRACE_DEFAULT_TRACE_SIZE_KB		(1)
/* maximum size of ftrace buffer */
#define FTRACE_MAX_TRACE_SIZE_KB		(1024)

/* default cpu mask */
#define FTRACE_DEFAULT_CPU_MASK			((1 << NR_CPUS) - 1)

/* max pids to be traced */
#define FTRACE_MAX_TRACE_PIDS			(20)

/* ftrace trace states */
enum {
	E_FTRACE_TRACE_STATE_IDLE = 0,
	E_FTRACE_TRACE_STATE_START,
	E_FTRACE_TRACE_STATE_STOP,
	E_FTRACE_TRACE_STATE_DUMP,
};

#define FTRACE_MAX_FILENAME_LEN			(256)

/* ftrace headers */
#define FTRACE_FUNC_HEADER	"#           TASK-PID    CPU#    TIMESTAMP  FUNCTION\n" \
	"#              | |       |          |         |\n"

#define FTRACE_LAT_HEADER	"#                  _------=> CPU#\n" \
	"#                 / _-----=> irqs-off\n" \
"#                | / _----=> need-resched\n" \
"#                || / _---=> hardirq/softirq\n" \
"#                ||| / _--=> preempt-depth\n" \
"#                |||| /\n" \
"#                |||||     delay\n" \
"#  cmd     pid   ||||| time  |   caller\n" \
"#     \\   /      |||||   \\   |   /\n"

#define FTRACE_GRAPH_HEADER	"#      TIME       CPU  TASK/PID        DURATION                  FUNCTION CALLS\n" \
	"#       |         |    |    |           |   |                     |   |   |   |\n"

/* events for kmemtracer */
#define FTRACE_KMEM_KMALLOC_EVENT		"kmem:kmalloc"
#define FTRACE_KMEM_KFREE_EVENT		"kmem:kfree"

/* options for bt_kernel tracer */
#define FTRACE_BT_TRACER			"function"
#define FTRACE_BT_KERNEL_TRACE_OPTION		"func_stack_trace"
#define FTRACE_BT_USER_TRACE_OPTION		"userstacktrace"

/* maximum call depth */
#define FTRACE_BT_MAX_CALL_DEPTH	(20)
/* default call depth */
#define FTRACE_BT_DEFAULT_CALL_DEPTH    (5)

/* ftrace list structure */
struct ftrace_supported_list {
	char *trace_name;
	int available;
};


/* ftrace configuration structure */
struct kdbg_ftrace_conf {
	char *trace_name; /* name of tracer */
	int trace_pid[FTRACE_MAX_TRACE_PIDS]; /* pid to be trace */
	int trace_num_pid; /* number of pids to be traced */
	char *trace_list; /* list of functions to trace */
	int trace_list_size; /* size of list of functions to trace */
	char *trace_not_list; /* list of functions not to trace */
	int trace_not_list_size; /* size of list of functions not to trace */
	char *trace_graph_list; /* list of graph functions to trace */
	int trace_graph_list_size; /* size of list of graph functions to trace */
	char *trace_black_list; /* list of blacklist functions not to trace */
	int trace_black_list_size; /* size of blacklist functions not to trace */
	int trace_mode; /* mode of trace print/log) */
	int trace_lines_per_print; /* lines per print - valid for "print" mode */
	char trace_file_name[FTRACE_MAX_FILENAME_LEN]; /* trace file name - valid for "log" mode */
	struct file *trace_file; /* trace file - valid for "log" mode */
	char trace_ctrl_file_name[FTRACE_MAX_FILENAME_LEN]; /* trace ctrl file name */
	struct file *trace_ctrl; /* trace ctrl file */
	unsigned long trace_buffer_size[NR_CPUS]; /* max trace buffer size */
	int trace_cpu_mask; /* trace cpu mask */
	int trace_state; /* trace state(idle/start/stop/dump) */
	struct ftrace_supported_list supported_list[FTRACE_SUPPORTED_TRACERS]; /* list of supported tracers */
	int trace_timestamp_nsec_status; /* trace timestamp nsec status */
	char *trace_event_list; /* list of events to trace */
	int trace_event_list_size; /* size of list of events to trace */
	int trace_option; /* flag for trace option */
	int trace_call_depth; /* call_depth */
};

/* global ftrace configuration */
extern struct kdbg_ftrace_conf fconf;

#ifdef CONFIG_FTRACE_BENCHMARK
/* ftrace benchmarking states */
enum {
	E_FTRACE_BENCHMARK_IDLE = 0,
	E_FTRACE_BENCHMARK_START,
	E_FTRACE_BENCHMARK_STOP,
};

/* ftrace benchmark configuration structure */
struct kdbg_ftrace_benchmark_conf {
	struct task_struct *bench_th; /* pointer to benchmark thread */
	struct task_struct *wakeup_th;  /* pointer to wakeup thread- used in wakeup tracer*/
	long iterations;        /* Iteration */
	int count;  /* count to print and average */
	int state;  /* benchmarking states- start/stop */
	int time_count;  /* count for timer */
	char trace_name[20]; /* tracer name */
};

/* global ftrace configuration */
extern struct kdbg_ftrace_benchmark_conf bconf;
#endif /* CONFIG_FTRACE_BENCHMARK */

/* ftrace helper functions */
/* ftrace basic helper functions */
int kdbg_ftrace_start(void);
int kdbg_ftrace_stop(void);
int kdbg_ftrace_list(struct kdbg_ftrace_conf *pfconf);
void kdbg_ftrace_reset(void);
int kdbg_ftrace_dump(struct kdbg_ftrace_conf *pfconf);
int kdbg_ftrace_stack_dump(struct kdbg_ftrace_conf *pfconf);

/* ftrace configuration helper functions */
const char *kdbg_ftrace_get_trace_name(void);
int kdbg_ftrace_set_trace_name(char *tracer_name);

int kdbg_ftrace_get_trace_pid(struct kdbg_ftrace_conf *pfconf);
int kdbg_ftrace_set_trace_pid(int val);
int kdbg_ftrace_reset_trace_pid(struct kdbg_ftrace_conf *pfconf, int reset);

/* ftrace status helper functions */
void kdbg_ftrace_enable(void);
void kdbg_ftrace_disable(void);

#ifdef CONFIG_DYNAMIC_FTRACE
int kdbg_ftrace_set_ftrace_filter(unsigned char *buf);
int kdbg_ftrace_set_ftrace_notrace(unsigned char *buf);
void kdbg_ftrace_reset_ftrace_filter(void);
void kdbg_ftrace_reset_ftrace_notrace(void);
int kdbg_ftrace_set_ftrace_graph_filter(char *buf);
void kdbg_ftrace_reset_ftrace_graph_filter(void);
#endif
#ifdef CONFIG_FUNCTION_PROFILER
/* function profiling */
int kdbg_ftrace_profile_start(void);
int kdbg_ftrace_profile_stop(void);
int kdbg_ftrace_profile_dump(struct kdbg_ftrace_conf *pfconf);
int kdbg_ftrace_profile_dump_stat(void *v);
void kdbg_ftrace_profile_dump_header(void);
#endif /* CONFIG_FUNCTION_PROFILER */

unsigned long kdbg_ftrace_get_trace_buffer_size(int cpu);
int kdbg_ftrace_set_trace_buffer_size(unsigned long val, int cpu);
unsigned long kdbg_ftrace_get_trace_max_latency(void);
void kdbg_ftrace_set_trace_max_latency(unsigned long val);
int kdbg_ftrace_get_trace_cpu_mask(void);
int kdbg_ftrace_set_trace_cpu_mask(char *buf);
int kdbg_ftrace_set_trace_option(char *buf);

int kdbg_ftrace_open_ctrl(struct kdbg_ftrace_conf *pfconf);
int kdbg_ftrace_close_ctrl(struct kdbg_ftrace_conf *pfconf);
int kdbg_ftrace_write_ctrl(struct kdbg_ftrace_conf *pfconf, char *buffer);
int kdbg_ftrace_read_ctrl(struct kdbg_ftrace_conf *pfconf, char *buffer);

void kdbg_ftrace_set_trace_max_stack(unsigned long val);
unsigned long kdbg_ftrace_get_trace_max_stack(void);

s64 kdbg_ftrace_timekeeping_get_ns_raw(void);

#ifdef CONFIG_EVENT_TRACING
void kdbg_ftrace_available_subsys(void);
int kdbg_ftrace_get_available_events(char *match);
int kdbg_ftrace_set_event(char *buf);
int kdbg_ftrace_reset_event(void);
#endif

struct stack_trace; /* forward declaration */
void kdbg_save_stack_trace_user(struct stack_trace *trace);

#ifdef CONFIG_FTRACE_BENCHMARK
int trace_benchmark_thread(void *data);
int trace_benchmark_latency_thread(void *data);
int trace_benchmark_memory_thread(void *data);
int trace_benchmark_event_thread(void *data);
int trace_wakeup_thread(void *data);
void handle_ftrace_benchmark_config(void);
#endif
#endif
