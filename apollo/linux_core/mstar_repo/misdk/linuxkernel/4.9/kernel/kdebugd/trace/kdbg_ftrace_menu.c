/*
 * kdbg_ftrace_menu.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by rajesh bhagat(rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */
#include <linux/kernel.h>
#include <kdebugd.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kallsyms.h>
#include <linux/ftrace.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/ctype.h>

#include "kdbg_util.h"
#include <trace/kdbg_ftrace_helper.h>
#include <trace/kdbg-ftrace.h>

/* default ftrace configuration values */
struct kdbg_ftrace_conf fconf = {
#ifndef CONFIG_FTRACE_BENCHMARK
	CONFIG_FTRACE_DEFAULT_TRACER,
#else
	"nop",
#endif
	{0},
	0,
	NULL,
	0,
	NULL,
	0,
	NULL,
	0,
	NULL,
	0,
	CONFIG_FTRACE_DEFAULT_TRACE_MODE,
	CONFIG_FTRACE_DEFAULT_TRACE_OUTPUT_NUM_LINES,
	CONFIG_FTRACE_DEFAULT_TRACE_OUTPUT_FILE_NAME,
	NULL,
	{0},
	NULL,
	{0},
#ifdef CONFIG_SMP
	CONFIG_FTRACE_DEFAULT_TRACE_CPU_MASK,
#else
	NR_CPUS,
#endif
	E_FTRACE_TRACE_STATE_IDLE,
	{{"function", FTRACE_TRACE_UNAVAILABLE},
		{"function_graph", FTRACE_TRACE_UNAVAILABLE},
		{"sched_switch", FTRACE_TRACE_UNAVAILABLE},
		{"wakeup", FTRACE_TRACE_UNAVAILABLE},
		{"irqsoff", FTRACE_TRACE_UNAVAILABLE},
		{"preemptoff", FTRACE_TRACE_UNAVAILABLE},
		{"stack", FTRACE_TRACE_PSEUDO},
		{"kmemtrace", FTRACE_TRACE_PSEUDO},
		{"branch", FTRACE_TRACE_UNAVAILABLE},
		{"blk", FTRACE_TRACE_UNAVAILABLE},
		{"function_profile", FTRACE_TRACE_PSEUDO},
		{"events", FTRACE_TRACE_PSEUDO},
		{"bt_kernel", FTRACE_TRACE_PSEUDO},
		{"bt_user", FTRACE_TRACE_PSEUDO}
	},
#ifdef CONFIG_KDEBUGD_FTRACE_HR_CLOCK
	CONFIG_FTRACE_TIMESTAMP_NSEC_STATUS,
#else
	0,
#endif
	0,
	0
};


/*
 * is_pseudo_tracer
 * checks a tracer is pseudo or not.
 */
static int is_pseudo_tracer(struct kdbg_ftrace_conf *pfconf)
{
	int ctr = 0, found = 0;
	int len1 = 0, len2 = 0;

	WARN_ON(!pfconf);

	if (!pfconf->trace_name)
		return -1;

	/* check the list of tracers for pseudo flag */
	for (ctr = 0 ; pfconf->supported_list[ctr].trace_name && ctr < FTRACE_SUPPORTED_TRACERS; ctr++) {
			len1 = strlen(pfconf->supported_list[ctr].trace_name);
			len2 = strlen(pfconf->trace_name);
			if (!strncmp(pfconf->supported_list[ctr].trace_name, pfconf->trace_name,
						len1 > len2 ? len1 : len2)) {
				found = 1;
				break;
			}
	}

	/* return 1 if psuedo, else 0 */
	if (found && pfconf->supported_list[ctr].available == FTRACE_TRACE_PSEUDO)
		return 1;
	else
		return 0;

}

/* forward declaration of get_buffer */
static char *get_buffer(void);

/*
 * append_func_list
 * allocates func_list and appends the func_name to func_list.
 */
static int append_func_list(char **func_list, char *func_name, int *size)
{
	int len_buf = 0;
	int len_list = 0;
	int len_total = 0, len_used = 0, len_unused = 0;

	if (func_name == NULL || size == NULL) {
		error("Invalid Params, func_name %p, size %p\n", func_name, size);
		return -1;
	}

	/* initialize len_buf and len_list */
	len_buf = strlen(func_name);
	if (*func_list == NULL) {
		*size = 1;

		*func_list = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE,
				KDEBUGD_FTRACE_FUNC_NAME_LEN, GFP_KERNEL);
		if (*func_list == NULL) {
			error("Kmalloc, len %d ..Failed.\n" , KDEBUGD_FTRACE_FUNC_NAME_LEN);
			return -1;
		}
		memset(*func_list, 0, KDEBUGD_FTRACE_FUNC_NAME_LEN);
	} else {
		len_list = strlen(*func_list);
	}

	debug("func_list %s, func_name %s, size %d\n", *func_list ? *func_list : "NULL",
			func_name, *size);

	/*
	 * len_total = number of buffers * size of each buffer
	 * i.e. KDEBUGD_FTRACE_FUNC_NAME_LEN
	 *
	 * len_used = extra 2 added for seperator ',' and EOL '\0'
	 *
	 * len_unused = len_total - len_used
	 */
	len_total = (*size * KDEBUGD_FTRACE_FUNC_NAME_LEN);
	len_used = len_list + len_buf + 2;
	len_unused  =  len_total - len_used;

	debug("len_buf %d, len_list %d, len_total %d, len_used %d, len_unused %d\n"
			, len_buf, len_list, len_total, len_used, len_unused);

	if (len_buf <= 0 || len_buf > KDEBUGD_FTRACE_FUNC_NAME_LEN) {
		error("Invalid Buffer Size, Range [> 0 and < %d]\n",
				KDEBUGD_FTRACE_FUNC_NAME_LEN);
		return -1;
	}

	if (len_unused <= 0) {
		*func_list = KDBG_MEM_DBG_KREALLOC(KDBG_MEM_FTRACE_MODULE, *func_list,
				(*size + 1) * KDEBUGD_FTRACE_FUNC_NAME_LEN, GFP_KERNEL);
		if (*func_list == NULL) {
			error("Krealloc, len %d ..Failed\n" ,
					(*size + 1) * KDEBUGD_FTRACE_FUNC_NAME_LEN);
			return -1;
		}
		(*size)++;
	}

	strncpy(*func_list + len_list, func_name, len_buf);
	*(*func_list + len_list + len_buf) = ',';
	*(*func_list + len_list + len_buf + 1) = '\0';

	return 0;
}

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * get_func_name
 * gets the func_name from user, and check its validity.
 */
static char *get_func_name(void)
{
	unsigned int func_addr = 0;
	static char *func_name;

	func_name = get_buffer();

	if (!func_name) {
		warn("Invalid Function Name.\n");
		return NULL;
	}

	if (func_name[0] == '\0') {
		warn("Invalid Function Name \"%s\".\n", func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return NULL;
	}

	/* reset support */
	if (strcasecmp(func_name, "None") == 0)
		return func_name;

	/* wildcard character support */
	if (func_name[0] == '*' || func_name[0] == '!' ||
			func_name[strlen(func_name) - 1] == '*') {
		return func_name;
	}

	/* checking validity of func_name */
	func_addr = kallsyms_lookup_name(func_name);

	if (!func_addr) {
		warn("Invalid Function Name \"%s\" [Name Lookup Failed].\n",
				func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return NULL;
	}

	debug("%08x : %s\n", func_addr, func_name);

	return func_name;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

/*
 * validate_func_list
 * validates the func_name in func_list
 *
 * if check is 1, it validates func exist in func_list or not.
 * else, it validates all the funcions in func_list with symbol table.
 *
 */
static int validate_func_list(char *func_list, int check, char *func)
{
	char *dup = NULL, *func_name = NULL;
	unsigned int func_addr = 0;
	char *dup_start = NULL;
	int len = 0;

	if (!func_list) {
		debug("Invalid Function List.\n");
		return -1;
	}

	len = strlen(func_list) + 1;
	dup = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);
	if (!dup) {
		debug("Kmalloc, len %d ..Failed.\n", len);
		return -1;
	}
	strncpy(dup, func_list, len);

	/* make a copy, strsep modifies pointer */
	dup_start = dup;
	while ((func_name = strsep(&dup_start, ","))) {

		len = strlen(func_name);
		if (!len)
			continue;

		if (check) {
			/* check func_name exists in func_list */
			if (func && !strcasecmp(func, func_name)) {
				KDBG_MEM_DBG_KFREE(dup);
				/* found */
				return 0;
			}
		} else {
			/* wildcard character support */
			if (func_name[0] == '*' || func_name[0] == '!' ||
					func_name[len - 1] == '*') {
				continue;
			}

			/* checking validity of func_name */
			func_addr = kallsyms_lookup_name(func_name);

			if (!func_addr) {
				debug("Kallsyms_lookup_name ...Failed.\n");
				KDBG_MEM_DBG_KFREE(dup);
				return -1;
			}
		}

	}
	KDBG_MEM_DBG_KFREE(dup);

	/* by default return not found in check mode */
	if (check)
		return -1;

	return 0;
}

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * handle_ftrace_list_black_filter
 * resets the notrace list which are not present in black list.
 *
 */
static int handle_ftrace_list_black_filter(struct kdbg_ftrace_conf *pfconf)
{
	char fname[KDEBUGD_FTRACE_FUNC_NAME_LEN];
	char *dup = NULL, *func_name = NULL;
	char *dup_start = NULL;
	int len = 0;
	int ret = 0;

	WARN_ON(!pfconf);

	if (!pfconf->trace_not_list || !pfconf->trace_black_list) {
		debug("Invalid Input List\n");
		return 0;
	}

	len = strlen(pfconf->trace_not_list) + 1;
	dup = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);
	if (!dup) {
		debug("Kmalloc, len %d ..Failed.\n", len);
		return -1;
	}
	strncpy(dup, pfconf->trace_not_list, len);

	/* make a copy, strsep modifies pointer */
	dup_start = dup;
	while ((func_name = strsep(&dup_start, ","))) {

		len = strlen(func_name);
		if (!len)
			continue;

		/* function present in black list should never be reset */
		ret = validate_func_list(pfconf->trace_black_list,
				E_VALIDATE_CHECK_MODE, func_name);
		if (!ret) {
			debug("Function \"%s\" Already In Black List.\n", func_name);
			continue;
		}

		/* prepend "!" to reset the filter */
		strncpy(fname, "!", sizeof("!"));
		strncat(fname, func_name, KDEBUGD_FTRACE_FUNC_NAME_LEN - 1);

		/* set trace_not_list */
		ret = kdbg_ftrace_set_ftrace_notrace(fname);
		if (ret < 0) {
			info("Set Black Function \"%s\" ..Failed.\n", fname);
			KDBG_MEM_DBG_KFREE(dup);
			return -1;
		}
		debug("Set Black Function \"%s\" ..Done\n", fname);
	}
	KDBG_MEM_DBG_KFREE(dup);
	return 0;
}



/*
 * validate_and_add_func_list
 *
 * validates the function list and adds the validated functions to
 * func list.
 */
static int validate_and_add_func_list(char **dest_func_list, int *dest_func_list_size, char *src_func_list)
{
	char *dup = NULL, *func_name = NULL;
	unsigned int func_addr = 0;
	char *dup_start = NULL;
	int len = 0;
	int ret = 0;

	if (!src_func_list) {
		debug("Invalid Function List.\n");
		return -1;
	}
	len = strlen(src_func_list) + 1;
	dup = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);
	if (!dup) {
		debug("Kmalloc, len %d ..Failed.\n", len);
		return -1;
	}
	strncpy(dup, src_func_list, len);
	/* make a copy, strsep modifies pointer */
	dup_start = dup;
	while ((func_name = strsep(&dup_start, ","))) {
		len = strlen(func_name);
		if (!len)
			continue;

		/* add to trace_back_list */
		/* checking validity of func_name, continue if not valid */
		func_addr = kallsyms_lookup_name(func_name);

		if (!func_addr) {
			debug("Kallsyms_lookup_name \"%s\" ...Failed.\n", func_name);
			continue;
		}


		ret = validate_func_list(*dest_func_list,
				E_VALIDATE_CHECK_MODE, func_name);

		if (!ret) {
			debug("Set Function \"%s\" Already Configured.\n",
					func_name);
			continue;
		}

		ret  = append_func_list(dest_func_list,
				func_name, dest_func_list_size);
		if (ret < 0) {
			error("Set Function \"%s\" ..Failed.\n", func_name);
			KDBG_MEM_DBG_KFREE(dup);
			return -1;
		}

		/* set dest_func_list to notrace */
		ret = kdbg_ftrace_set_ftrace_notrace(func_name);
		if (ret < 0) {
			error("Set Function \"%s\" ..Failed.\n", func_name);
			KDBG_MEM_DBG_KFREE(dup);
			return -1;
		}
		debug("Set Function \"%s\" ..Done.\n", func_name);

	}
	KDBG_MEM_DBG_KFREE(dup);
	return 0;
}
#endif

