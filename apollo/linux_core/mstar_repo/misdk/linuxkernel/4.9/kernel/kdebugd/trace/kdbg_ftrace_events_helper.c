/*
 * kdbg_ftrace_events_helper.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

/* Include in trace_events.c */
#include <kdebugd.h>
#include "kdbg_util.h"
#include <trace/kdbg_ftrace_helper.h>


void kdbg_ftrace_available_subsys(void)
{
	struct event_subsystem *system = NULL;

	menu("----Available Subsystems----\n");
	list_for_each_entry(system, &event_subsystems, list) {
		menu("%s  ", system->name);
	}
	menu("\n");
}

int kdbg_ftrace_get_available_events(char *match)
{
	/*struct ftrace_event_call *call;*/
	struct trace_event_call *call;
	int ctr = 0;

	if (!match)
		return -1;

	mutex_lock(&event_mutex);
	list_for_each_entry(call, &ftrace_events, list) {

		if (!call->name || !call->class || !call->class->reg)
			continue;

		if (!strncmp(match, call->class->system, strlen(match) - 1)) {
			ctr++;
			menu("%10s:%-35s", call->class->system, call->name);
			if (ctr && !(ctr % 3))
				menu("\n");
		}
	}

	menu("\n");
	mutex_unlock(&event_mutex);
	return 0;
}

int kdbg_ftrace_set_event(char *buf)
{
	int set = 1;
	char *alt_buf = NULL, *alt_buf_start = NULL;
	struct trace_array *tr = top_trace_array();

	if (!buf)
		return -1;

	/* copy of the buf is made as ftrace_set_clr_event modifies the buf */
	alt_buf = kstrdup(buf, GFP_KERNEL);
	if (!alt_buf)
		return -1;

	if (*alt_buf == '!')
		set = 0;

	alt_buf_start = alt_buf;
	if (ftrace_set_clr_event(tr, alt_buf_start + !set, set)) {
		kfree(alt_buf);
		return -1;
	}

	kfree(alt_buf);
	return 0;
}

int kdbg_ftrace_reset_event(void)
{
	char buf[5] = "*:*";
	char *alt_buf = NULL, *alt_buf_start = NULL;
	struct trace_array *tr = top_trace_array();


	/* copy of the buf is made as ftrace_set_clr_event modifies the buf */
	alt_buf = kstrdup(buf, GFP_KERNEL);
	if (!alt_buf)
		return -1;

	if (ftrace_set_clr_event(tr, alt_buf_start, 0))
		return -1;

	kfree(alt_buf);
	return 0;
}


