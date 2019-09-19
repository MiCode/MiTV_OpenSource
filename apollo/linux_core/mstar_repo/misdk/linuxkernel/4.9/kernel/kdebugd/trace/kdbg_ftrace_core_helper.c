/*
 * kdbg_ftrace_core_helper.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

/* Include in trace.c */
#include <trace/kdbg_ftrace_helper.h>
#include <kdebugd.h>
#include "kdbg_util.h"
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#ifdef CONFIG_KDEBUGD_FTRACE_USER_BACKTRACE
#include <linux/sched.h>
#include "../../../kernel/kdebugd/kdbg-trace.h"
#endif

/* ftrace helper functions */
/* ftrace basic helper functions */
/*
 * kdbg_ftrace_start
 * starts the current tracer.
 */
int kdbg_ftrace_start(void)
{
	struct trace_array *tr = &global_trace;

	mutex_lock(&trace_types_lock);

	if (!tr) {
		mutex_unlock(&trace_types_lock);
		return -1;
	}

	if (!tracing_is_on()) {
		tracing_on();
		if (tr->current_trace->start)
			tr->current_trace->start(tr);
		tracing_start();
	}
	mutex_unlock(&trace_types_lock);
	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_start);

/*
 * kdbg_ftrace_stop
 * stops the current tracer.
 */
int kdbg_ftrace_stop(void)
{
	struct trace_array *tr = &global_trace;

	mutex_lock(&trace_types_lock);

	if (!tr) {
		mutex_unlock(&trace_types_lock);
		return -1;
	}

	if (tracing_is_on()) {
		tracing_off();
		tracing_stop();
		if (tr->current_trace->stop)
			tr->current_trace->stop(tr);
	}
	mutex_unlock(&trace_types_lock);

	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_stop);

/*
 * kdbg_ftrace_list
 * lists the available tracers in the system.
 */
int kdbg_ftrace_list(struct kdbg_ftrace_conf *pfconf)
{
	struct tracer *t;
	int ctr = 0;

	WARN_ON(!pfconf);

	mutex_lock(&trace_types_lock);

	if (!trace_types) {
		mutex_unlock(&trace_types_lock);
		return -1;
	}

	for (t = trace_types; t; t = t->next) {
		for (ctr = 0; ctr < FTRACE_SUPPORTED_TRACERS; ctr++) {
			if (!strcmp(t->name, pfconf->supported_list[ctr].trace_name))
				pfconf->supported_list[ctr].available = FTRACE_TRACE_AVAILABLE;
		}
	}

	mutex_unlock(&trace_types_lock);
	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_list);

/*
 * kdbg_ftrace_reset
 * reset the current tracers and trace buffer.
 */
void kdbg_ftrace_reset(void)
{

	mutex_lock(&trace_types_lock);
	/* reset both trace buffers i.e. global_trace and max_tr */
	tracing_reset_online_cpus(&(global_trace.trace_buffer));
#ifdef CONFIG_TRACER_MAX_TRACE
	tracing_reset_online_cpus(&(global_trace.max_buffer));
#endif
	mutex_unlock(&trace_types_lock);
}
EXPORT_SYMBOL(kdbg_ftrace_reset);

/*
 * kdbg_ftrace_open_log
 * open the log file used for writing traces.
 */
static int kdbg_ftrace_open_log(struct kdbg_ftrace_conf *pfconf)
{
	int ret = 0 ;

	WARN_ON(!pfconf);

	pfconf->trace_file = filp_open(pfconf->trace_file_name, O_CREAT | O_TRUNC |
			O_WRONLY | O_LARGEFILE, 0600);
	if (IS_ERR(pfconf->trace_file)) {
		error("Trace Error %ld, Opening %s.\n", -PTR_ERR(pfconf->trace_file),
				pfconf->trace_file_name);
		pfconf->trace_file = NULL;
		ret = -1;
	} else {
		info("Trace Output File Name %s.\n", pfconf->trace_file_name);
	}

	return ret;
}

