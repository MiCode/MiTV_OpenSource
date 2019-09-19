/*
 * kdbg_ftrace_stat_helper.c
 *
 * Copyright (C) 2010 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */

/* Include in trace_stat.c */
#include <kdebugd.h>
#include "kdbg_util.h"
#include <trace/kdbg_ftrace_helper.h>
#include <linux/export.h>

#ifdef CONFIG_FUNCTION_PROFILER
int kdbg_ftrace_profile_dump(struct kdbg_ftrace_conf *pfconf)
{
	struct stat_session *node = NULL;
	struct tracer_stat  *ts = NULL;
	int event = 0, i = 0;
	void *stat = NULL;

	mutex_lock(&all_stat_sessions_mutex);
	list_for_each_entry(node, &all_stat_sessions, session_list) {

		ts = node->ts;

		/* filter only profile data */
		if (strncmp(node->ts->name, "function", 8))
			break;

		mutex_lock(&node->stat_mutex);
		stat = ts->stat_start(ts);
		if (!stat) {
			mutex_unlock(&node->stat_mutex);
			break;
		}

		/*
		 * Iterate over the tracer stat entries and display them.
		 */
		for (i = 1; ; i++) {
			stat = ts->stat_next(stat, i);

			if (!stat)
				break;

			/* print the header */
			if (i == 1) {
				info("CPU \"%c\" Profile..\n", node->ts->name[8]);
				kdbg_ftrace_profile_dump_header();
			}

			/* print the profile data */
			kdbg_ftrace_profile_dump_stat(stat);


			if (pfconf->trace_lines_per_print &&
					(i % pfconf->trace_lines_per_print == 0)) {
				menu("Press 99 To Stop Trace Dump, Else Continue.. ");
				event = debugd_get_event_as_numeric(NULL, NULL);
				menu("\n");
				if (event == 99)
					break;
			}
		}

		mutex_unlock(&node->stat_mutex);
	}
	mutex_unlock(&all_stat_sessions_mutex);
	return 0;
}
EXPORT_SYMBOL(kdbg_ftrace_profile_dump);
#endif
