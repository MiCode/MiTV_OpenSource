/*
 *  internal.h - declarations internal to debugfs
 *
 *  Copyright (C) 2016 Nicolai Stange <nicstange@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#ifndef _DEBUGFS_INTERNAL_H_
#define _DEBUGFS_INTERNAL_H_

struct file_operations;

/* declared over in file.c */
extern const struct file_operations debugfs_noop_file_operations;
extern const struct file_operations debugfs_open_proxy_file_operations;
extern const struct file_operations debugfs_full_proxy_file_operations;

typedef struct refcount_struct {
                atomic_t refs;
} refcount_t;

struct debugfs_fsdata {
	const struct file_operations *real_fops;
	refcount_t active_users;
	struct completion active_users_drained;
	struct rcu_head rcu_head;
};

static inline void refcount_set(refcount_t *r, unsigned int n)
{
                atomic_set(&r->refs, n);
}

static inline __must_check bool refcount_dec_and_test(refcount_t *r)
{
                return atomic_dec_and_test(&r->refs);
}

static inline __must_check bool refcount_inc_not_zero(refcount_t *r)
{
                return atomic_add_unless(&r->refs, 1, 0);
}

/*
 * A dentry's ->d_fsdata either points to the real fops or to a
 * dynamically allocated debugfs_fsdata instance.
 * In order to distinguish between these two cases, a real fops
 * pointer gets its lowest bit set.
 */
#define DEBUGFS_FSDATA_IS_REAL_FOPS_BIT BIT(0)

#endif /* _DEBUGFS_INTERNAL_H_ */
