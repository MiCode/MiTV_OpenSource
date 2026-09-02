#include <linux/device.h>
#include <linux/kallsyms.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/async.h>
#include <linux/suspend.h>
#include <linux/of.h>

/*
 * If you need to extend more item, you should
 *	1. add the item name in #define
 *	2. add the enum
 *	3. implement your own .parser and .defaults in struct parser_ops
 *
 * e.g.
 *	#define MSTAR_STR_FOO	"my-foo"
 *	enum {
 *		...
 *		OF_MSTAR_STR_FOO,
 *		OF_MSTAR_STR_MAX,
 *	};
 *
 *	const struct parser_ops sym_name[] = {
 *		[OF_MSTAR_STR_FOO] = {
 *			.node_name = MY_FOO_NAME,
 *			.parser    = parser_foo,
 *			.defaults  = default_foo,
 *		},
 *		...
 *	}
 * DONOT use as gloable variable, maybe race condiction
*/
#define MSTAR_STR_ROOT			"Mstar-STR"
#define MSTAR_STR_SUSPEND_STG1		"suspend-stage1"
#define MSTAR_STR_RESUME_STG1		"resume-stage1"
#define MSTAR_STR_SUSPEND_STG2		"suspend-stage2"
#define MSTAR_STR_RESUME_STG2		"resume-stage2"
#define MSTAR_STR_POLICY		"policy"
#define MSTAR_STR_SUSPEND_STG1_WF	"suspend-stage1-waitfor"
#define MSTAR_STR_RESUME_STG1_WF	"resume-stage1-waitfor"
#define MSTAR_STR_SUSPEND_STG2_WF	"suspend-stage2-waitfor"
#define MSTAR_STR_RESUME_STG2_WF	"resume-stage2-waitfor"

enum {
	OF_MSTAR_STR_ROOT = 0,
	OF_MSTAR_STR_MODULE,
	OF_MSTAR_STR_SUSPEND_1,
	OF_MSTAR_STR_RESUME_1,
	OF_MSTAR_STR_SUSPEND_2,
	OF_MSTAR_STR_RESUME_2,
	OF_MSTAR_STR_POLICY,
	OF_MSTAR_STR_STG1_SUSP_WF,
	OF_MSTAR_STR_STG1_RESM_WF,
	OF_MSTAR_STR_STG2_SUSP_WF,
	OF_MSTAR_STR_STG2_RESM_WF,
	OF_MSTAR_STR_MAX,
};

/* Add the .parser and .defaults here */
#define OF_MSTAR_OPS_DECLARE(sym_name, mod_name) 	\
	const struct parser_ops sym_name[] = {		\
		[OF_MSTAR_STR_ROOT] = {			\
			.node_name = MSTAR_STR_ROOT,	\
			.parser    = parser_root,	\
			.defaults  = root_default,	\
		},					\
		[OF_MSTAR_STR_MODULE] = {		\
			.node_name = mod_name,		\
			.parser    = parser_leaf,	\
			.defaults  = root_default,	\
		},					\
		[OF_MSTAR_STR_SUSPEND_1] = {		\
			.node_name = MSTAR_STR_SUSPEND_STG1,	\
			.parser    = parser_suspend_order, \
			.defaults  = suspend_default,	\
		},					\
		[OF_MSTAR_STR_RESUME_1] = {		\
			.node_name = MSTAR_STR_RESUME_STG1,		\
			.parser    = parser_resume_order, \
			.defaults  = resume_default,	\
		},					\
		[OF_MSTAR_STR_SUSPEND_2] = {		\
			.node_name = MSTAR_STR_SUSPEND_STG2,	\
			.parser    = parser_suspend_order_stage2, \
			.defaults  = NULL,		\
		},					\
		[OF_MSTAR_STR_RESUME_2] = {		\
			.node_name = MSTAR_STR_RESUME_STG2,	\
			.parser    = parser_resume_order_stage2, \
			.defaults  = NULL,		\
		},					\
		[OF_MSTAR_STR_POLICY] = {		\
			.node_name = MSTAR_STR_POLICY,\
			.parser    = parser_policy,	\
			.defaults  = NULL,		\
		},					\
		[OF_MSTAR_STR_STG1_SUSP_WF] = {		\
			.node_name = MSTAR_STR_SUSPEND_STG1_WF, \
			.parser    = parser_waitfor,\
			.defaults  = NULL,		\
		},					\
		[OF_MSTAR_STR_STG1_RESM_WF] = {		\
			.node_name = MSTAR_STR_RESUME_STG1_WF, \
			.parser    = parser_waitfor,\
			.defaults  = NULL,		\
		},					\
		[OF_MSTAR_STR_STG2_SUSP_WF] = {		\
			.node_name = MSTAR_STR_SUSPEND_STG2_WF, \
			.parser    = parser_waitfor,\
			.defaults  = NULL,		\
		},					\
		[OF_MSTAR_STR_STG2_RESM_WF] = {		\
			.node_name = MSTAR_STR_RESUME_STG2_WF, \
			.parser    = parser_waitfor,\
			.defaults  = NULL,		\
		},					\
	};