/*
 * kdbg_ftrace_close_log
 * close the log file used for writing traces.
 */
static int kdbg_ftrace_close_log(struct kdbg_ftrace_conf *pfconf)
{
	int ret  = 0;

	WARN_ON(!pfconf);

	ret = filp_close(pfconf->trace_file, NULL);
	pfconf->trace_file = NULL;
	return ret;
}


/*
 * kdbg_ftrace_print_log_write
 * prints/logs the buffer.
 */
static int kdbg_ftrace_print_log_write(struct kdbg_ftrace_conf *pfconf, char *trace_buffer)
{
	int ret = 0;
	mm_segment_t oldfs;
	struct file *fp = NULL;

	WARN_ON(!pfconf);

	if (!trace_buffer) {
		error("Trace Error, Invalid Buffer.");
		return -1;
	}

	fp = pfconf->trace_file;

	/* print the trace */
	if (fp == NULL) {
		menu("%s", trace_buffer);
		return 0;
	}

	/* log the trace */
	if (!(fp->f_op && fp->f_op->write)) {
		error("Trace Error, Writing %s.\n", pfconf->trace_file_name);
		return -1;
	}

	/*
	 * kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	oldfs = get_fs();
	set_fs(KERNEL_DS);

	ret = fp->f_op->write(fp, trace_buffer, strlen(trace_buffer),  &fp->f_pos);

	if (ret < 0) {
		error("Trace Error %d, Writing %s.\n", -ret, pfconf->trace_file_name);

		/* restore kernel segment */
		set_fs(oldfs);
		return -1;
	}

	/* restore kernel segment */
	set_fs(oldfs);
	return 0;
}

static int kdbg_ftrace_print_log_banner(struct kdbg_ftrace_conf *pfconf,	struct trace_iterator *iter, int lat)
{
	struct trace_array *tr = iter->tr;
	struct trace_array_cpu *data = tr->trace_buffer.data;
	struct tracer *type = tr->current_trace;
	unsigned long total;
	unsigned long entries;
	const char *name = "preemption";
	char *trace_header = NULL;
	char *trace_banner = NULL;
	int ret = 0;

#define BANNER_APPEND(x, ...)	do { if (TRACE_MAX_PRINT - ret > 0) { \
	ret += snprintf(trace_banner + ret, TRACE_MAX_PRINT - ret, x, ##__VA_ARGS__); \
} \
	} while (0)

	WARN_ON(!pfconf);

	/* allocate the local trace banner */
	trace_banner = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE,
			TRACE_MAX_PRINT, GFP_KERNEL);

	if (!trace_banner) {
		warn("kmalloc failed, len %d\n" , TRACE_MAX_PRINT);
		return -1;
	}

	BANNER_APPEND("# tracer: %s\n", iter->trace->name);
	BANNER_APPEND("#\n");

	/* set the latency trace header */
	if (lat) {
		if (type)
			name = type->name;

		entries = ring_buffer_entries(iter->trace_buffer->buffer);
		total = entries +
			ring_buffer_overruns(iter->trace_buffer->buffer);

		debug("entries %lu, overuns %lu, total %lu\n", entries,
				ring_buffer_overruns(iter->trace_buffer->buffer), total);
		BANNER_APPEND("# %s latency trace v1.1.5 on %s\n",
				name, UTS_RELEASE);

		BANNER_APPEND("# -----------------------------------"
				"---------------------------------\n");

		BANNER_APPEND("# latency: %lu us, #%lu/%lu, CPU#%d |"
				" (M:%s VP:%d, KP:%d, SP:%d HP:%d",
				nsecs_to_usecs(data->saved_latency),
				entries,
				total,
				(tr->trace_buffer).cpu,
#if defined(CONFIG_PREEMPT_NONE)
				"server",
#elif defined(CONFIG_PREEMPT_VOLUNTARY)
				"desktop",
#elif defined(CONFIG_PREEMPT)
				"preempt",
#else
				"unknown",
#endif
				/* These are reserved for later use */
				0, 0, 0, 0);
#ifdef CONFIG_SMP
		BANNER_APPEND(" #P:%d)\n", num_online_cpus());
#else
		BANNER_APPEND(")\n");
#endif
		BANNER_APPEND("#    -----------------\n");
		BANNER_APPEND("#    | task: %.16s-%d "
				"(uid:%d nice:%ld policy:%ld rt_prio:%ld)\n",
				data->comm, data->pid, data->uid, data->nice,
				data->policy, data->rt_priority);
		BANNER_APPEND("#    -----------------\n");

		if (data->critical_start) {
			char str[KSYM_SYMBOL_LEN];

			/* fetch name of data->critical_start */
			kallsyms_lookup(data->critical_start, NULL, NULL, NULL, str);
			BANNER_APPEND("#  => started at: ");
			BANNER_APPEND("%s", str);

			/* fetch name of data->critical_end */
			kallsyms_lookup(data->critical_end, NULL, NULL, NULL, str);
			BANNER_APPEND("\n#  => ended at:   ");
			BANNER_APPEND("%s", str);

			BANNER_APPEND("\n#\n");
		}
		BANNER_APPEND("#\n");
	}

	/* set the trace header */
	if (!strcmp(pfconf->trace_name, "function_graph"))
		trace_header = FTRACE_GRAPH_HEADER;
	else if (lat)
		trace_header = FTRACE_LAT_HEADER;
	else
		trace_header = FTRACE_FUNC_HEADER;

	BANNER_APPEND("%s", trace_header);

	/* print/log trace header */
	if (kdbg_ftrace_print_log_write(pfconf, trace_banner)) {
		KDBG_MEM_DBG_KFREE(trace_banner);
		return -1;
	}

	KDBG_MEM_DBG_KFREE(trace_banner);
	return 0;
}


