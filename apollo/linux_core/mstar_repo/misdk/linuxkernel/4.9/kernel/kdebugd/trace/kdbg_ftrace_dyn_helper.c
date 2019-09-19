/*
 * kdbg_ftrace_dyn_helper.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

/* Include in ftrace.c */
#include <kdebugd.h>
#include <trace/kdbg_ftrace_helper.h>

/* ftrace configuration helper functions */

/*
 * kdbg_ftrace_get_trace_pid
 * gets the current trace pid.
 *
 * = 0 indicates swapper pid
 * < 0 indicates no pid set
 * > 0 indicates normal pid
 *
 */
int kdbg_ftrace_get_trace_pid(struct kdbg_ftrace_conf *pfconf)
{
#if 0
	struct ftrace_pid *fpid, *safe;
	int pid = 0;

	mutex_lock(&ftrace_lock);

	/* reset trace_pid and trace_num_pid */
	memset(pfconf->trace_pid, 0, FTRACE_MAX_TRACE_PIDS * sizeof(pfconf->trace_pid[0]));
	pfconf->trace_num_pid = 0;

	if (list_empty(&ftrace_pids)) {
		/* no pid */
		mutex_unlock(&ftrace_lock);
		return -1;
	}

	list_for_each_entry_safe(fpid, safe, &ftrace_pids, list) {
		struct pid *ppid = fpid->pid;

		if (ppid == ftrace_swapper_pid) {
			/* swapper tasks */
			pid = 0;
		} else if (ppid) {
			pid = pid_vnr(ppid);
		} else {
			/* no pid */
			pid  = -1;
			break;
		}
		/* check overrun */
		if (pfconf->trace_num_pid >= FTRACE_MAX_TRACE_PIDS)
			break;
		/* populate trace_pid */
		pfconf->trace_pid[pfconf->trace_num_pid++] = pid;
	}

	mutex_unlock(&ftrace_lock);
	return 0;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(kdbg_ftrace_get_trace_pid);

/*
 * kdbg_ftrace_set_trace_pid
 * sets the specified trace pid.
 *
 * = 0 indicates swapper pid
 * < 0 indicates no pid set
 * > 0 indicates normal pid
 *
 */
int kdbg_ftrace_set_trace_pid(int val)
{
#if 0
	return ftrace_pid_add(val);
#else
	return 0;
#endif
}
EXPORT_SYMBOL(kdbg_ftrace_set_trace_pid);

/*
 * kdbg_ftrace_reset_trace_pid
 * resets the trace pid.
 */
int kdbg_ftrace_reset_trace_pid(struct kdbg_ftrace_conf *pfconf, int reset)
{

#if 0
	struct ftrace_pid *fpid, *safe;

	mutex_lock(&ftrace_lock);

	if (reset) {
		/* reset trace_pid and trace_num_pid */
		memset(pfconf->trace_pid, 0, FTRACE_MAX_TRACE_PIDS * sizeof(pfconf->trace_pid[0]));
		pfconf->trace_num_pid = 0;
	}

	if (list_empty(&ftrace_pids)) {
		mutex_unlock(&ftrace_lock);
		return -1;
	}

	list_for_each_entry_safe(fpid, safe, &ftrace_pids, list) {
		struct pid *pid = fpid->pid;

		clear_ftrace_pid_task(pid);

		list_del(&fpid->list);
		kfree(fpid);
	}

	ftrace_update_pid_func();
	ftrace_startup_enable(0);

	mutex_unlock(&ftrace_lock);

	return 0;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(kdbg_ftrace_reset_trace_pid);

void kdbg_ftrace_enable(void)
{
	mutex_lock(&ftrace_lock);

	last_ftrace_enabled = !!ftrace_enabled;

	/* enable ftrace_enabled */
	ftrace_enabled = 1;

	/* we are starting ftrace again */
	if (ftrace_ops_list != &ftrace_list_end) {
		update_ftrace_function();
		ftrace_startup_sysctl();
	}

	mutex_unlock(&ftrace_lock);
}
EXPORT_SYMBOL(kdbg_ftrace_enable);

void kdbg_ftrace_disable(void)
{
	mutex_lock(&ftrace_lock);

	last_ftrace_enabled = !!ftrace_enabled;

	/* disable ftrace_enabled */
	ftrace_enabled = 0;

	/* stopping ftrace calls (just send to ftrace_stub) */
	ftrace_trace_function = ftrace_stub;
	ftrace_shutdown_sysctl();

	mutex_unlock(&ftrace_lock);
}
EXPORT_SYMBOL(kdbg_ftrace_disable);

#ifdef CONFIG_DYNAMIC_FTRACE

/*
 * kdbg_ftrace_set_ftrace_filter
 * sets the specified function name as filter to be traced.
 */
int kdbg_ftrace_set_ftrace_filter(unsigned char *buf)
{
	char *alt_buf = NULL, *alt_buf_start = NULL;

	if (!buf)
		return -1;

	/* copy of the buffer is made as ftrace_set_filter modifies the buffer */
	alt_buf = kstrdup(buf, GFP_KERNEL);
	if (!alt_buf)
		return -1;

	alt_buf_start = alt_buf;
	ftrace_set_filter(&global_ops, alt_buf_start, strlen(alt_buf), 0);

	kfree(alt_buf);
	return 0 ;
}
EXPORT_SYMBOL(kdbg_ftrace_set_ftrace_filter);

/*
 * kdbg_ftrace_set_ftrace_notrace
 * sets the specified function name as filter not to be traced.
 */
int kdbg_ftrace_set_ftrace_notrace(unsigned char *buf)
{
	char *alt_buf = NULL, *alt_buf_start = NULL;

	if (!buf)
		return -1;

	/* copy of the buffer is made as ftrace_set_notrace modifies the buffer */
	alt_buf = kstrdup(buf, GFP_KERNEL);
	if (!alt_buf)
		return -1;

	alt_buf_start = alt_buf;
	ftrace_set_notrace(&global_ops, alt_buf_start, strlen(alt_buf), 0);

	kfree(alt_buf);
	return 0 ;
}
EXPORT_SYMBOL(kdbg_ftrace_set_ftrace_notrace);

/*
 * kdbg_ftrace_reset_ftrace_filter
 * resets the filter to be traced.
 */
void kdbg_ftrace_reset_ftrace_filter(void)
{
	ftrace_set_filter(&global_ops, NULL, 0, 1);
}
EXPORT_SYMBOL(kdbg_ftrace_reset_ftrace_filter);

/*
 * kdbg_ftrace_reset_ftrace_notrace
 * resets the filter not to be traced.
 */
void kdbg_ftrace_reset_ftrace_notrace(void)
{
	ftrace_set_notrace(&global_ops, NULL, 0, 1);
}
EXPORT_SYMBOL(kdbg_ftrace_reset_ftrace_notrace);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * kdbg_ftrace_set_ftrace_graph_filter
 * sets the ftrace graph filter.
 */
int kdbg_ftrace_set_ftrace_graph_filter(char *buf)
{
	int ret = 0;
	char *alt_buf = NULL, *alt_buf_start = NULL;

	if (!buf)
		return -1;

	/* copy of the buffer is made as ftrace_set_filter modifies the buffer */
	alt_buf = kstrdup(buf, GFP_KERNEL);
	if (!alt_buf)
		return -1;

	alt_buf_start = alt_buf;

	mutex_lock(&graph_lock);

	if (ftrace_graph_count >= FTRACE_GRAPH_MAX_FUNCS) {
		mutex_unlock(&graph_lock);
		kfree(alt_buf);
		return -1;
	}

	/* ftrace_set_func function internally enables ftrace_graph_filter_enabled */
	ret = ftrace_set_func(ftrace_graph_funcs, &ftrace_graph_count, FTRACE_GRAPH_MAX_FUNCS,
			alt_buf_start);

	mutex_unlock(&graph_lock);

	kfree(alt_buf);

	return ret;
}
EXPORT_SYMBOL(kdbg_ftrace_set_ftrace_graph_filter);

/*
 * kdbg_ftrace_reset_ftrace_graph_filter
 * resets the ftrace graph filter.
 */
void kdbg_ftrace_reset_ftrace_graph_filter(void)
{
	mutex_lock(&graph_lock);
	ftrace_graph_count = 0;
	memset(ftrace_graph_funcs, 0, sizeof(ftrace_graph_funcs));
	mutex_unlock(&graph_lock);
}
EXPORT_SYMBOL(kdbg_ftrace_reset_ftrace_graph_filter);
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_PROFILER
/*
 * kdbg_ftrace_profile_start
 * starts the profiling.
 */
int kdbg_ftrace_profile_start(void)
{
	int ret = 0;

	mutex_lock(&ftrace_profile_lock);

	/* start the profiling if not already started */
	if (!ftrace_profile_enabled) {
		ret = ftrace_profile_init();
		if (ret < 0) {
			mutex_unlock(&ftrace_profile_lock);
			return -1;
		}

		ret = register_ftrace_profiler();
		if (ret < 0) {
			mutex_unlock(&ftrace_profile_lock);
			return -1;
		}
		ftrace_profile_enabled = 1;
	}

	mutex_unlock(&ftrace_profile_lock);
	return 0;
}

/*
 * kdbg_ftrace_profile_stop
 * stops the profiling.
 */
int kdbg_ftrace_profile_stop(void)
{

	mutex_lock(&ftrace_profile_lock);

	/* stop the profiling if already started */
	if (ftrace_profile_enabled) {
		ftrace_profile_enabled = 0;
		/*
		 * unregister_ftrace_profiler calls stop_machine
		 * so this acts like an synchronize_sched.
		 */
		unregister_ftrace_profiler();
	}

	mutex_unlock(&ftrace_profile_lock);

	return 0;
}

void kdbg_ftrace_profile_dump_header(void)
{
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	menu("  Function                               "
		"Hit    Time            Avg             s^2\n"
		"  --------                               "
		"---    ----            ---             ---\n");
#else
	menu("  Function                               Hit\n"
		"  --------                               ---\n");
#endif
}

static void print_graph_duration(unsigned long long duration)
{
	unsigned long nsecs_rem = do_div(duration, 1000);
    /* log10(ULONG_MAX) + '\0' */
	char msecs_str[21];
	char nsecs_str[5];
	int len;
	int i;

	snprintf(msecs_str, sizeof(msecs_str), "%lu", (unsigned long) duration);

	/* Print msecs */
	menu("%s", msecs_str);

	len = strlen(msecs_str);

	/* Print nsecs (we don't want to exceed 7 numbers) */
	if (len < 7) {
		snprintf(nsecs_str, 8 - len, "%03lu", nsecs_rem);
		menu(".%s", nsecs_str);
		len += strlen(nsecs_str);
	}

	menu(" us ");

	/* Print remaining spaces to fit the row's width */
	for (i = len; i < 7; i++)
		menu(" ");
}


/*
 * kdbg_ftrace_profile_dump_stat
 * dump the profiling data.
 */
int kdbg_ftrace_profile_dump_stat(void *v)
{
	struct ftrace_profile *rec = (struct ftrace_profile *)v;
	char str[KSYM_SYMBOL_LEN];
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	unsigned long long avg;
	unsigned long long stddev;
#endif

	mutex_lock(&ftrace_profile_lock);

	/* we raced with function_profile_reset() */
	if (unlikely(rec->counter == 0)) {
		mutex_unlock(&ftrace_profile_lock);
		return -1;
	}

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);
	menu("  %-30.30s  %10lu", str, rec->counter);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	menu("    ");
	avg = rec->time;
	do_div(avg, rec->counter);

	/* Sample standard deviation (s^2) */
	if (rec->counter <= 1)
		stddev = 0;
	else {
		stddev = rec->time_squared - rec->counter * avg * avg;
		/*
		 * Divide only 1000 for ns^2 -> us^2 conversion.
		 * trace_print_graph_duration will divide 1000 again.
		 */
		do_div(stddev, (rec->counter - 1) * 1000);
	}

	print_graph_duration(rec->time);
	menu("    ");
	print_graph_duration(avg);
	menu("    ");
	print_graph_duration(stddev);
#endif

	menu("\n");
	mutex_unlock(&ftrace_profile_lock);
	return 0;
}
#endif /* CONFIG_FUNCTION_PROFILER */