struct target {
	const char *name;
	struct dev_pm_ops *pm;
	struct device *dev;
	struct str_waitfor_dev *waitfor_dev;
	int (*fn_suspend)(struct device *dev);
	int (*fn_resume)(struct device *dev);
	int (*fn_suspend_stage2)(struct device *dev);
	int (*fn_resume_stage2)(struct device *dev);
};

struct parser_ops {
	const char *node_name;
	int (*parser)(struct device_node **root,
			struct device_node **leaf,
			struct target *targ,
			const char *item);
	int (*defaults)(struct target *targ);
};

static int parser_root(struct device_node **root,
			struct device_node **leaf,
			struct target *targ,
			const char *item)
{
	*root = of_find_node_by_name(NULL, item);
	if (!*root) {
		pr_err("%s: No [%s] defined in dts\n", targ->name, item);
		return -1;
	}

	return 0;
}

static int parser_leaf(struct device_node **root,
			struct device_node **leaf,
			struct target *targ,
			const char *item)
{
	*leaf = of_find_node_by_name(*root, item);
	if (!*leaf) {
		pr_err("%s: Mstar-STR, No [%s] defined in dts\n",
				targ->name, item);
		return -1;
	}

	return 0;
}

static int root_default(struct target *targ)
{
	pr_info("%s: Mstar-STR, use default setting\n", targ->name);
	targ->pm->suspend = targ->fn_suspend;
	targ->pm->resume = targ->fn_resume;

	return -1;
}

static int parser_suspend_order(struct device_node **root,
				struct device_node **leaf,
				struct target *targ,
				const char *item)
{
	struct property *prop;

	prop = of_find_property(*leaf, item, NULL);
	if (!prop) {
		pr_err("%s: Mstar-STR, No [%s] defined in dts\n",
				targ->name, item);
		return -1;
	}

	if (!strncmp((char *)prop->value, "noirq", strlen("noirq"))) {
		pr_info("%s: Mstar-STR, [%s] register to [suspend_noirq]\n",
				targ->name, item);
		targ->pm->suspend_noirq = targ->fn_suspend;
	} else if (!strncmp((char *)prop->value, "late", strlen("late"))) {
		pr_info("%s: Mstar-STR, [%s] register to [suspend_late]\n",
				targ->name, item);
		targ->pm->suspend_late = targ->fn_suspend;
	} else if (!strncmp((char *)prop->value, "normal", strlen("normal"))) {
		pr_info("%s: Mstar-STR, [%s] register to [suspend]\n",
				targ->name, item);
		targ->pm->suspend = targ->fn_suspend;
	} else {
		pr_info("%s: Mstar-STR, [%s] Unknow [%s],"
			"using default setting\n",
			targ->name, item, (char *)prop->value);
		return -1;
	}

	return 0;
}