/*
 * kdbg_ftrace_print_log
 * prints the trace log to standard output/log file.
 */
static int kdbg_ftrace_print_log(struct kdbg_ftrace_conf *pfconf, struct trace_iterator *iter,
		int trace_cnt)
{
	int event = 0;

	WARN_ON(!pfconf);

	/* print/log trace banner */
	if (trace_cnt == 1) {
		if (kdbg_ftrace_print_log_banner(pfconf, iter,
					trace_flags & TRACE_ITER_LATENCY_FMT)) {
			return -1;
		}
	}

	/* print/log trace buffer */
	if (kdbg_ftrace_print_log_write(pfconf, iter->seq.buffer))
		return -1;

	/* special handling cases for "print" and "log" mode */
	if (pfconf->trace_file) {
		menu(".");
		if (trace_cnt % 4 == 0)
			menu("\r");
	} else if (pfconf->trace_lines_per_print &&
			(trace_cnt % pfconf->trace_lines_per_print == 0)) {
		menu("Press 99 To Stop Trace Dump, Else Continue.. ");
		event = debugd_get_event_as_numeric(NULL, NULL);
		menu("\n");
		if (event == 99)
			return -1;
	}
	return 0;
}

	static int
kdbg_ftrace_print_log_seq(struct kdbg_ftrace_conf *pfconf, struct trace_iterator *iter, int cnt)
{
	struct trace_seq *s = &iter->seq;

	WARN_ON(!pfconf);

	/* Probably should print a warning here. */
	if (s->seq.len >= TRACE_MAX_PRINT)
		s->seq.len = TRACE_MAX_PRINT;

	/* should be zero ended, but we are paranoid. */
	s->buffer[s->seq.len] = 0;

	if (kdbg_ftrace_print_log(pfconf, iter, cnt))
		return -1;

	trace_seq_init(s);
	return 0;
}