#ifdef CONFIG_FUNCTION_TRACER

/*
 * handle_ftrace_pid_preset
 * applies the trace pid preset filters beforehand.
 */
static void handle_ftrace_pid_preset(struct kdbg_ftrace_conf *pfconf, int *ftrace_preset_error)
{
	int ret = 0;
	int i = 0, j = 0;

	WARN_ON(!ftrace_preset_error);

	if (pfconf->trace_num_pid) {

		/* reset trace pid */
		kdbg_ftrace_reset_trace_pid(pfconf, E_RESET_DEFAULT_MODE);

		for (i = 0; i < pfconf->trace_num_pid; i++) {
			ret = kdbg_ftrace_set_trace_pid(pfconf->trace_pid[i]);

			if (ret < 0) {
				error("Preset Trace PID \"%d\" ..Failed.\n",
						pfconf->trace_pid[i]);
				*ftrace_preset_error = 1;


				/* delete trace pid from list */
				for (j = i ; j < pfconf->trace_num_pid - 1; j++)
					pfconf->trace_pid[j] = pfconf->trace_pid[j + 1];

				pfconf->trace_num_pid--;

				debug("Preset Trace PID \"");
				for (i = 0; i < pfconf->trace_num_pid; i++)
					debug("%d, ", pfconf->trace_pid[i]);

				debug("\" ..Done.\n");
			}
		}
		if (!(*ftrace_preset_error)) {
			debug("Preset Trace PID \"");
			for (i = 0; i < pfconf->trace_num_pid; i++)
				debug("%d, ", pfconf->trace_pid[i]);

			debug("\" ..Done.\n");
		}
	}
}

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * handle_ftrace_list_preset
 * applies the trace list preset filters beforehand.
 */
static void handle_ftrace_list_preset(struct kdbg_ftrace_conf *pfconf, int *ftrace_preset_error)
{
	int ret = 0;
	char *dup = NULL, *tok = NULL;
	char *dup_start = NULL;
	int len = 0;

	if (pfconf->trace_list) {
		/* reset ftrace filter */
		kdbg_ftrace_reset_ftrace_filter();

		len = strlen(pfconf->trace_list) + 1;
		dup = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);

		if (!dup) {
			error("Kmalloc, len %d ..Failed.\n", len);
			*ftrace_preset_error = 1;
		} else {
			strncpy(dup, pfconf->trace_list, len);

			/* make a copy, strsep modifies pointer */
			dup_start = dup;
			while ((tok = strsep(&dup_start, ","))) {

				if (!strlen(tok))
					continue;

				if (strstr(dup_start, tok)) {
					warn("Preset Trace Function \"%s\" ..Duplicated.\n", tok);
					*ftrace_preset_error = 1;
				} else {

					ret = kdbg_ftrace_set_ftrace_filter(tok);

					if (ret < 0) {
						error("Preset Trace Function \"%s\" ..Failed.\n", tok);
						*ftrace_preset_error = 1;
					} else {
						debug("Preset Trace Function \"%s\" ..Done.\n", tok);
					}
				}
			}
			KDBG_MEM_DBG_KFREE(dup);
		}
	}
}


/*
 * handle_ftrace_list_not_preset
 * applies the notrace list preset filters beforehand.
 */
static void handle_ftrace_list_not_preset(struct kdbg_ftrace_conf *pfconf, int *ftrace_preset_error)
{
	int ret = 0;
	char *dup = NULL, *tok = NULL;
	char *dup_start = NULL;
	int len = 0;

	if (pfconf->trace_not_list) {
		/* reset ftrace notrace */
		ret = handle_ftrace_list_black_filter(pfconf);
		if (ret) {
			error("Preset NoTrace List %s ..Failed.\n", pfconf->trace_not_list);
			*ftrace_preset_error = 1;
			return;
		}

		len = strlen(pfconf->trace_not_list) + 1;
		dup = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);

		if (!dup) {
			error("Kmalloc, len %d ..Failed.\n", len);
			*ftrace_preset_error = 1;
		} else {
			strncpy(dup, pfconf->trace_not_list, len);

			/* make a copy, strsep modifies pointer */
			dup_start = dup;
			while ((tok = strsep(&dup_start, ","))) {

				if (!strlen(tok))
					continue;

				if (strstr(dup_start, tok)) {
					warn("Preset NoTrace Function \"%s\" ..Duplicated.\n",
							tok);
					*ftrace_preset_error = 1;
				} else {

					ret = kdbg_ftrace_set_ftrace_notrace(tok);

					if (ret < 0) {
						error("Preset NoTrace Function \"%s\" ..Failed.\n",
								tok);
						*ftrace_preset_error = 1;
					} else {
						debug("Preset NoTrace Function \"%s\" ..Done.\n", tok);
					}
				}
			}
			KDBG_MEM_DBG_KFREE(dup);
		}
	}
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * handle_ftrace_graph_preset
 * applies the graph preset filters beforehand.
 */
static void handle_ftrace_graph_preset(struct kdbg_ftrace_conf *pfconf, int *ftrace_preset_error)
{
	int ret = 0;
	char *dup = NULL, *tok = NULL;
	char *dup_start = NULL;
	int len = 0;

	if (pfconf->trace_graph_list) {
		/* reset ftrace graph filter */
		kdbg_ftrace_reset_ftrace_graph_filter();

		len = strlen(pfconf->trace_graph_list) + 1;
		dup = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);

		if (!dup) {
			error("Kmalloc, len %d ..Failed.\n", len);
			*ftrace_preset_error = 1;
		} else {
			strncpy(dup, pfconf->trace_graph_list, len);

			/* make a copy, strsep modifies pointer */
			dup_start = dup;
			while ((tok = strsep(&dup_start, ","))) {

				if (!strlen(tok))
					continue;

				if (strstr(dup_start, tok)) {
					warn("Preset Trace Graph Function \"%s\" ..Duplicated.\n",
							tok);
					*ftrace_preset_error = 1;
				} else {

					ret = kdbg_ftrace_set_ftrace_graph_filter(tok);

					if (ret < 0) {
						error("Preset Trace Graph Function \"%s\" ..Failed.\n",
								tok);
						*ftrace_preset_error = 1;
					} else {
						debug("Preset Trace Function \"%s\" ..Done.\n", tok);
					}
				}
			}
			KDBG_MEM_DBG_KFREE(dup);
		}
	}
}
#endif
#endif

/*
 * handle_ftrace_preset_filter
 * applies the preset filters beforehand.
 */
static int handle_ftrace_preset_filter(struct kdbg_ftrace_conf *pfconf)
{
	int ftrace_preset_error = 0;

	/* preset pid */
	handle_ftrace_pid_preset(pfconf, &ftrace_preset_error);

#ifdef CONFIG_DYNAMIC_FTRACE
	/* preset trace list */
	handle_ftrace_list_preset(pfconf, &ftrace_preset_error);
	/* preset notrace list */
	handle_ftrace_list_not_preset(pfconf, &ftrace_preset_error);
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* preset graph list */
	handle_ftrace_graph_preset(pfconf, &ftrace_preset_error);
#endif
#endif

	if (ftrace_preset_error)
		return -1;

	return 0;
}
#endif

/*
 * get_buffer
 * gets the buffer from the user.
 */
static char *get_buffer(void)
{
	debugd_event_t event;
	static char *buf;
	int len = 0;

	debugd_get_event(&event);
	menu("\n");

	len = strlen(event.input_string) + 1;
	buf = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE, len, GFP_KERNEL);
	if (!buf) {
		error("Kmalloc, len %d ..Failed.\n", len);
		return NULL;
	}
	strncpy(buf, event.input_string, len);

	debug("buf %s\n", buf);

	return buf;
}

/*
 * find_tracer
 * This function is used to find tracer type.
 */
static int find_tracer(char *trace_name)
{
	int tracer = 0;

	if (!trace_name) {
		tracer = E_NOP_TRACER;
	} else if (!strcmp(trace_name, "nop")) {
		tracer = E_NOP_TRACER;
	} else if (!strcmp(trace_name, "function") ||
			!strcmp(trace_name, "function_graph")) {
		tracer = E_FUNCTION_TRACER;
	} else if (!strcmp(trace_name, "wakeup") || !strcmp(trace_name, "irqsoff")
			|| !strcmp(trace_name, "preemptoff")) {
		tracer = E_LATENCY_TRACER;
	} else if (!strcmp(trace_name, "stack")) {
		tracer = E_STACK_TRACER;
	} else if (!strcmp(trace_name, "blk")) {
		tracer = E_BLOCK_TRACER;
	} else if (!strcmp(trace_name, "function_profile")) {
		tracer = E_PROFILE_TRACER;
	} else if (!strcmp(trace_name, "kmemtrace")) {
		tracer = E_KMEM_TRACER;
	} else if (!strcmp(trace_name, "events")) {
		tracer = E_EVENTS_TRACER;
	} else if (!strcmp(trace_name, "bt_kernel")) {
		tracer = E_BT_KERNEL_TRACER;
	} else if (!strcmp(trace_name, "bt_user")) {
		tracer = E_BT_USER_TRACER;
	} else {
		tracer = E_OTHER_TRACER;
	}

	return tracer;
}

#ifdef CONFIG_FUNCTION_TRACER

/* handle_ftrace_show_filter
 * shows current ftrace filters.
 */
static void handle_ftrace_show_filter(struct kdbg_ftrace_conf *pfconf)
{
	int i;
	menu("Current Filter\n");
	menu("Trace PID   : ");
	if (pfconf->trace_num_pid) {
		for (i = 0; i < pfconf->trace_num_pid ; i++) {
			menu("%d", pfconf->trace_pid[i]);
			if (pfconf->trace_pid[i] == 0)
				menu("(Swapper Tasks)");
			menu(", ");
		}
	} else {
		menu("No PID");
	}
	menu(".\n");
#ifdef CONFIG_DYNAMIC_FTRACE
	menu("Trace List  : %s.\n",
			pfconf->trace_list ? pfconf->trace_list : "None");
	menu("Notrace List: %s.\n",
			pfconf->trace_not_list ? pfconf->trace_not_list : "None");
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	menu("Trace Graph List: %s.\n",
			pfconf->trace_graph_list ? pfconf->trace_graph_list : "None");
#endif
	menu("Trace Black List(Read-Only): %s.\n",
			pfconf->trace_black_list ? pfconf->trace_black_list : "None");
#endif
}


/*
 * handle_ftrace_reset_filter
 * handles reset of ftrace filters.
 */
static void handle_ftrace_reset_filter(struct kdbg_ftrace_conf *pfconf)
{
#ifdef CONFIG_FUNCTION_TRACER
	/* reset trace pid */
	kdbg_ftrace_reset_trace_pid(pfconf, E_RESET_CLEAR_MODE);
#ifdef CONFIG_DYNAMIC_FTRACE
	/* reset trace_list and trace_not list */
	kdbg_ftrace_reset_ftrace_filter();
	if (handle_ftrace_list_black_filter(pfconf)) {
		error("Reset NoTrace List %s ..Failed.\n", pfconf->trace_not_list);
		return;
	}
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	kdbg_ftrace_reset_ftrace_graph_filter();
#endif


	/* free the trace list */
	if (pfconf->trace_list) {
		pfconf->trace_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_list);
		pfconf->trace_list = NULL;
	}

	/* free the notrace list */
	if (pfconf->trace_not_list) {
		pfconf->trace_not_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_not_list);
		pfconf->trace_not_list = NULL;
	}

	/* free the trace graph list */
	if (pfconf->trace_graph_list) {
		pfconf->trace_graph_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_graph_list);
		pfconf->trace_graph_list = NULL;
	}