static int parser_suspend_order_stage2(struct device_node **root,
					struct device_node **leaf,
					struct target *targ,
					const char *item)
{
	struct property *prop;

	prop = of_find_property(*leaf, item, NULL);
	if (!prop) {
		pr_info("%s: Mstar-STR, No [%s] defined in dts\n",
					targ->name, item);
		return -1;
	}

	if (!targ->fn_suspend_stage2) {
		pr_err("%s: Mstar-STR, [%s] callback function is not defined!"
			" please review your DTS\n", targ->name, item);
		return -1;
	}

	if (!strncmp((char *)prop->value, "noirq", strlen("noirq"))) {
		pr_info("%s: Mstar-STR, [%s] register to [suspend_noirq]\n",
				targ->name, item);
		targ->pm->suspend_noirq = targ->fn_suspend_stage2;
	} else if (!strncmp((char *)prop->value, "late", strlen("late"))) {
		pr_info("%s: Mstar-STR, [%s] register to [suspend_late]\n",
				targ->name, item);
		targ->pm->suspend_late = targ->fn_suspend_stage2;
	} else if (!strncmp((char *)prop->value, "normal", strlen("normal"))) {
		pr_info("%s: Mstar-STR, [%s] register to [suspend]\n",
				targ->name, item);
		targ->pm->suspend = targ->fn_suspend_stage2;
	} else {
		pr_info("%s: Mstar-STR, [%s] Unknow [%s], "
			"using default setting\n",
			targ->name, item, (char *)prop->value);
		return -1;
	}

	return 0;
}

static int suspend_default(struct target *targ)
{
	pr_info("%s: Mstar-STR, Suspend order use default setting\n",
				targ->name);
	targ->pm->suspend = targ->fn_suspend;

	return 0;
}

static int parser_resume_order(struct device_node **root,
				struct device_node **leaf,
				struct target *targ,
				const char *item)
{
	struct property *prop;

	prop = of_find_property(*leaf, item, NULL);
	if (!prop) {
		pr_info("%s: Mstar-STR, No [%s] defined in dts\n",
					targ->name, item);
		return -1;
	}

	if (!strncmp((char *)prop->value, "noirq", strlen("noirq"))) {
		pr_info("%s: Mstar-STR, [%s] register to [resume_noirq]\n",
					targ->name, item);
		targ->pm->resume_noirq = targ->fn_resume;
	} else if (!strncmp((char *)prop->value, "early", strlen("early"))) {
		pr_info("%s: Mstar-STR, [%s] register to [resume_early]\n",
					targ->name, item);
		targ->pm->resume_early = targ->fn_resume;
	} else if (!strncmp((char *)prop->value, "normal", strlen("normal"))) {
		pr_info("%s: Mstar-STR, [%s] register to resume\n",
					targ->name, item);
		targ->pm->resume = targ->fn_resume;
	} else {
		pr_info("%s: Mstar-STR, [%s] Unknow [%s], "
			"using default setting\n",
			targ->name, item, (char *)prop->value);
		return -1;
	}

	return 0;
}

static int parser_resume_order_stage2(struct device_node **root,
					struct device_node **leaf,
					struct target *targ,
					const char *item)
{
	struct property *prop;

	prop = of_find_property(*leaf, item, NULL);
	if (!prop) {
		pr_info("%s: Mstar-STR, No [%s] defined in dts\n",
				targ->name, item);
		return -1;
	}

	if (!targ->fn_resume_stage2) {
		pr_err("%s: Mstar-STR, [%s] callback function is not defined!"
			" please review your DTS\n", targ->name, item);
		return -1;
	}

	if (!strncmp((char *)prop->value, "noirq", strlen("noirq"))) {
		pr_info("%s: Mstar-STR, [%s] register to [resume_noirq]\n",
						targ->name, item);
		targ->pm->resume_noirq = targ->fn_resume_stage2;
	} else if (!strncmp((char *)prop->value, "early", strlen("early"))) {
		pr_info("%s: Mstar-STR, [%s] register to [resume_early]\n",
						targ->name, item);
		targ->pm->resume_early = targ->fn_resume_stage2;
	} else if (!strncmp((char *)prop->value, "normal", strlen("normal"))) {
		pr_info("%s: Mstar-STR, [%s] register to [resume]\n",
						targ->name, item);
		targ->pm->resume = targ->fn_resume_stage2;
	} else {
		pr_info("%s: Mstar-MSTR [%s] Unknow [%s],"
			"using default setting\n",
			targ->name, item, (char *)prop->value);
		return -1;
	}

	return 0;
}

static int resume_default(struct target *targ)
{
	pr_info("%s: Mstar-STR, Resume order use default setting\n",
						targ->name);
	targ->pm->resume = targ->fn_resume;

	return 0;
}