int kdbg_trace_init_global_iter(struct trace_iterator *iter)
{
	/* reset iterator */
	memset(iter, 0, sizeof(*iter));
#ifdef CONFIG_TRACER_MAX_TRACE
	if (global_trace.current_trace && global_trace.current_trace->print_max)
		iter->tr->max_buffer = global_trace.max_buffer;
	else
#endif
		iter->tr->trace_buffer = global_trace.trace_buffer;
	iter->trace = global_trace.current_trace;
	iter->cpu_file = TRACE_PIPE_ALL_CPU;
	iter->buffer_iter = kzalloc(sizeof(*iter->buffer_iter) * num_possible_cpus(), GFP_KERNEL);
	if (!iter->buffer_iter)
		return -1;
	return 0;
}

/*
 * kdbg_ftrace_dump
 * dumps the ftrace dump for the current tracer.
 */
int kdbg_ftrace_dump(struct kdbg_ftrace_conf *pfconf)
{
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	unsigned int old_userobj;
	int cnt = 0, cpu;
	int print_log_error = 0;

	WARN_ON(!pfconf);

#if defined(CONFIG_STACK_TRACER) || defined(CONFIG_FUNCTION_PROFILER)
	/* special case for stack and function profile tracer */
	if (pfconf->trace_mode == FTRACE_OUTPUT_MODE_LOG)
		info("Trace Mode \"Log\" Not Supported.\n");
#ifdef CONFIG_STACK_TRACER
	if (!strcmp(pfconf->trace_name, "stack"))
		return kdbg_ftrace_stack_dump(pfconf);
#endif
#ifdef CONFIG_FUNCTION_PROFILER
	else if (!strcmp(pfconf->trace_name, "function_profile"))
		return kdbg_ftrace_profile_dump(pfconf);
#endif
#endif

	/* open the log file */
	if (pfconf->trace_mode == FTRACE_OUTPUT_MODE_LOG) {
		/* "log" mode, return if log file does not exits/opens */
		if (kdbg_ftrace_open_log(pfconf))
			return -1;
	}

	if (kdbg_trace_init_global_iter(&iter) < 0)
		return -1;

	for_each_tracing_cpu(cpu) {
		atomic_inc(&iter.tr->trace_buffer.data[cpu].disabled);
	}

	old_userobj = trace_flags;

	/* don't look at user memory in panic mode */
	trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	/* reset all but tr, trace, and overruns */
	memset((char *)&iter + offsetof(struct trace_iterator, seq), 0,
			sizeof(struct trace_iterator) -
			offsetof(struct trace_iterator, seq));
	if (trace_flags & TRACE_ITER_LATENCY_FMT)
		iter.iter_flags |= TRACE_FILE_LAT_FMT;
	/* Annotate start of buffers if we had overruns */
	if (ring_buffer_overruns(iter.tr->trace_buffer.buffer))
		iter.iter_flags |= TRACE_FILE_ANNOTATE;
	iter.pos = -1;

	WARN_ON(!iter.buffer_iter);
	for_each_tracing_cpu(cpu) {
		iter.buffer_iter[cpu] =
			ring_buffer_read_prepare(iter.tr->trace_buffer.buffer, cpu);
		ring_buffer_read_start(iter.buffer_iter[cpu]);
		tracing_iter_reset(&iter, cpu);
	}

	while (!trace_empty(&iter)) {
		cnt++;

		if (trace_find_next_entry_inc(&iter) != NULL)
			print_trace_line(&iter);

		if (kdbg_ftrace_print_log_seq(pfconf, &iter, cnt)) {
			print_log_error = 1;
			break;
		}
	}

	if (!cnt) {
		info("Trace Buffer Empty.\n");
		if (pfconf->trace_file)
			kdbg_ftrace_print_log_write(pfconf, "Trace Buffer Empty.\n");
		print_log_error = 1;
	}

	trace_flags = old_userobj;

	for_each_tracing_cpu(cpu) {
		atomic_dec(&iter.tr->trace_buffer.data[cpu].disabled);
	}

	for_each_tracing_cpu(cpu)
		if (iter.buffer_iter[cpu])
			ring_buffer_read_finish(iter.buffer_iter[cpu]);

	/*Free iter->buffer_iter allocated in init*/
	if (iter.buffer_iter)
		kfree(iter.buffer_iter);

	/* close the log file */
	if (pfconf->trace_file)
		kdbg_ftrace_close_log(pfconf);

	/* return -1, in case of any error */
	if (print_log_error)
		return -1;

	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_dump);