#endif
#endif
	info("Reset Filter ..Done.\n");
}

/*
 * handle_ftrace_pid_filter
 * handles set/reset of pid filters.
 */
static void handle_ftrace_pid_filter(struct kdbg_ftrace_conf *pfconf)
{
	int ret = 0;
	int i = 0, found_pid = 0;
	int is_number = 0;
	int pid = 0;
	struct pid *ppid = NULL;
	struct task_struct *tsk = NULL;

	menu("PID (-1 to Reset) ==>  ");
	pid = debugd_get_event_as_numeric(NULL, &is_number);
	menu("\n");

	/* reset trace pid */
	if (is_number && pid == -1) {
		kdbg_ftrace_reset_trace_pid(pfconf,
				E_RESET_CLEAR_MODE);
		info("Reset Trace PID ..Done.\n");
		return;
	}

	if (pid < 0) {
		warn("Invalid PID.\n");
		return;
	}

	/* set trace pid */
	/* check overrun of pfconf->trace_pid */
	if (pfconf->trace_num_pid >= FTRACE_MAX_TRACE_PIDS) {
		error("Set Trace PID ..Failed. [Range (>0 && <=FTRACE_MAX_TRACE_PIDS(%d))].\n", FTRACE_MAX_TRACE_PIDS);
		return;
	}

	for (i = 0; i < pfconf->trace_num_pid; i++) {
		if (pid == pfconf->trace_pid[i]) {
			found_pid = 1;
			break;
		}
	}

	/* break if, found in trace_pid array */
	if (found_pid) {
		warn("Trace PID \"%d\" Already Configured.\n", pid);
		return;
	}

	/* check for valid pid, if pid > 0*/
	if (pid) {
		ppid = find_get_pid(pid);

		if (!ppid) {
			warn("Invalid PID [No Thread Exists].\n");
			return;
		}

		tsk = get_pid_task(ppid, PIDTYPE_PID);

		if (!tsk) {
			warn("Invalid PID [No Thread Exists].\n");
			/* release ppid */
			put_pid(ppid);
			return;
		}

		/* release tsk and ppid */
		put_task_struct(tsk);
		put_pid(ppid);
	}

	ret = kdbg_ftrace_set_trace_pid(pid);

	if (ret < 0) {
		error("Set Trace PID \"%d\" ..Failed.\n", pid);
		return;
	}

	ret = kdbg_ftrace_get_trace_pid(pfconf);

	if (ret < 0) {
		warn("Get Trace PID ..Failed.\n");
		return;
	}

	info("Set Trace PID \"%d\" ..Done.\n", pid);
	return;
}

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * handle_ftrace_list_filter
 * handles set/reset ftrace list filters.
 */