static int parser_policy(struct device_node **root,
			struct device_node **leaf,
			struct target *targ,
			const char *item)
{
	struct property *prop;

	prop = of_find_property(*leaf, item, NULL);
	if (!prop) {
		pr_info("%s: Mstar-STR, No [%s] defined in dts\n",
				targ->name, item);
		return -1;
	}

	if (!targ->dev) {
		pr_err("%s: Mstar-STR, the module's device structure is not set"
			"while calling of_mstar_str, "
			"Please check again!\n", targ->name);
		return -1;
	}

	if (!strncmp((char *)prop->value, "async", strlen("async"))) {
		pr_info("%s: Mstar-STR, [%s] register STR policy to async\n",
					targ->name, item);
		device_enable_async_suspend(targ->dev);
	} else if (!strncmp((char *)prop->value, "sync", strlen("sync"))) {
		pr_info("%s: Mstar-STR, [%s] register STR policy to sync\n",
					targ->name, item);
		device_disable_async_suspend(targ->dev);
	} else {
		pr_err("%s: Mstar-STR, [%s] Unknow [%s],"
			"using default setting\n",
			targ->name, item, (char *)prop->value);
		return -1;
	}

	return 0;
}

static int parser_waitfor(struct device_node **root,
				struct device_node **leaf,
				struct target *targ,
				const char *item)
{
	struct property *prop;
	extern struct device* dpm_get_dev(const char* name);
	struct device **dev;

	prop = of_find_property(*leaf, item, NULL);
	if (!prop) {
		pr_info("%s: Mstar-STR, No [%s] defined in dts\n",
				targ->name, item);
		return -1;
	}

	if (!strncmp(item, MSTAR_STR_SUSPEND_STG1_WF, strlen(item)))
		dev = &(targ->waitfor_dev->stage1_s_wait);
	else if (!strncmp(item, MSTAR_STR_RESUME_STG1_WF, strlen(item)))
		dev = &(targ->waitfor_dev->stage1_r_wait);
	else if (!strncmp(item, MSTAR_STR_SUSPEND_STG2_WF, strlen(item)))
		dev = &(targ->waitfor_dev->stage2_s_wait);
	else if (!strncmp(item, MSTAR_STR_RESUME_STG2_WF, strlen(item)))
		dev = &(targ->waitfor_dev->stage2_r_wait);
	else
		return -1; // should not go here

	*dev = dpm_get_dev((const char *)prop->value);
	if (!(*dev)) {
		pr_err("%s: Mstar-STR, [%s] cannot find %s\n",
			targ->name, item, (char *)prop->value);
		return -1;
	}

	pr_info("%s: Mstar-STR, Set [%s] %s\n",
			targ->name, item, (char *)prop->value);

	return 0;
}

#ifdef CONFIG_CC_IS_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
#endif
int of_mstar_str(const char *mod_name, struct device *dev,
			const struct dev_pm_ops *pm,
			struct str_waitfor_dev *waitfor,
			int (*suspend_fn)(struct device *dev),
			int (*resume_fn)(struct device *dev),
			int (*suspend_fn2)(struct device *dev),
			int (*resume_fn2)(struct device *dev))
{
	int i;
	struct device_node *root;
	struct device_node *leaf;
	OF_MSTAR_OPS_DECLARE(of_mst, mod_name);
	struct target targ = {
		.name = mod_name,
		.dev = dev,
		.pm = pm,
		.waitfor_dev = waitfor,
		.fn_suspend = suspend_fn,
		.fn_resume = resume_fn,
		.fn_suspend_stage2 = suspend_fn2,
		.fn_resume_stage2 = resume_fn2,
	};

	for (i = 0; i < OF_MSTAR_STR_MAX; ++i) {
		if (of_mst[i].parser(&root, &leaf, &targ,
				of_mst[i].node_name) == -1) {
			// Use default setting
			if (!of_mst[i].defaults)
				continue;
			// if defaults == -1, set global default setting
			if (of_mst[i].defaults(&targ) == -1)
				break;
		}
	}

	return 0;
}
#ifdef CONFIG_CC_IS_CLANG
#pragma clang diagnostic pop
#endif
EXPORT_SYMBOL(of_mstar_str);