/* ftrace configuration helper functions */
/*
 * kdbg_ftrace_get_trace_name
 * gets the trace name.
 */
const char *kdbg_ftrace_get_trace_name(void)
{
	const char *trace_name = NULL;

	mutex_lock(&trace_types_lock);
	if (global_trace.current_trace)
		trace_name = global_trace.current_trace->name;
	mutex_unlock(&trace_types_lock);

	return trace_name;
}
EXPORT_SYMBOL(kdbg_ftrace_get_trace_name);

/*
 * kdbg_ftrace_set_trace_name
 * sets the trace name.
 */
int kdbg_ftrace_set_trace_name(char *trace_name)
{
	int i = 0;
	int lock_count = 0;
	struct trace_array tr;

	if (!trace_name)
		return -1;

	/* strip ending whitespace. */
	for (i = strlen(trace_name); i > 0 && isspace(trace_name[i]); i--)
		trace_name[i] = 0;

	/* check if module_mutex can be locked */
	lock_count = atomic_read(&(module_mutex.count));
	if (lock_count == 1) {
		return tracing_set_tracer(&tr, trace_name);
	} else {
		error("Module Mutex Unavailable, Lock Count %d\n", lock_count);
		return -1;
	}

}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_name);

/*
 * kdbg_ftrace_get_trace_buffer_size
 * gets the buffer size.
 */
unsigned long kdbg_ftrace_get_trace_buffer_size(int cpu)
{
	struct trace_array *tr = &global_trace;
	unsigned long val;

	mutex_lock(&trace_types_lock);

	val = tr->trace_buffer.data[cpu].entries >> 10 ;

	if (!ring_buffer_expanded)
		val += (trace_buf_size >> 10);
	mutex_unlock(&trace_types_lock);

	return val;
}
EXPORT_SYMBOL(kdbg_ftrace_get_trace_buffer_size);

/*
 * kdbg_ftrace_set_trace_buffer_size
 * sets the buffer size.
 */
int kdbg_ftrace_set_trace_buffer_size(unsigned long val, int cpu)
{
	int ret = 0;

	if (!val)
		return -1;

	/* value is in KB */
	val <<= 10;

	if (val != global_trace.trace_buffer.data[cpu].entries) {
		ret = tracing_resize_ring_buffer(&global_trace,val, cpu);
		if (ret < 0)
			goto out;
	}
	/* If check pages failed, return ENOMEM */
	if (tracing_disabled)
		ret = -ENOMEM;
out:
	return ret;
}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_buffer_size);

#ifdef CONFIG_TRACER_MAX_TRACE
/*
 * kdbg_ftrace_get_trace_max_latency
 * gets the max latency.
 */
unsigned long kdbg_ftrace_get_trace_max_latency(void)
{
	unsigned long latency = 0;

	mutex_lock(&trace_types_lock);

	if (tracing_max_latency)
		latency = nsecs_to_usecs(tracing_max_latency);
	else
		latency = 0;
	mutex_unlock(&trace_types_lock);

	return latency;
}
EXPORT_SYMBOL(kdbg_ftrace_get_trace_max_latency);

/*
 * kdbg_ftrace_set_trace_max_latency
 * sets the max latency.
 */