static void handle_ftrace_list_filter(struct kdbg_ftrace_conf *pfconf)
{
	int ret = 0;
	char *func_name = NULL;

	menu("Function Name (\"None\" to Reset) ==>  ");
	func_name = get_func_name();
	if (!func_name)
		return;

	/* reset trace_list */
	if (strcasecmp(func_name, "None") == 0) {

		kdbg_ftrace_reset_ftrace_filter();

		/* free the trace list */
		if (pfconf->trace_list) {
			pfconf->trace_list_size = 0;
			KDBG_MEM_DBG_KFREE(pfconf->trace_list);
			pfconf->trace_list = NULL;
		}
		info("Reset Trace List ..Done.\n");
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	ret = validate_func_list(pfconf->trace_list,
			E_VALIDATE_CHECK_MODE, func_name);

	if (!ret) {
		warn("Trace Function \"%s\" Already Configured.\n",
				func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	/* set trace_list */
	ret = kdbg_ftrace_set_ftrace_filter(func_name);

	if (ret < 0) {
		error("Set Trace Function \"%s\" ..Failed.\n",
				func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	info("Set Trace Function \"%s\" ..Done.\n", func_name);

	ret  = append_func_list(&pfconf->trace_list, func_name,
			&pfconf->trace_list_size);

	if (ret < 0)
		error("Set Trace List \"%s\" ..Failed.\n", func_name);

	KDBG_MEM_DBG_KFREE(func_name);
	func_name = NULL;

	return;
}

/*
 * handle_ftrace_list_not_filter
 * handles set/reset ftrace list not filters.
 */
static void handle_ftrace_list_not_filter(struct kdbg_ftrace_conf *pfconf)
{
	int ret = 0;
	char *func_name = NULL;

	menu("Function Name (\"None\" to Reset) ==>  ");
	func_name = get_func_name();

	if (!func_name)
		return;

	/* reset trace_not_list */
	if (strcasecmp(func_name, "None") == 0) {

		ret = handle_ftrace_list_black_filter(pfconf);
		if (ret) {
			error("Reset NoTrace List %s ..Failed.\n", pfconf->trace_not_list);
			KDBG_MEM_DBG_KFREE(func_name);
			func_name = NULL;
			return;
		}

		/* free the notrace list */
		if (pfconf->trace_not_list) {
			pfconf->trace_not_list_size = 0;
			KDBG_MEM_DBG_KFREE(pfconf->trace_not_list);
			pfconf->trace_not_list = NULL;
		}

		info("Reset NoTrace List ..Done.\n");
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	ret = validate_func_list(pfconf->trace_not_list,
			E_VALIDATE_CHECK_MODE, func_name);

	if (!ret) {
		warn("NoTrace Function \"%s\" Already Configured.\n",
				func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	/* set trace_not_list */
	ret = kdbg_ftrace_set_ftrace_notrace(func_name);

	if (ret < 0) {
		error("Set NoTrace Function ..Failed.\n");
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	info("Set NoTrace Function \"%s\" ..Done.\n", func_name);

	ret  = append_func_list(&pfconf->trace_not_list, func_name,
			&pfconf->trace_not_list_size);

	if (ret < 0)
		error("Set Trace List \"%s\" ..Failed.\n", func_name);

	KDBG_MEM_DBG_KFREE(func_name);
	func_name = NULL;

	return;
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * handle_ftrace_graph_filter
 * handles set/reset of ftrace graph filter.
 */
static void handle_ftrace_graph_filter(struct kdbg_ftrace_conf *pfconf)
{
	int ret = 0;
	char *func_name = NULL;

	menu("Function Name (\"None\" to Reset) ==>  ");
	func_name = get_func_name();
	if (!func_name)
		return;

	/* reset trace_graph_list */
	if (strcasecmp(func_name, "None") == 0) {

		kdbg_ftrace_reset_ftrace_graph_filter();

		/* free the trace graph list */
		if (pfconf->trace_graph_list) {
			pfconf->trace_graph_list_size = 0;
			KDBG_MEM_DBG_KFREE(pfconf->trace_graph_list);
			pfconf->trace_graph_list = NULL;
		}
		info("Reset Trace Graph List ..Done.\n");
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	ret = validate_func_list(pfconf->trace_graph_list,
			E_VALIDATE_CHECK_MODE, func_name);

	if (!ret) {
		warn("Trace Function \"%s\" Already Configured.\n",
				func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	/* set trace_graph_list */
	ret = kdbg_ftrace_set_ftrace_graph_filter(func_name);

	if (ret < 0) {
		error("Set Trace Graph Function \"%s\" ..Failed.\n",
				func_name);
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
		return;
	}

	info("Set Trace Graph Function \"%s\" ..Done.\n",
			func_name);

	ret  = append_func_list(&pfconf->trace_graph_list,
			func_name, &pfconf->trace_graph_list_size);

	if (ret < 0) {
		error("Set Trace Graph List \"%s\" ..Failed.\n",
				func_name);
	}

	KDBG_MEM_DBG_KFREE(func_name);
	func_name = NULL;

	return;
}
#endif
#endif


/*
 * handle_ftrace_options
 * handles the ftrace options for diffrent tracers.
 * used for the case of dynamic ftrace.
 */
static int handle_ftrace_options(void)
{
	int operation = 0, ret = 0;
	struct kdbg_ftrace_conf *pfconf = &fconf;

#ifdef CONFIG_DYNAMIC_FTRACE
	info("Dynamic Ftrace Support Enabled.\n");
#else
	info("Dynamic Ftrace Support Disabled.\n");
#endif

	do {
		menu("\n");
		menu("-------------------------------------------------------------------\n");
		menu("1)  Ftrace Filter: Show Filter.\n");
		menu("2)  Ftrace Filter: Reset Filter.\n");
		menu("-------------------------------------------------------------------\n");
		menu("3)  Ftrace Filter: Set Trace PID.\n");
#ifdef CONFIG_DYNAMIC_FTRACE
		menu("4)  Ftrace Filter: Set Trace List.\n");
		menu("5)  Ftrace Filter: Set NoTrace List.\n");
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
		menu("6)  Ftrace Filter: Set Trace Graph List.\n");
#endif
#endif
		menu("-------------------------------------------------------------------\n");
		menu("99) Ftrace Filter: Exit Menu.\n");
		menu("-------------------------------------------------------------------\n");
		menu("\n");
		menu("Select Option  ==>  ");

		operation = debugd_get_event_as_numeric(NULL, NULL);
		menu("\n");

		switch (operation) {

		case E_SUBMENU_FILTER_SHOW:
			{
				handle_ftrace_show_filter(pfconf);
				break;
			}

		case E_SUBMENU_FILTER_RESET:
			{
				handle_ftrace_reset_filter(pfconf);
				break;
			}

		case E_SUBMENU_FILTER_PID:
			{
				handle_ftrace_pid_filter(pfconf);
				break;
			}

#ifdef CONFIG_DYNAMIC_FTRACE
		case E_SUBMENU_FILTER_LIST:
			{
				handle_ftrace_list_filter(pfconf);
				break;
			}

		case E_SUBMENU_FILTER_NOT_LIST:
			{
				handle_ftrace_list_not_filter(pfconf);
				break;
			}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
		case E_SUBMENU_FILTER_GRAPH_LIST:
			{
				handle_ftrace_graph_filter(pfconf);
				break;
			}

#endif
#endif
		case E_SUBMENU_FILTER_EXIT:
			break;

		default:
			menu("Invalid Option....\n");
			break;
		}

	} while (operation != E_SUBMENU_FILTER_EXIT);

	/* reset the tracer to nop */
	ret = kdbg_ftrace_set_trace_name("nop");
	if (ret) {
		error("Set Trace \"nop\" ..Failed.\n");
		return -1;
	}

	/* reset the trace buffers */
	kdbg_ftrace_reset();

	/* pseudo tracer */
	if (is_pseudo_tracer(pfconf)) {
		menu("Ftrace Filter menu exit....\n");
		/* return value is true - to show the kdebugd menu options */
		return 1;
	}

	/* set the desired tracer */
	ret = kdbg_ftrace_set_trace_name(pfconf->trace_name);
	if (ret) {
		error("Set Trace \"%s\" ..Failed.\n", pfconf->trace_name);
		return -1;
	}

	menu("Ftrace Filter menu exit....\n");
	/* return value is true - to show the kdebugd menu options */
	return 1;
}
#endif

/*
 * reset_transient_config
 * resets the transient configuration.
 */
int reset_transient_config(struct kdbg_ftrace_conf *pfconf, char *trace_name)
{
	int ret = 0;

	/* return if same tracer is set again */
	if (!strcmp(pfconf->trace_name, trace_name))
		return 0;

	/* reset the events */
	if (pfconf->trace_event_list) {
		ret = kdbg_ftrace_reset_event();
		if (ret)
			return -1;

		/* free the trace event list */
		pfconf->trace_event_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_event_list);
		pfconf->trace_event_list = NULL;
	}

	/* reset trace options */
	if (pfconf->trace_option) {
		/* set the function tracer */
		ret = kdbg_ftrace_set_trace_name(FTRACE_BT_TRACER);
		if (ret)
			return -1;

		/* reset the func_stack_trace option to disable the backtrace */
		ret = kdbg_ftrace_set_trace_option("no" FTRACE_BT_KERNEL_TRACE_OPTION);
		if (ret)
			return -1;

		/* reset the userstacktrace option to get the backtrace */
		ret = kdbg_ftrace_set_trace_option("no" FTRACE_BT_USER_TRACE_OPTION);
		if (ret)
			return -1;

		/* reset the tracer to nop */
		ret = kdbg_ftrace_set_trace_name("nop");
		if (ret)
			return -1;

		/* reset pfconf->trace_option */
		pfconf->trace_option = 0;
	}

	/* reset call depth */
	pfconf->trace_call_depth = 0;

	return 0;
}

/*
 * handle_ftrace_plugin
 * handles various plugins supported, i.e. defined in supported_list.
 */
static int handle_ftrace_plugin(char *trace_name, int *ftrace_disable)
{
	int ret = 0, found = 0;
	struct kdbg_ftrace_conf *pfconf = &fconf;
	int ctr = 0;
	debugd_event_t event;
	int tracer = 0;
	char trace_ctrl_file_name[FTRACE_MAX_FILENAME_LEN] = {0};
	int len1 = 0, len2 = 0;

	WARN_ON(!ftrace_disable);

	if (!trace_name) {
		warn("Invalid Trace Name.");
		return -1;
	}

	debug("Tracer Name \"%s\".\n", trace_name);

	/* list the available tracers */
	ret  = kdbg_ftrace_list(pfconf);
	if (ret) {
		warn("Trace List Empty.");
		return -1;
	}

	for (ctr = 0 ; pfconf->supported_list[ctr].trace_name && ctr < FTRACE_SUPPORTED_TRACERS; ctr++) {
		if (pfconf->supported_list[ctr].available == FTRACE_TRACE_AVAILABLE ||
				pfconf->supported_list[ctr].available == FTRACE_TRACE_PSEUDO) {
			debug("Available Tracer Name \"%s\".\n", pfconf->supported_list[ctr].trace_name);
			len1 = strlen(pfconf->supported_list[ctr].trace_name);
			len2 = strlen(trace_name);
			if (!strncmp(pfconf->supported_list[ctr].trace_name, trace_name,
						len1 > len2 ? len1 : len2)) {
				found = 1;
				break;
			}
		}
	}

	tracer = find_tracer(trace_name);

	if (!found) {
		warn("Invalid Trace Plugin \"%s\".\n", trace_name);
		warn("Not Supported On This Architecture.\n");
		return -1;
	}

	/* reset transient configuration */
	ret = reset_transient_config(pfconf, trace_name);

	if (ret) {
		error("Reset transient Configuration \"%s\" ..Failed.\n",
			trace_name);
		return -1;
	}

	debug("Reset Transient Configuration \"%s\" ..Done.\n",
		trace_name);

	/* special handling */
	switch (tracer) {

	case E_LATENCY_TRACER:
		{
			memset(trace_ctrl_file_name, 0, sizeof(trace_ctrl_file_name));
			menu("Disable Function Tracing(y/N) ==> ");
			debugd_get_event(&event);
			*ftrace_disable = event.input_string[0];
			menu("\n");
			break;
		}
	case E_STACK_TRACER:
		{

			strncpy(trace_ctrl_file_name, FTRACE_CTRL_STACK,
					FTRACE_MAX_FILENAME_LEN);
			trace_ctrl_file_name[FTRACE_MAX_FILENAME_LEN - 1] = '\0';
			break;
		}
	case E_BLOCK_TRACER:
		{
			char *blk_dev_name = NULL;

			menu("Block Device Name (sda/sdb/..) ==>  ");
			blk_dev_name = get_buffer();

			if (!blk_dev_name) {
				warn("Invalid Block Device Name.\n");
				return -1;
			}

			if (blk_dev_name[0] == '\0') {
				warn("Invalid Block Device Name.\n");
				KDBG_MEM_DBG_KFREE(blk_dev_name);
				blk_dev_name = NULL;
				return -1;
			}
			snprintf(trace_ctrl_file_name, FTRACE_MAX_FILENAME_LEN,
					FTRACE_CTRL_BLOCK, blk_dev_name);

			KDBG_MEM_DBG_KFREE(blk_dev_name);
			blk_dev_name = NULL;
			break;
		}

	case E_EVENTS_TRACER:
	{
		char *event_name = NULL;


		while (1) {
			kdbg_ftrace_available_subsys();

			menu("Trace Event List  : %s.\n",
					pfconf->trace_event_list ? pfconf->trace_event_list : "None");

			menu("Event Name (\"None\" to Reset, \"99\" to Exit) ==>  ");
			event_name = get_buffer();

			if (!event_name) {
				warn("Invalid Event Name.\n");
				continue;
			}

			if (event_name[0] == '\0') {
				warn("Invalid Event Name.\n");
				KDBG_MEM_DBG_KFREE(event_name);
				event_name = NULL;
				continue;
			}

			/* reset trace_event_list */
			if (strcasecmp(event_name, "None") == 0) {

				/* reset the events */
				ret = kdbg_ftrace_reset_event();
				if (ret) {
					error("Reset Trace Event List ..Failed.\n");
					KDBG_MEM_DBG_KFREE(event_name);
					event_name = NULL;
					continue;
				}

				/* free the trace event list */
				if (pfconf->trace_event_list) {
					pfconf->trace_event_list_size = 0;
					KDBG_MEM_DBG_KFREE(pfconf->trace_event_list);
					pfconf->trace_event_list = NULL;
				}
				info("Reset Trace Event List ..Done.\n");
				KDBG_MEM_DBG_KFREE(event_name);
				event_name = NULL;
				continue;
			}

			if (strcasecmp(event_name, "99") == 0) {
				KDBG_MEM_DBG_KFREE(event_name);
				event_name = NULL;

				if (!pfconf->trace_event_list) {
					warn("Invalid Trace List.\n");
					error("Set Trace \"%s\" ..Failed.\n", trace_name);
					return -1;
				}
				break;
			}

			/* check for tabspace */
			if (event_name[strlen(event_name) - 1] == 0x9)
				kdbg_ftrace_get_available_events(event_name);
			else {
				/* validate the event */
				ret = validate_func_list(pfconf->trace_event_list,
					E_VALIDATE_CHECK_MODE, event_name);

				if (!ret) {
					warn("Trace Event \"%s\" Already Configured.\n",
					event_name);
					KDBG_MEM_DBG_KFREE(event_name);
					event_name = NULL;
					continue;
				}
				/* set the event */
				if (kdbg_ftrace_set_event(event_name)) {
					error("Set Event Name \"%s\" ..Failed.\n", event_name);
					KDBG_MEM_DBG_KFREE(event_name);
					event_name = NULL;
					continue;
				}
				info("Set Event Name \"%s\" ..Done\n", event_name);

				ret  = append_func_list(&pfconf->trace_event_list, event_name,
						&pfconf->trace_event_list_size);

				if (ret < 0)
					error("Set Trace Event List \"%s\" ..Failed.\n", event_name);
			}

			KDBG_MEM_DBG_KFREE(event_name);
			event_name = NULL;
		}
		break;
	}
	case E_BT_KERNEL_TRACER:
	case E_BT_USER_TRACER:
	{
		int call_depth = 0;

		if (!pfconf->trace_list) {
			error("Invalid Filter.\n");
			info("Please add valid filter from Filter Menu.\n");
			return -1;
		}

		if (tracer == E_BT_USER_TRACER) {
			menu("Call Depth (Max %d) ==> ",
					FTRACE_BT_MAX_CALL_DEPTH);
			call_depth = debugd_get_event_as_numeric(NULL, NULL);
			menu("\n");

			if (call_depth <= 0 || call_depth > FTRACE_BT_MAX_CALL_DEPTH) {
				warn("Invalid Call Depth.\n");
				info("Using default Call Depth FTRACE_BT_DEFAULT_CALL_DEPTH(%d)\n", FTRACE_BT_DEFAULT_CALL_DEPTH);
				call_depth = FTRACE_BT_DEFAULT_CALL_DEPTH;
			}
			pfconf->trace_call_depth = call_depth;
			info("Set Call Depth \"%d\" ..Done.\n", pfconf->trace_call_depth);
		}

		break;
	}
	default:
		{
			memset(trace_ctrl_file_name, 0, sizeof(trace_ctrl_file_name));
			break;
		}
	}

	if (trace_ctrl_file_name[0] != '\0') {
		struct file *fp = NULL;

		fp = filp_open(trace_ctrl_file_name, O_RDWR, 0600);
		if (IS_ERR(fp)) {
			error("Trace Ctrl Error %ld, Opening %s.\n", -PTR_ERR(fp),
					trace_ctrl_file_name);
			error("Set Trace \"%s\" ..Failed.\n", trace_name);
			return -1;
		}
		filp_close(fp, NULL);

		snprintf(pfconf->trace_ctrl_file_name, FTRACE_MAX_FILENAME_LEN,
				trace_ctrl_file_name);
	} else {
		memset(pfconf->trace_ctrl_file_name, 0, sizeof(pfconf->trace_ctrl_file_name));
	}

	/* reset the tracer to nop */
	ret = kdbg_ftrace_set_trace_name("nop");
	if (ret) {
		error("Set Trace \"nop\" ..Failed.\n");
		return -1;
	}

	/* reset the trace buffers */
	kdbg_ftrace_reset();

	/* psuedo tracer */
	if (pfconf->supported_list[ctr].available == FTRACE_TRACE_PSEUDO) {
		pfconf->trace_name = trace_name;
		return 0;
	}

	/* set the desired tracer */
	ret = kdbg_ftrace_set_trace_name(trace_name);
	if (ret) {
		error("Set Trace \"%s\" ..Failed.\n", trace_name);
		return -1;
	}

	pfconf->trace_name = (char *) kdbg_ftrace_get_trace_name();

	return ret;
}

/*
 * handle_ftrace_setup_config
 * handles the basic configuration of the tracers.
 */
static int handle_ftrace_setup_config(void)
{
	int operation = 0, ret = 0;
	struct kdbg_ftrace_conf *pfconf = &fconf;
	unsigned long long ts1 = 0, ts2 = 0;
	int kdbg_ftrace_nsec_enabled = 0;

	ts1 = ts2 = sched_clock();
	do_div(ts1, NSEC_PER_MSEC);

	/* check if nsec feature is enabled by default */
	if ((ts1 * NSEC_PER_MSEC) != ts2) {
		/* enable the nsec feature flag */
		kdbg_ftrace_nsec_enabled = 1;
	}

	do {
		menu("\n");
		menu("-------------------------------------------------------------------\n");
		menu("1)  Ftrace Config: Show Configuration.\n");
		menu("-------------------------------------------------------------------\n");
		menu("2)  Ftrace Config: Set Output Mode.\n");
		menu("3)  Ftrace Config: Set Buffer Size (per cpu).\n");
		menu("4)  Ftrace Config: Set CPU Mask.\n");
		menu("5)  Ftrace Config: nSec feature on/off. (current : %s)\n",
				kdbg_ftrace_nsec_enabled ? "on" : (pfconf->trace_timestamp_nsec_status ? "on" : "off"));
		menu("-------------------------------------------------------------------\n");
		menu("99) Ftrace Config: Exit Menu.\n");
		menu("-------------------------------------------------------------------\n");
		menu("\n");
		menu("Select Option==>  ");

		operation = debugd_get_event_as_numeric(NULL, NULL);
		menu("\n");

		switch (operation) {

		case E_SUBMENU_OPTION_SHOW_CONFIG:
			{
				int cpu;
				menu("Current Configuration\n");
				menu("Output Mode          : Trace %s",
						(pfconf->trace_mode == FTRACE_OUTPUT_MODE_LOG)
						? "Log" : "Print");
				if (pfconf->trace_mode == FTRACE_OUTPUT_MODE_LOG) {
					menu(", FileName %s.\n", pfconf->trace_file_name);
				} else {
					menu(", Lines Per Print %d.\n",
							pfconf->trace_lines_per_print);
				}

				for_each_possible_cpu(cpu) {
				menu("Buffer Size (cpu:%d): %lu Kb.\n",
						cpu, pfconf->trace_buffer_size[cpu]);
				}
				menu("CPU Mask             : %d.\n",
						pfconf->trace_cpu_mask);
				menu("nSec Feature         : %s.\n",
						kdbg_ftrace_nsec_enabled ? "on" : (pfconf->trace_timestamp_nsec_status ? "on" : "off"));
				break;
			}

		case E_SUBMENU_OPTION_OUTPUT_MODE:
			{
				int mode = 0, lines_per_print = 0;
				char *file_name = NULL;
				struct file *fp = NULL;

				menu("Output Mode(1.Trace Print 2.Trace Log) ==> ");
				mode = debugd_get_event_as_numeric(NULL, NULL);
				menu("\n");

				if (mode < 0 || (mode != FTRACE_OUTPUT_MODE_PRINT &&
							mode != FTRACE_OUTPUT_MODE_LOG)) {
					warn("Invalid Mode.\n");
					break;
				}

				if (mode == FTRACE_OUTPUT_MODE_LOG) {
					menu("File Name ==> ");
					file_name = get_buffer();

					/* check validity of filename */
					if (!file_name) {
						warn("Invalid File Name.\n");
						break;
					}

					if (file_name[0] == '\0') {
						warn("Invalid File Name \"%s\".\n", file_name);
						KDBG_MEM_DBG_KFREE(file_name);
						break;
					}

					fp = filp_open(file_name, O_CREAT | O_TRUNC | O_WRONLY
							| O_LARGEFILE, 0600);
					if (IS_ERR(fp)) {
						warn("Invalid File Name \"%s\" [Unable to Create/Access].\n",
								file_name);
						KDBG_MEM_DBG_KFREE(file_name);
						break;
					}
					filp_close(fp, NULL);

					strncpy(pfconf->trace_file_name, file_name,
							sizeof(pfconf->trace_file_name) - 1);
					pfconf->trace_file_name[sizeof(pfconf->trace_file_name) - 1] = '\0';

					KDBG_MEM_DBG_KFREE(file_name);

					info("Set File Name \"%s\" ..Done.\n",
							pfconf->trace_file_name);
				} else {
					menu("Lines Per Print (0 for all, Max %d)  ==> ",
							FTRACE_MAX_OUTPUT_NUM_LINES);
					lines_per_print = debugd_get_event_as_numeric(NULL, NULL);
					menu("\n");

					if (lines_per_print < 0 || lines_per_print > FTRACE_MAX_OUTPUT_NUM_LINES) {
						warn("Invalid Lines Per Print, [Range (>=0 && <= FTRACE_MAX_OUTPUT_NUM_LINES(%d))].\n",
								FTRACE_MAX_OUTPUT_NUM_LINES);
						break;
					}

					pfconf->trace_lines_per_print = lines_per_print;
					info("Set Lines Per Print \"%d\" ..Done.\n",
							pfconf->trace_lines_per_print);
				}

				pfconf->trace_mode = mode;

				info("Set Output Mode \"%s\" ..Done.\n",
						(pfconf->trace_mode == FTRACE_OUTPUT_MODE_PRINT)
						? "Print" : "Log");
				break;
			}

		case E_SUBMENU_OPTION_BUFFER_SIZE:
			{
				int cpu = 0;
				long buffer_size_kb = 0;

				menu("Buffer size (per cpu) (Max %d Kb) ==> ",
						FTRACE_MAX_TRACE_SIZE_KB);
				buffer_size_kb = debugd_get_event_as_numeric(NULL, NULL);
				menu("\n");

				if (buffer_size_kb <= 0 ||
						buffer_size_kb > FTRACE_MAX_TRACE_SIZE_KB) {
					warn("Invalid Buffer size \"%lu Kb\", [Range (> 0 && <= FTRACE_MAX_TRACE_SIZE_KB(%d))].\n",
							buffer_size_kb, FTRACE_MAX_TRACE_SIZE_KB);
					break;
				}

				for_each_possible_cpu(cpu) {
				ret = kdbg_ftrace_set_trace_buffer_size(buffer_size_kb, cpu);
				if (ret < 0) {
					error("Set Buffer Size (per cpu) \"%lu Kb\" ..Failed.\n",
							buffer_size_kb);
					break;
				}
				pfconf->trace_buffer_size[cpu] = kdbg_ftrace_get_trace_buffer_size(cpu);
				info("Set Buffer Size (cpu:%d) \"%lu Kb\" ..Done.\n",
						cpu, pfconf->trace_buffer_size[cpu]);
				}
				break;
			}

		case E_SUBMENU_OPTION_CPU_MASK:
			{
#ifdef CONFIG_SMP
				int cpu_mask = 0;
				char cpu_mask_str[NR_CPUS + 1] = {0};
				int cpu_ctr, cpu_num;

				cpu_mask = pfconf->trace_cpu_mask;

				do {
					menu("------ Ftrace Config : Set CPU MasK ------\n");
					menu("NUM   CPU              STATE\n");
					menu("=== =======================================\n");
					for (cpu_ctr = 0 ; cpu_ctr < NR_CPUS ; cpu_ctr++) {
						menu(" %d      CPU%d          %s\n", cpu_ctr + 1, cpu_ctr,
												(cpu_mask & (1 << cpu_ctr)) ? "Trace Running" : "Tracing Not Running");
					}
					menu(" 99- Exit\n");
					menu("--------------- STATUS END ----------------\n");
					menu("*HELP*\n");
					menu("  A) This feature lets the user trace on specified CPUS\n");
					menu("  B) [TurnOn]  -->  The selected CPU is Used during the tracing feature of FTrace\n");
					menu("  C) [TurnOff] --> The selected CPU is not used in during tracing  feature of FTrace\n");
					menu("  D) Enter 0 for Disabling Ftrace tracing for all CPUs\n");
					menu("  E) Enter %d for Enabling FTrace tracing for All CPUs\n", NR_CPUS + 1);
					menu("To (toggle) Turn On/off feature enter corresponding number\n");
					menu("Select Option  ==>  ");
					cpu_num = debugd_get_event_as_numeric(NULL, NULL);
					menu("\n");

					if (cpu_num == E_SUBMENU_OPTION_EXIT)
						break;

					if (cpu_num < 0 || cpu_num > NR_CPUS + 1) {
						warn("Invalid CPU Mask, [Range (>= 0 && <= NR_CPUS + 1(%d))].\n",
								NR_CPUS + 1);
						continue;
					}

					if (cpu_num == 0)
						cpu_mask = 0;
					else if (cpu_num == NR_CPUS + 1) {
						cpu_mask = ((1 << NR_CPUS) - 1);
					} else if (cpu_num > 0 && cpu_num <= NR_CPUS) {
						if (cpu_mask & (1 << (cpu_num - 1)))
							cpu_mask &= ~(1 << (cpu_num - 1));
						else
							cpu_mask |= (1 << (cpu_num - 1));
					}

				} while (cpu_num != E_SUBMENU_OPTION_EXIT);

				debug("cpu_mask %d\n", cpu_mask);

				if (cpu_mask < 0 || cpu_mask > ((1 << NR_CPUS) - 1)) {
					warn("Invalid CPU Mask, [Range (>= 0 && <= NR_CPUS + 1(%d))].\n",
							((1 << NR_CPUS) - 1));
					break;
				}

				if (cpu_mask > 0 && cpu_mask <= 9)
					snprintf(cpu_mask_str, NR_CPUS + 1, "%d", cpu_mask);
				else
					snprintf(cpu_mask_str, NR_CPUS + 1, "%x", cpu_mask);

				cpu_mask_str[NR_CPUS] = '\0';

				if (cpu_mask_str[0] == '\0') {
					warn("Invalid CPU Mask, [Range (> 0 && <= NR_CPUS_BITS(%d))].\n",
							((1 << NR_CPUS) - 1));
					break;
				}

				ret = kdbg_ftrace_set_trace_cpu_mask(cpu_mask_str);
				if (ret) {
					error("Set CPU Mask \"%s\" ..Failed.\n", cpu_mask_str);
					break;
				}

				/* reset the tracer to nop */
				ret = kdbg_ftrace_set_trace_name("nop");
				if (ret) {
					error("Set Trace \"nop\" ..Failed.\n");
					break;
				}

				/* reset the trace buffers */
				kdbg_ftrace_reset();

				/* pseudo tracer */
				if (!is_pseudo_tracer(pfconf)) {
					/* set the desired tracer */
					ret = kdbg_ftrace_set_trace_name(pfconf->trace_name);
					if (ret) {
						error("Set Trace \"%s\" ..Failed.\n",
								pfconf->trace_name);
						break;
					}

					pfconf->trace_name = (char *) kdbg_ftrace_get_trace_name();
				}

				pfconf->trace_cpu_mask = kdbg_ftrace_get_trace_cpu_mask();
				info("Set CPU Mask \"%d\" ..Done.\n",
						pfconf->trace_cpu_mask);
				info("CPU Mask Not Supported : Stack, Memory, Block Queue Tracer, Events Tracer and Function Profile.\n");
#else
				warn("Not Supported On This Architecture.\n");
#endif
				break;
			}

		case E_SUBMENU_OPTION_TS_NSEC:
			{
#ifdef CONFIG_KDEBUGD_FTRACE_HR_CLOCK

				/* check if nsec feature is enabled by default */
				if (kdbg_ftrace_nsec_enabled) {
					warn("nSec feature By Default Enabled On This Architecture.\n");
					break;
				}

				pfconf->trace_timestamp_nsec_status	= !pfconf->trace_timestamp_nsec_status;
				menu("Set nSec feature \"%s\" ..Done.\n", pfconf->trace_timestamp_nsec_status ? "on" : "off");
#else
				warn("Not Supported On This Architecture.\n");
#endif
				break;
			}
		case E_SUBMENU_OPTION_EXIT:
			break;

		default:
			menu("Invalid Option....\n  ");
			break;


		}

	} while (operation != E_SUBMENU_OPTION_EXIT);

	menu("Ftrace Config menu exit....\n");

	/* return value is true - to show the kdebugd menu options */
	return 1;
}

/*
 * handle_ftrace_config
 * This function sets up the ftrace configuration.
 *
 */
static int handle_ftrace_config(void)
{
	int cpu;
#ifdef CONFIG_SMP
	char cpu_mask_str[NR_CPUS + 1] = {0};
#endif
	struct file *fp = NULL;
	struct kdbg_ftrace_conf *pfconf = &fconf;
#ifdef CONFIG_DYNAMIC_FTRACE
	int len = 0;
#endif
	int ftrace_config_error = 0;
	int tracer = 0;
#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE

	if (CONFIG_FTRACE_DEFAULT_TRACE_LIST[0] != '\0') {

		len = sizeof(CONFIG_FTRACE_DEFAULT_TRACE_LIST) + 1;
		pfconf->trace_list_size = len/KDEBUGD_FTRACE_FUNC_NAME_LEN + (len%KDEBUGD_FTRACE_FUNC_NAME_LEN ? 1 : 0);

		/* allocate trace_list, extra 1 added for seperator ',' */
		pfconf->trace_list = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE,
				pfconf->trace_list_size * KDEBUGD_FTRACE_FUNC_NAME_LEN, GFP_KERNEL);
		if (pfconf->trace_list == NULL) {
			error("Kmalloc, len %d ..Failed.\n" ,
					sizeof(CONFIG_FTRACE_DEFAULT_TRACE_LIST) + 1);
			pfconf->trace_list_size = 0;
			ftrace_config_error = 1;
		}
	}

	if (CONFIG_FTRACE_DEFAULT_TRACE_NOT_LIST[0] != '\0') {

		len = sizeof(CONFIG_FTRACE_DEFAULT_TRACE_NOT_LIST) + 1;
		pfconf->trace_not_list_size = len/KDEBUGD_FTRACE_FUNC_NAME_LEN + (len%KDEBUGD_FTRACE_FUNC_NAME_LEN ? 1 : 0);

		/* allocate trace_not_list, extra 1 added for seperator ',' */
		pfconf->trace_not_list = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE,
				pfconf->trace_not_list_size * KDEBUGD_FTRACE_FUNC_NAME_LEN, GFP_KERNEL);
		if (pfconf->trace_not_list == NULL) {
			error("Kmalloc, len %d ..Failed.\n" ,
					sizeof(CONFIG_FTRACE_DEFAULT_TRACE_NOT_LIST) + 1);
			pfconf->trace_not_list_size = 0;
			ftrace_config_error = 1;
		}
	}

	if (CONFIG_FTRACE_DEFAULT_TRACE_GRAPH_LIST[0] != '\0') {

		len = sizeof(CONFIG_FTRACE_DEFAULT_TRACE_GRAPH_LIST) + 1;
		pfconf->trace_graph_list_size = len/KDEBUGD_FTRACE_FUNC_NAME_LEN + (len%KDEBUGD_FTRACE_FUNC_NAME_LEN ? 1 : 0);

		/* allocate trace_graph_list, extra 1 added for seperator ',' */
		pfconf->trace_graph_list = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_FTRACE_MODULE,
				pfconf->trace_graph_list_size * KDEBUGD_FTRACE_FUNC_NAME_LEN, GFP_KERNEL);
		if (pfconf->trace_graph_list == NULL) {
			error("Kmalloc, len %d ..Failed.\n" ,
					sizeof(CONFIG_FTRACE_DEFAULT_TRACE_GRAPH_LIST) + 1);
			pfconf->trace_graph_list_size = 0;
			ftrace_config_error = 1;
		}
	}

	/* set the default filters */
	if (pfconf->trace_list) {
		strncpy(pfconf->trace_list, CONFIG_FTRACE_DEFAULT_TRACE_LIST,
				sizeof(CONFIG_FTRACE_DEFAULT_TRACE_LIST));
		strncat(pfconf->trace_list, ",", sizeof(","));

		if (validate_func_list(pfconf->trace_list, E_VALIDATE_DEFAULT_MODE, NULL)) {
			error("Preset Trace List \"%s\" ..Failed.\n", pfconf->trace_list);
			ftrace_config_error = 1;
		}
	}
	if (pfconf->trace_not_list) {
		strncpy(pfconf->trace_not_list, CONFIG_FTRACE_DEFAULT_TRACE_NOT_LIST,
				sizeof(CONFIG_FTRACE_DEFAULT_TRACE_NOT_LIST));
		strncat(pfconf->trace_not_list, ",", sizeof(","));

		if (validate_func_list(pfconf->trace_not_list, E_VALIDATE_DEFAULT_MODE, NULL)) {
			error("Preset NoTrace List \"%s\" ..Failed.\n", pfconf->trace_not_list);
			ftrace_config_error = 1;
		}
	}

	if (pfconf->trace_graph_list) {
		strncpy(pfconf->trace_graph_list, CONFIG_FTRACE_DEFAULT_TRACE_GRAPH_LIST,
				sizeof(CONFIG_FTRACE_DEFAULT_TRACE_GRAPH_LIST));
		strncat(pfconf->trace_graph_list, ",", sizeof(","));

		if (validate_func_list(pfconf->trace_graph_list, E_VALIDATE_DEFAULT_MODE,
					NULL)) {
			error("Preset Trace Graph List \"%s\" ..Failed.\n",
					pfconf->trace_graph_list);
			ftrace_config_error = 1;
		}
	}
#if 0 /*3.1.10 doesn't add these lines*/
	/* list of function which cause recursion, should not be traced */
	if (!pfconf->trace_black_list) {
		if (validate_and_add_func_list(&pfconf->trace_black_list, &pfconf->trace_black_list_size,
								CONFIG_FTRACE_DEFAULT_TRACE_BLACK_LIST)) {
			error("Preset Trace Black List \"%s\" ..Failed.\n",
							pfconf->trace_black_list);
			ftrace_config_error = 1;
		}
		debug("Preset Trace Black List \"%s\" ..Done.\n", pfconf->trace_black_list);
	}
#endif /*#if 0*/
#endif
	if (ftrace_config_error || handle_ftrace_preset_filter(pfconf))
		ftrace_config_error = 1;
#endif

	/* set the default trace_mode */
	if (pfconf->trace_mode < 0 || (pfconf->trace_mode != FTRACE_OUTPUT_MODE_PRINT
				&& pfconf->trace_mode != FTRACE_OUTPUT_MODE_LOG)) {
		error("Preset Trace Mode \"%d\" ..Failed.\n", pfconf->trace_mode);
		ftrace_config_error = 1;
	}

	/* set the default trace_lines_per_print */
	if (pfconf->trace_mode == FTRACE_OUTPUT_MODE_PRINT
			&& (pfconf->trace_lines_per_print < 0
				||	pfconf->trace_lines_per_print > FTRACE_MAX_OUTPUT_NUM_LINES)) {
		error("Preset Trace Lines Per Print \"%d\" ..Failed. [Range (>=0 && <= FTRACE_MAX_OUTPUT_NUM_LINES(%d))]\n",
				pfconf->trace_lines_per_print, FTRACE_MAX_OUTPUT_NUM_LINES);
		ftrace_config_error = 1;
	}

	/* set the default trace_file_name */
	if (pfconf->trace_mode == FTRACE_OUTPUT_MODE_LOG) {
		if (pfconf->trace_file_name[0] == '\0') {
			error("Preset Trace File Name \"%s\" ..Failed.\n",
					pfconf->trace_file_name);
			ftrace_config_error = 1;
		} else {
			fp = filp_open(pfconf->trace_file_name, O_CREAT | O_TRUNC |
					O_WRONLY | O_LARGEFILE, 0600);
			if (IS_ERR(fp)) {
				error("Preset Trace File Name \"%s\" ..Failed.\n",
						pfconf->trace_file_name);
				ftrace_config_error = 1;
			} else {
				filp_close(fp, NULL);
			}
		}
	}

	/* set the default trace_buffer_size */
	for_each_possible_cpu(cpu) {
	pfconf->trace_buffer_size[cpu] = CONFIG_FTRACE_DEFAULT_TRACE_SIZE_KB;

	if (pfconf->trace_buffer_size[cpu] <= 0 ||
			pfconf->trace_buffer_size[cpu] > FTRACE_MAX_TRACE_SIZE_KB) {
		error("Preset Buffer Size (cpu:%d) \"%lu Kb\" ..Failed. [Range (> 0 && <= FTRACE_MAX_TRACE_SIZE_KB(%d))].\n",
				cpu, pfconf->trace_buffer_size[cpu], FTRACE_MAX_TRACE_SIZE_KB);
		ftrace_config_error = 1;
	} else {
		/* set the default trace_buffer_size */
		if ((kdbg_ftrace_set_trace_buffer_size(pfconf->trace_buffer_size[cpu], cpu) < 0) ||
				kdbg_ftrace_stop()) {
			error("Preset Buffer Size (cpu:%d) \"%lu Kb\" ..Failed.\n",
					cpu, pfconf->trace_buffer_size[cpu]);
			ftrace_config_error = 1;
		}
	}
      }
#ifdef CONFIG_SMP
	/* set the default trace_cpu_mask */
	if (pfconf->trace_cpu_mask <= 0
			|| pfconf->trace_cpu_mask > ((1 << NR_CPUS) - 1)) {
		error("Preset CPU Mask \"%d\" ..Failed. [Range (> 0 && <= NR_CPUS_BITS(%d))]\n",
				pfconf->trace_cpu_mask, ((1 << NR_CPUS) - 1));
		ftrace_config_error = 1;
	} else {

		/* check that configured CPU mask is less that max supported */
		if (pfconf->trace_cpu_mask < ((1 << NR_CPUS) - 1))
			pfconf->trace_cpu_mask = ((1 << NR_CPUS) - 1);

		if (pfconf->trace_cpu_mask > 0 && pfconf->trace_cpu_mask <= 9)
			snprintf(cpu_mask_str, NR_CPUS + 1, "%d", pfconf->trace_cpu_mask);
		else
			snprintf(cpu_mask_str, NR_CPUS + 1, "%x", pfconf->trace_cpu_mask);
		cpu_mask_str[NR_CPUS] = '\0';

		if (kdbg_ftrace_set_trace_cpu_mask(cpu_mask_str)) {
			error("Preset CPU Mask \"%s\" ..Failed.\n", cpu_mask_str);
			ftrace_config_error = 1;
		}
	}
#endif

	/* set the default tracer */
	if (pfconf->trace_name && pfconf->trace_name[0] == '\0')
		pfconf->trace_name = "nop";

	tracer = find_tracer(pfconf->trace_name);

	if (!ftrace_config_error) {
		/* cant set blk and pseudo tracers at init time */
		if (tracer == E_BLOCK_TRACER || is_pseudo_tracer(pfconf) || kdbg_ftrace_set_trace_name(pfconf->trace_name)) {
			/* set the tracer to nop and continue to menu */
			warn("Preset Trace \"%s\" ..Failed. Continuing With No Tracer..", pfconf->trace_name);
			pfconf->trace_name = "nop";
			(void) kdbg_ftrace_set_trace_name(pfconf->trace_name);
		}
	}

	if (ftrace_config_error) {
		error("Ftrace Config Init ..Failed.\n");
		return -1;
	}
	return 0;
}

#ifdef CONFIG_FTRACE_BENCHMARK
/*
 * handle_ftrace_benchmark_cmds
 */
static int handle_ftrace_benchmark_cmds(void)
{
	int operation = 0;
	int current_tracer = 0;
	struct kdbg_ftrace_conf *pfconf = &fconf;
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
	char tracer_nop[5] = "nop";

	do {
		menu("\n");
		menu("-------------------------------------------------------------------\n");
		menu("1)  Ftrace Benchmark: Show Benchmark Configuration.\n");
		menu("-------------------------------------------------------------------\n");
		menu("2)  Ftrace Benchmark: Set Benchmark Configuration.\n");
		menu("3)  Ftrace Benchmark: Start Benchmarking.\n");
		menu("4)  Ftrace Benchmark: Stop Benchmarking.\n");
		menu("-------------------------------------------------------------------\n");
		menu("99) Ftrace Benchmark: Exit Menu.\n");
		menu("-------------------------------------------------------------------\n");
		menu("\n");
		menu("Select Option==>  ");

		operation = debugd_get_event_as_numeric(NULL, NULL);
		menu("\n");

		switch (operation) {

		case E_SUBMENU_BENCHMARK_SHOW_CONFIG:
			{
				menu("Current Configuration");
				menu("\nIterations      : %ld", pbconf->iterations);
				menu("\nCount           : %d", pbconf->count);
				menu("\n");
				break;
			}
		case E_SUBMENU_BENCHMARK_SET_CONFIG:
			{
				handle_ftrace_benchmark_config();
				break;
			}
		case E_SUBMENU_BENCHMARK_START:
			{
				if (pbconf->state == E_FTRACE_BENCHMARK_START) {
					warn("Benchmark Running..\n");
					break;
				}

				/* Default "nop" tracer is set in benchmark, if tracer is nop get trace name from user
				 *  else if trace is selected from Ftrace menu.
				 */
				if (!strcmp(pfconf->trace_name, "nop")) {
					int ctr = 0;
					int found = 0;
					int len1 = 0, len2 = 0;
					char *trace_name = NULL;

					warn("Trace Not Configured.\n");

					menu("Supported Tracer List:\n");
					for (ctr = 0; ctr < FTRACE_SUPPORTED_TRACERS; ctr++) {
						if (ctr && !(ctr % 5))
							menu("\n");
						menu("%16s\t", pfconf->supported_list[ctr].trace_name);
					}
					menu("\n");

					menu("Trace Name (\"99\" to Exit) ==> ");

					/* get trace name*/
					trace_name = get_buffer();

					if (trace_name[0] == '\0') {
						warn("Invalid Trace Name.\n");
						/* free trace name */
						KDBG_MEM_DBG_KFREE(trace_name);
						trace_name = NULL;
						continue;
					}

					if (strcasecmp(trace_name, "99") == 0) {
						/* free trace name */
						KDBG_MEM_DBG_KFREE(trace_name);
						trace_name = NULL;
						continue;
					}

					/* Search trace name in supported tracer list */
					len2 = strlen(trace_name);
					for (ctr = 0 ; pfconf->supported_list[ctr].trace_name && ctr < FTRACE_SUPPORTED_TRACERS; ctr++) {
						len1 = strlen(pfconf->supported_list[ctr].trace_name);
						if (!strncmp(pfconf->supported_list[ctr].trace_name, trace_name,
									len1 > len2 ? len1 : len2)) {
							found = 1;
							break;
						}
					}

					/* Invalid tracer name Entered */
					if (!found) {
						warn("Invalid Trace Plugin \"%s\".\n", trace_name);
						/* free trace name */
						KDBG_MEM_DBG_KFREE(trace_name);
						trace_name = NULL;
						continue;
					}

					/* update tracer name */
					strncpy(pbconf->trace_name, trace_name, len2 + 1);

					if (trace_name) {
						/* free trace name */
						KDBG_MEM_DBG_KFREE(trace_name);
						trace_name = NULL;
					}

					menu("Set Trace Plugin \"%s\" Done.\n", pbconf->trace_name);

				} else {
					/* update tracer name */
					strncpy(pbconf->trace_name, pfconf->trace_name, strlen(pfconf->trace_name) + 1);
				}

				/*Find current tracer*/
				current_tracer = find_tracer(pbconf->trace_name);

				if (current_tracer == E_NOP_TRACER) {
					warn("Trace Not Configured.\n");
					continue;
				}

				switch (current_tracer) {

				case E_FUNCTION_TRACER:
				case E_STACK_TRACER:
				case E_PROFILE_TRACER:
				case E_BT_KERNEL_TRACER:
				case E_BT_USER_TRACER:
				case E_BLOCK_TRACER:
				case E_OTHER_TRACER:
					{
						pbconf->bench_th = kthread_run(trace_benchmark_thread, NULL, "trace_benchmark_test");
						if (IS_ERR(pbconf->bench_th)) {
							error("Failed to create benchmark test thread ");
							pbconf->bench_th = NULL;
							break;
						}
						break;
					}
				case E_LATENCY_TRACER:
					{
						pbconf->bench_th = kthread_run(trace_benchmark_latency_thread, NULL, "trace_benchmark_latency_test");
						if (IS_ERR(pbconf->bench_th)) {
							error("Failed to create benchmark latency test thread ");
							pbconf->bench_th = NULL;
							break;
						}

						/* incase of wakeup tracer,schedule another thread to wakeup first thread */
						if (!strcmp(pbconf->trace_name, "wakeup")) {
							pbconf->wakeup_th = kthread_run(trace_wakeup_thread, NULL, "trace_wakeup_test");
							if (IS_ERR(pbconf->wakeup_th)) {
								error("Failed to create ftrace wakeup test thread ");
								pbconf->wakeup_th = NULL;
								break;
							}
						}
						break;
					}
				case E_KMEM_TRACER:
					{
						pbconf->bench_th = kthread_run(trace_benchmark_memory_thread, NULL, "trace_benchmark_memory_test");
						if (IS_ERR(pbconf->bench_th)) {
							error("Failed to create benchmark memory test thread ");
							pbconf->bench_th = NULL;
							break;
						}
						break;
					}
				case E_EVENTS_TRACER:
					{
						pbconf->bench_th = kthread_run(trace_benchmark_event_thread, NULL, "trace_benchmark_event_test");
						if (IS_ERR(pbconf->bench_th)) {
							error("Failed to create benchmark event(timer) test thread ");
							pbconf->bench_th = NULL;
							break;
						}
						break;
					}
				default:
					break;
				} /* switch (current_tracer) */

				/*set benchmark state to start*/
				pbconf->state = E_FTRACE_BENCHMARK_START;

				break;
				}
		case E_SUBMENU_BENCHMARK_STOP:
			{
				/*Stop Running Thread*/
				if (pbconf->state != E_FTRACE_BENCHMARK_START) {
					warn("Benchmark Not Started.\n");
					break;
				}

				info("Stopping benchmark....\n");
				if (pbconf->bench_th) {
					kthread_stop(pbconf->bench_th);
					pbconf->bench_th = NULL;
				}

				/*when current tracer is wakeup, also stop running wakeup thread*/
				if (!strcmp(pbconf->trace_name, "wakeup") && (pbconf->wakeup_th)) {
					kthread_stop(pbconf->wakeup_th);
					pbconf->wakeup_th = NULL;
				}

				info("Benchmark Stopped.\n");

				/*set benchmark state to start*/
				pbconf->state = E_FTRACE_BENCHMARK_STOP;

				/* set tracer name to "nop" */
				strncpy(pbconf->trace_name, tracer_nop, strlen(tracer_nop) + 1);

				break;
			}
		default:
			break;
		}   /*switch(operation)*/
	} while (operation != E_SUBMENU_BENCHMARK_EXIT);

	/* inform user regarding the state of the trace */
	if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START)
		info("Trace \"%s\" Running.", pfconf->trace_name);

	return 1;
}
#endif /* CONFIG_FTRACE_BENCHMARK */

/*
 * handle_ftrace_cmds
 * This function is used to handle ftrace commands provided by the user.
 *
 */
static int handle_ftrace_cmds(void)
{
	int operation = 0, ret = 0;
	struct kdbg_ftrace_conf *pfconf = &fconf;
	char *trace_name = NULL;
	int current_tracer = 0;
	static int init_defaults = 1, tracer_select = 1, ftrace_menu_exit;
	static int ftrace_disable;
	debugd_event_t event;
	int is_number = 0;
#ifdef CONFIG_FTRACE_BENCHMARK
	struct kdbg_ftrace_benchmark_conf *pbconf = &bconf;
#endif

	/* set default configuration, if setting defaults fail, exit ftrace*/
	if (init_defaults) {
		if (handle_ftrace_config()) {
			/* return value is true - to show the kdebugd menu options */
			return 1;
		}
		init_defaults = 0;
	}

	do {

		current_tracer = find_tracer(pfconf->trace_name);

		if (ftrace_menu_exit || pfconf->trace_state != E_FTRACE_TRACE_STATE_START) {
			ftrace_menu_exit = 0;
			menu("\n");
			menu("-------------------------------------------------------------------\n");

			if (current_tracer == E_NOP_TRACER) {
				menu("Ftrace Plugin Not Configured, Select Plugin (from %c to %c)",
						tolower(E_MENU_TRACE_FUNCTION), tolower(E_MENU_TRACE_MAX - 1));
			} else {
				menu("Current Ftrace Plugin \"%s\"", pfconf->trace_name);
				if (tracer_select) {
					menu(", Select Plugin (from %c to %c)", tolower(E_MENU_TRACE_FUNCTION),
							tolower(E_MENU_TRACE_MAX - 1));
				}
			}
			menu(".\n");

			menu("-------------------------------------------------------------------\n");
			/* tracers */
			menu("a)  Plugin: Trace Function. [F]\n");
			menu("b)  Plugin: Trace Function Graph. [F]\n");
			menu("c)  Plugin: Trace Sched_Switch (Process Context Switches).\n");
			menu("d)  Plugin: Trace Wakeup (Scheduling Latency).\n");
			menu("e)  Plugin: Trace Irqs_Off (Interrupts-off Latency).\n");
			menu("f)  Plugin: Trace Preempt_Off (Preemption-off Latency).\n");
			/* advanced tracers */
			menu("g)  Plugin: Trace Stack.\n");
			menu("h)  Plugin: Trace Memory (Slab Allocations).\n");
			menu("i)  Plugin: Trace Branches (Likely-Unlikely).\n");
			menu("j)  Plugin: Trace Block Queues.\n");
			/* enhanced tracers */
			menu("k)  Plugin: Trace Profile. [F]\n");
			menu("l)  Plugin: Trace Events.\n");
			menu("m)  Plugin: Trace Function (Kernel Backtrace).\n");
			menu("n)  Plugin: Trace Function (User Backtrace).\n");
			/* operations */
			menu("-------------------------------------------------------------------\n");
			menu("1)  Ftrace: Trace Start.\n");
			menu("2)  Ftrace: Trace Stop.\n");
			menu("3)  Ftrace: Trace Dump.\n");
			menu("4)  Ftrace: Trace Reset.\n");
			menu("5)  Ftrace: Trace Filter.\n");
			menu("6)  Ftrace: Trace Setup.\n");
#ifdef CONFIG_FTRACE_BENCHMARK
			menu("7)  Ftrace: Trace Benchmark.\n");
#endif
			menu("-------------------------------------------------------------------\n");
			menu("99) Ftrace: Exit Menu.\n");
			menu("-------------------------------------------------------------------\n");
		}
		menu("\n");
		menu("Select Option  ==>  ");

		operation = debugd_get_event_as_numeric(&event, &is_number);
		menu("\n");

		if (!is_number) {
			if (strlen(event.input_string) > 1) {
				/* invalid input */
				operation = -1;
			} else {
				operation = toupper(event.input_string[0]);
			}
		}

		switch (operation) {

		case E_MENU_TRACE_START:
			{

				if (current_tracer == E_NOP_TRACER) {
					warn("Trace Not Configured.\n");
					break;
				}

				if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START) {
					warn("Trace \"%s\" Already Started.\n",
							pfconf->trace_name);
					break;
				}

				/* special handling */
				switch (current_tracer) {

				case E_LATENCY_TRACER:
					{
#ifdef CONFIG_TRACER_MAX_TRACE
						debug("Reset Trace Max Latency.\n");
						/* reset the max latency */
						kdbg_ftrace_set_trace_max_latency(0);
#endif
#ifdef CONFIG_FUNCTION_TRACER
						if ((ftrace_disable == 'y') ||
								(ftrace_disable == 'Y')) {
							debug("Disable Function Tracer.\n");
							kdbg_ftrace_disable();
						}
#endif
						break;
					}
#ifdef CONFIG_STACK_TRACER
				case E_STACK_TRACER:
					{
						debug("Enable Stack Tracer, Ctrl File \"%s\".\n",
								pfconf->trace_ctrl_file_name);
						debug("Reset Trace Max Stack.\n");
						/* reset the max stack size */
						kdbg_ftrace_set_trace_max_stack(0);
						break;
					}
#endif
				case E_BLOCK_TRACER:
					{
						/* selecting the tracer, sets up trace_ctrl_file_name */
						debug("Enable Block Tracer, Ctrl File \"%s\".\n",
								pfconf->trace_ctrl_file_name);
						break;
					}
#ifdef CONFIG_EVENT_TRACING
				case E_KMEM_TRACER:
					{
						debug("Enable Kmem Tracer, %s, %s\n", FTRACE_KMEM_KMALLOC_EVENT, FTRACE_KMEM_KFREE_EVENT);
						if (kdbg_ftrace_set_event(FTRACE_KMEM_KMALLOC_EVENT) || kdbg_ftrace_set_event(FTRACE_KMEM_KFREE_EVENT)) {
							error("Trace Start \"%s\" ..Failed.\n", pfconf->trace_name);
							continue;
						}
						break;
					}
#endif

				case E_BT_KERNEL_TRACER:
				case E_BT_USER_TRACER:
					{

						/* check the filters */
						if (!pfconf->trace_list) {
							error("Invalid Filter.\n");
							info("Please add valid filter from Filter Menu.\n");
							continue;
						}

						/* set the function tracer */
						ret = kdbg_ftrace_set_trace_name(FTRACE_BT_TRACER);
						if (ret) {
							error("Trace Start \"%s\" ..Failed.\n", pfconf->trace_name);
							continue;
						}

						/* check if already set */
						if (!pfconf->trace_option) {
							/* set the func_stack_trace option to get the backtrace */
							ret = kdbg_ftrace_set_trace_option(FTRACE_BT_KERNEL_TRACE_OPTION);
							if (ret) {
								error("Trace Start \"%s\" ..Failed.\n", pfconf->trace_name);
								continue;
							}

							if (current_tracer == E_BT_USER_TRACER) {
								/* set the userstacktrace option to get the backtrace */
								ret = kdbg_ftrace_set_trace_option(FTRACE_BT_USER_TRACE_OPTION);
								if (ret) {
									error("Trace Start \"%s\" ..Failed.\n", pfconf->trace_name);
									continue;
								}
							}
							/* set pfconf->trace_option flag */
							pfconf->trace_option = 1;
						}
						break;
					}
				default:
					break;
				}

				/* enable the trace control */
				if (pfconf->trace_ctrl_file_name[0] != '\0') {
					ret  = kdbg_ftrace_open_ctrl(pfconf);
					if (ret) {
						error("Trace Start \"%s\" ..Failed.\n",
								pfconf->trace_name);
						break;
					}

					ret = kdbg_ftrace_write_ctrl(pfconf, FTRACE_CTRL_ENABLE);
					if (ret) {
						error("Trace Start \"%s\" ..Failed.\n",
								pfconf->trace_name);
						break;
					}
				}

				/* reset the trace buffers */
				kdbg_ftrace_reset();

				/* Assuming it will pass */
				info("Trace Start \"%s\" ..Done.\n", pfconf->trace_name);

				ret = kdbg_ftrace_start();
				if (ret) {
					error("Trace Start \"%s\" ..Failed.\n",
							pfconf->trace_name);
					break;
				}
#ifdef CONFIG_FUNCTION_PROFILER
				if (current_tracer == E_PROFILE_TRACER) {
					ret = kdbg_ftrace_profile_start();
					if (ret) {
						error("Trace Start \"%s\" ..Failed.\n",
								pfconf->trace_name);
						break;
					}
				}
#endif

				/* change trace state to started */
				pfconf->trace_state = E_FTRACE_TRACE_STATE_START;
				break;
			}
		case E_MENU_TRACE_STOP:
			{

				if (current_tracer == E_NOP_TRACER) {
					warn("Trace Not Configured.\n");
					break;
				}

				if (pfconf->trace_state != E_FTRACE_TRACE_STATE_START) {
					if (pfconf->trace_state ==  E_FTRACE_TRACE_STATE_STOP) {
						warn("Trace \"%s\" Already Stopped.\n",
								pfconf->trace_name);
					} else if (pfconf->trace_state == E_FTRACE_TRACE_STATE_IDLE) {
						warn("Trace \"%s\" Never Started.\n",
								pfconf->trace_name);
					}
					break;
				}

				ret = kdbg_ftrace_stop();
				if (ret) {
					error("Trace Stop \"%s\" ..Failed.\n",
							pfconf->trace_name);
					break;
				}

#ifdef CONFIG_FUNCTION_PROFILER
				if (current_tracer == E_PROFILE_TRACER)
					kdbg_ftrace_profile_stop();
#endif
				info("Trace Stop \"%s\" ..Done.\n", pfconf->trace_name);

				/* special handling */
				switch (current_tracer) {
				case E_LATENCY_TRACER:
					{
#ifdef CONFIG_FUNCTION_TRACER
						if ((ftrace_disable == 'y') ||
								(ftrace_disable == 'Y')) {
							debug("Enable Function Tracer.\n");
							kdbg_ftrace_enable();
						}
#endif
						break;
					}
#ifdef CONFIG_STACK_TRACER
				case E_STACK_TRACER:
					{
						debug("Disable Stack Tracer, Ctrl File \"%s\".\n",
								pfconf->trace_ctrl_file_name);
						break;
					}
#endif
				case E_BLOCK_TRACER:
					{
						debug("Disable Block Tracer, Ctrl File \"%s\".\n",
								pfconf->trace_ctrl_file_name);
						break;
					}

#ifdef CONFIG_EVENT_TRACING
				case E_KMEM_TRACER:
					{
						debug("Disable Kmem Tracer, %s, %s\n", FTRACE_KMEM_KMALLOC_EVENT, FTRACE_KMEM_KFREE_EVENT);
						if (kdbg_ftrace_set_event("!" FTRACE_KMEM_KMALLOC_EVENT) || kdbg_ftrace_set_event("!" FTRACE_KMEM_KFREE_EVENT)) {
							error("Trace Stop \"%s\" ..Failed.\n", pfconf->trace_name);
							continue;
						}
						break;
					}
#endif

				default:
					break;
				}

#ifdef CONFIG_FTRACE_BENCHMARK
		/*if benchmark is running no other operation allowed except Benchmark itself*/
		if (pbconf->state == E_FTRACE_BENCHMARK_START && operation != E_MENU_TRACE_BENCHMARK) {
			warn("Benchmark Running..\n");
			continue;
		}
#endif

				/* disable the trace control */
				if (pfconf->trace_ctrl_file_name[0] != '\0') {
					ret = kdbg_ftrace_write_ctrl(pfconf, FTRACE_CTRL_DISABLE);
					if (ret) {
						error("Trace Stop \"%s\" ..Failed.\n",
								pfconf->trace_name);
						break;
					}

					ret = kdbg_ftrace_close_ctrl(pfconf);
					if (ret) {
						error("Trace Stop \"%s\" ..Failed.\n",
								pfconf->trace_name);
						break;
					}
				}

				/* change trace state to stopped */
				pfconf->trace_state = E_FTRACE_TRACE_STATE_STOP;
				break;
			}

		case E_MENU_TRACE_DUMP:
			{
#ifdef CONFIG_TRACER_MAX_TRACE
				unsigned long max_latency = 0;
#endif
#ifdef CONFIG_STACK_TRACER
				unsigned long max_stack_size = 0;
#endif

				if (current_tracer == E_NOP_TRACER) {
					warn("Trace Not Configured.\n");
					break;
				}

				if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START) {
					warn("Trace \"%s\" Running.", pfconf->trace_name);
					break;
				}

				/* change trace state to dump */
				pfconf->trace_state = E_FTRACE_TRACE_STATE_DUMP;

				info("Trace Dump \"%s\".\n", pfconf->trace_name);

				/* user backtrace tracer */
				if (current_tracer == E_MENU_TRACE_BT_USER) {
#ifdef CONFIG_ELF_MODULE
					info("ELF Module Enabled.");
#else
					info("ELF Module Disabled. Please enable it to get ELF information.");
#endif
				}

				ret = kdbg_ftrace_dump(pfconf);
				if (!ret) {
					info("Trace Dump \"%s\" ..Done.\n", pfconf->trace_name);

					/* special handling */
					switch (current_tracer) {
#ifdef CONFIG_TRACER_MAX_TRACE
					case E_LATENCY_TRACER:
						{
							max_latency = kdbg_ftrace_get_trace_max_latency();
							if (max_latency) {
								info("Trace Max Latency \"%lu us\".\n",
										max_latency);
							}
							break;
						}
#endif
#ifdef CONFIG_STACK_TRACER
					case E_STACK_TRACER:
						{
							max_stack_size = kdbg_ftrace_get_trace_max_stack();
							if (max_stack_size) {
								info("Trace Max Stack Size \"%lu\".\n",
										max_stack_size);
							}
							break;
						}
#endif
					default:
						break;

					}
				}
				/* change trace state to idle */
				pfconf->trace_state = E_FTRACE_TRACE_STATE_IDLE;
				break;
			}

		case E_MENU_TRACE_RESET:
			{
				if (current_tracer == E_NOP_TRACER) {
					warn("Trace Not Configured.\n");
					break;
				}

				if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START) {
					warn("Trace \"%s\" Running.", pfconf->trace_name);
					break;
				}

				/* reset the tracer to nop */
				ret = kdbg_ftrace_set_trace_name("nop");
				if (ret) {
					error("Set Trace \"nop\" ..Failed.\n");
					break;
				}

				/* reset the trace buffers */
				kdbg_ftrace_reset();

				info("Trace Reset \"%s\" ..Done.\n", pfconf->trace_name);

				pfconf->trace_name = (char *) kdbg_ftrace_get_trace_name();
				break;
			}

		case E_MENU_TRACE_FILTER:
			{

				if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START) {
					warn("Trace \"%s\" Running.", pfconf->trace_name);
					break;
				}
#ifdef CONFIG_FUNCTION_TRACER
				/* set ftrace options */
				handle_ftrace_options();
#else
				warn("Trace Filter Not Supported.\n");
#endif
				break;
			}
		case E_MENU_TRACE_SETUP:
			{

				if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START) {
					warn("Trace \"%s\" Running.", pfconf->trace_name);
					break;
				}

				handle_ftrace_setup_config();
				break;
			}

#ifdef CONFIG_FTRACE_BENCHMARK
		case E_MENU_TRACE_BENCHMARK:
			{
				/* handle ftrace benchmarking commnads */
				handle_ftrace_benchmark_cmds();
				break;
			}
#endif

		case E_MENU_TRACE_FUNCTION:
		case E_MENU_TRACE_FUNCTION_GRAPH:
		case E_MENU_TRACE_SCHED_SWITCH:
		case E_MENU_TRACE_WAKEUP:
		case E_MENU_TRACE_IRQS_OFF:
		case E_MENU_TRACE_PREEMPT_OFF:
			/* advanced tracers */
		case E_MENU_TRACE_STACK:
		case E_MENU_TRACE_MEMORY:
		case E_MENU_TRACE_BRANCH:
		case E_MENU_TRACE_BLOCK:
			/* enhanced tracers */
		case E_MENU_TRACE_PROFILE:
		case E_MENU_TRACE_EVENTS:
		case E_MENU_TRACE_BT_KERNEL:
		case E_MENU_TRACE_BT_USER:
			{
				if (pfconf->trace_state == E_FTRACE_TRACE_STATE_START) {
					warn("Trace \"%s\" Running.", pfconf->trace_name);
					break;
				}

				trace_name = pfconf->supported_list[operation - E_MENU_TRACE_FUNCTION].trace_name;

				ret = handle_ftrace_plugin(trace_name, &ftrace_disable);
				if (ret)
					break;

				info("Trace Plugin \"%s\" ..Done.\n", pfconf->trace_name);

				/* change trace state to idle */
				pfconf->trace_state = E_FTRACE_TRACE_STATE_IDLE;

				tracer_select = 0;
				break;
			}

		case E_MENU_TRACE_EXIT:
			break;

		default:
			menu("Invalid Option....\n  ");
			break;
		}
	} while (operation != E_MENU_TRACE_EXIT);
	menu("Ftrace menu exit....\n");

	ftrace_menu_exit = 1;
	/* return value is true - to show the kdebugd menu options */
	return 1;
}

/*
 * kdbg_ftrace_exit
 * unregister ftrace submenu with kdebugd and free all resources
 */
void kdbg_ftrace_exit(void)
{
#ifdef CONFIG_DYNAMIC_FTRACE
	struct kdbg_ftrace_conf *pfconf = &fconf;

	kdbg_ftrace_reset_ftrace_filter();

	/* reset trace_list */
	if (pfconf->trace_list) {
		pfconf->trace_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_list);
		pfconf->trace_list = NULL;
	}

	kdbg_ftrace_reset_ftrace_notrace();

	/* reset trace_not_list */
	if (pfconf->trace_not_list) {
		pfconf->trace_not_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_not_list);
		pfconf->trace_not_list = NULL;
	}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	kdbg_ftrace_reset_ftrace_graph_filter();

	/* free the trace graph list */
	if (pfconf->trace_graph_list) {
		pfconf->trace_graph_list_size = 0;
		KDBG_MEM_DBG_KFREE(pfconf->trace_graph_list);
		pfconf->trace_graph_list = NULL;
	}
#endif
#endif
	/* unregister ftrace */
	kdbg_unregister(KDBG_MENU_FTRACE);
}

/*
 * kdbg_ftrace_init
 * register ftrace submenu with kdebugd
 */
int kdbg_ftrace_init(void)
{
	kdbg_register("TRACE: Ftrace Support" KDEBUGD_FTRACE_VERSION_STRING,
			handle_ftrace_cmds,	NULL, KDBG_MENU_FTRACE);
	return 0;
}