void kdbg_ftrace_set_trace_max_latency(unsigned long val)
{
	mutex_lock(&trace_types_lock);
	tracing_max_latency = val * 1000;
	mutex_unlock(&trace_types_lock);
}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_max_latency);
#endif
/*
 * kdbg_ftrace_get_trace_cpu_mask
 * gets the cpu mask.
 */
int kdbg_ftrace_get_trace_cpu_mask(void)
{

	int len = 0;
	unsigned long  cpu_mask = 0;
	int ret = 0;
	mutex_lock(&tracing_cpumask_update_lock);

	//len = cpumask_scnprintf(mask_str, NR_CPUS + 1, tracing_cpumask); fix me

	if (!len) {
		mutex_unlock(&tracing_cpumask_update_lock);
		return 0;
	}

	/* convert to integer */
	//ret = strict_strtoul(mask_str, 16, &cpu_mask); //fix me

	/* handle invalid value(not a pure number) */
	if (ret < 0) {
		mutex_unlock(&tracing_cpumask_update_lock);
		return 0;
	}

	mutex_unlock(&tracing_cpumask_update_lock);

	return cpu_mask;
}
EXPORT_SYMBOL(kdbg_ftrace_get_trace_cpu_mask);

/*
 * kdbg_ftrace_set_trace_cpu_mask
 * sets the cpu mask.
 */
int kdbg_ftrace_set_trace_cpu_mask(char *buf)
{
	int err, cpu;
	cpumask_var_t tracing_cpumask_new;

	if (!alloc_cpumask_var(&tracing_cpumask_new, GFP_KERNEL))
		return -1;

	mutex_lock(&tracing_cpumask_update_lock);

	err = bitmap_parse(buf, strlen(buf), cpumask_bits(tracing_cpumask_new),
			nr_cpumask_bits);
	if (err) {
		mutex_unlock(&tracing_cpumask_update_lock);
		free_cpumask_var(tracing_cpumask);
		return -1;
	}

	local_irq_disable();
	arch_spin_lock(&global_trace.max_lock);
	for_each_tracing_cpu(cpu) {
		/*
		 * Increase/decrease the disabled counter if we are
		 * about to flip a bit in the cpumask:
		 */
		if (cpumask_test_cpu(cpu, tracing_cpumask) &&
				!cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_inc(&global_trace.trace_buffer.data[cpu].disabled);
		}
		if (!cpumask_test_cpu(cpu, tracing_cpumask) &&
				cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_dec(&global_trace.trace_buffer.data[cpu].disabled);
		}
	}
	arch_spin_unlock(&global_trace.max_lock);
	local_irq_enable();

	cpumask_copy(tracing_cpumask, tracing_cpumask_new);

	mutex_unlock(&tracing_cpumask_update_lock);
	free_cpumask_var(tracing_cpumask_new);

	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_cpu_mask);

/*
 * kdbg_ftrace_set_trace_option
 * sets the specified trace option.
 */
int kdbg_ftrace_set_trace_option(char *buf)
{
	char *cmp;
	int neg = 0;
	int ret = -1;
	int i;

	cmp = strstrip(buf);

	if (strncmp(cmp, "no", 2) == 0) {
		neg = 1;
		cmp += 2;
	}

	for (i = 0; trace_options[i]; i++) {
		if (strcmp(cmp, trace_options[i]) == 0) {
			ret = set_tracer_flag(&global_trace,1 << i, !neg);
			break;
		}
	}

    /* If no option could be set, test the specific tracer options */
	if (!trace_options[i]) {
		mutex_lock(&trace_types_lock);
		ret = set_tracer_option(global_trace.current_trace, cmp, neg);
		mutex_unlock(&trace_types_lock);
	}

	return ret;
}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_option);


/*
 * kdbg_ftrace_open_ctrl
 * opens the control file.
 */
int kdbg_ftrace_open_ctrl(struct kdbg_ftrace_conf *pfconf)
{

	int ret = 0 ;

	WARN_ON(!pfconf);

	pfconf->trace_ctrl = filp_open(pfconf->trace_ctrl_file_name, O_RDWR , 0600);
	if (IS_ERR(pfconf->trace_ctrl)) {
		error("Trace Ctrl Error %ld, Opening %s.\n", -PTR_ERR(pfconf->trace_ctrl),
				pfconf->trace_ctrl_file_name);
		pfconf->trace_ctrl = NULL;
		ret = -1;
	}

	return ret;
}

/*
 * kdbg_ftrace_close_ctrl
 * close the control file.
 */
int kdbg_ftrace_close_ctrl(struct kdbg_ftrace_conf *pfconf)
{
	int ret  = 0;

	WARN_ON(!pfconf);

	ret = filp_close(pfconf->trace_ctrl, NULL);
	pfconf->trace_ctrl = NULL;
	return ret;
}

/*
 * kdbg_ftrace_write_ctrl
 * writes to the control file.
 */
int kdbg_ftrace_write_ctrl(struct kdbg_ftrace_conf *pfconf, char *buffer)
{
	int ret = 0;
	mm_segment_t oldfs;
	struct file *fp = NULL;

	WARN_ON(!pfconf);

	fp = pfconf->trace_ctrl;

	if (!(fp && fp->f_op && fp->f_op->write)) {
		error("Trace Ctrl Error, Writing %s.\n", pfconf->trace_ctrl_file_name);
		return -1;
	}

	/*
	 * kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	oldfs = get_fs();
	set_fs(KERNEL_DS);

	ret = fp->f_op->write(fp, buffer, strlen(buffer),  &fp->f_pos);

	if (ret < 0) {
		error("Trace Ctrl Error %d, Writing %s.\n", -ret, pfconf->trace_ctrl_file_name);

		/* restore kernel segment */
		set_fs(oldfs);
		return -1;
	}

	/* restore kernel segment */
	set_fs(oldfs);
	return 0;
}


/*
 * kdbg_ftrace_write_ctrl
 * reads from the control file.
 */
int kdbg_ftrace_read_ctrl(struct kdbg_ftrace_conf *pfconf, char *buffer)
{
	int ret = 0;
	mm_segment_t oldfs;
	struct file *fp = NULL;

	WARN_ON(!pfconf);

	fp = pfconf->trace_ctrl;

	if (!(fp && fp->f_op && fp->f_op->read)) {
		error("Trace Ctrl Error, Reading %s.\n", pfconf->trace_ctrl_file_name);
		return -1;
	}

	/*
	 * kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	oldfs = get_fs();
	set_fs(KERNEL_DS);

	ret = fp->f_op->read(fp, buffer, TRACE_MAX_PRINT - 1,  &fp->f_pos);

	if (ret < 0) {
		error("Trace Ctrl Error %d, Reading %s.\n", -ret, pfconf->trace_ctrl_file_name);

		/* restore kernel segment */
		set_fs(oldfs);
		return -1;
	}

	/* restore kernel segment */
	set_fs(oldfs);
	return 0;
}

#ifdef CONFIG_KDEBUGD_FTRACE_USER_BACKTRACE
static inline void kdbg_ftrace_save_stack_trace_user(struct stack_trace *trace)
{
	struct kdbg_ftrace_conf *pfconf = &fconf;

	/* add last n functions in ring buffer */
	store_user_bt((struct kbdg_bt_trace *)trace, pfconf->trace_call_depth);
}

/* need to define it for saving the user space stack trace
   it is called by ftrace_trace_userstack in trace/trace.c */
void kdbg_save_stack_trace_user(struct stack_trace *trace)
{
	/*
	 * Trace user stack if we are not a kernel thread
	 */
	if (current->mm)
		kdbg_ftrace_save_stack_trace_user(trace);

	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}
#endif /* CONFIG_KDEBUGD_FTRACE_USER_BACKTRACE */
