/*
 * drivers/staging/android/ion/compat_ion.c
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "ion.h"
#include "compat_ion.h"

/* See drivers/staging/android/uapi/ion.h for the definition of these structs */
struct compat_ion_allocation_data {
	compat_size_t len;
	compat_size_t align;
	compat_uint_t heap_id_mask;
	compat_uint_t flags;
	compat_int_t handle;
};

struct compat_ion_custom_data {
	compat_uint_t cmd;
	compat_ulong_t arg;
};

struct compat_ion_handle_data {
	compat_int_t handle;
};

#if (MP_ION_PATCH_MSTAR==1)
struct compat_ion_user_data {
    compat_int_t handle;
    compat_ulong_t bus_addr;
};

struct compat_ion_cust_allocation_data {
    compat_size_t start;
    compat_size_t len;
    compat_size_t align;
    compat_uint_t heap_id_mask;
    compat_uint_t flags;
    compat_int_t handle;
    compat_ulong_t miu_offset;
    unsigned char miu;
};
#endif

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
struct compat_ion_cache_flush_data {
	compat_size_t start;
	compat_size_t len;
};
#endif

#define COMPAT_ION_IOC_ALLOC	_IOWR(ION_IOC_MAGIC, 0, \
				      struct compat_ion_allocation_data)
#define COMPAT_ION_IOC_FREE	_IOWR(ION_IOC_MAGIC, 1, \
				      struct compat_ion_handle_data)
#define COMPAT_ION_IOC_CUSTOM	_IOWR(ION_IOC_MAGIC, 6, \
				      struct compat_ion_custom_data)
#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
#define COMPAT_ION_IOC_CACHE_FLUSH      _IOW(ION_IOC_MAGIC, 10, \
						struct compat_ion_cache_flush_data)
#endif
#if (MP_ION_PATCH_MSTAR==1)
#define COMPAT_ION_IOC_CUST_ALLOC       _IOWR(ION_IOC_MAGIC, 9, \
                      struct compat_ion_cust_allocation_data)
#define COMPAT_ION_IOC_GET_CMA_BUFFER_INFO      _IOWR(ION_IOC_MAGIC, 11,\
                      struct compat_ion_user_data)
#endif


static int compat_get_ion_allocation_data(
			struct compat_ion_allocation_data __user *data32,
			struct ion_allocation_data __user *data)
{
	compat_size_t s;
	compat_uint_t u;
	compat_int_t i;
	int err;

	err = get_user(s, &data32->len);
	err |= put_user(s, &data->len);
	err |= get_user(s, &data32->align);
	err |= put_user(s, &data->align);
	err |= get_user(u, &data32->heap_id_mask);
	err |= put_user(u, &data->heap_id_mask);
	err |= get_user(u, &data32->flags);
	err |= put_user(u, &data->flags);
	err |= get_user(i, &data32->handle);
	err |= put_user(i, &data->handle);

	return err;
}

static int compat_get_ion_handle_data(
			struct compat_ion_handle_data __user *data32,
			struct ion_handle_data __user *data)
{
	compat_int_t i;
	int err;

	err = get_user(i, &data32->handle);
	err |= put_user(i, &data->handle);

	return err;
}

#if (MP_ION_PATCH_MSTAR==1)
static int compat_get_ion_user_data(
			struct compat_ion_user_data __user *data32,
			struct ion_user_data __user *data)
{
	compat_ulong_t ul;
	compat_int_t i;
	int err;

	err = get_user(i,&data32->handle);
	err |= put_user(i,&data->handle);
	err |= get_user(ul,&data32->bus_addr);
	err |= put_user(ul,&data->bus_addr);
	return err;
}

static int compat_put_ion_user_data(
			struct compat_ion_user_data __user *data32,
			struct ion_user_data __user *data)
{
	compat_ulong_t ul;
	compat_int_t i;
	int err;

	err = get_user(i,&data->handle);
	err |= put_user(i,&data32->handle);
	err |= get_user(ul,&data->bus_addr);
	err |= put_user(ul,&data32->bus_addr);
	return err;
}

static int compat_get_ion_cust_allocation_data(
			struct compat_ion_cust_allocation_data __user *data32,
			struct ion_cust_allocation_data __user *data)
{
    compat_ulong_t ul;
    compat_size_t s;
    compat_uint_t ui;
    compat_int_t i;
    unsigned char uc;
    int err;

    err = get_user(s,&data32->start);
    err |= put_user(s,&data->start);
    err |= get_user(s,&data32->len);
    err |= put_user(s,&data->len);
    err |= get_user(s,&data32->align);
    err |= put_user(s,&data->align);
    err |= get_user(ui,&data32->heap_id_mask);
    err |= put_user(ui,&data->heap_id_mask);
    err |= get_user(ui,&data32->flags);
    err |= put_user(ui,&data->flags);
    err |= get_user(i,&data32->handle);
    err |= put_user(i,&data->handle);
    err |= get_user(ul,&data32->miu_offset);
    err |= put_user(ul,&data->miu_offset);
    err |= get_user(uc,&data32->miu);
    err |= put_user(uc,&data->miu);
    return err;
}

static int compat_put_ion_cust_allocation_data(
			struct compat_ion_cust_allocation_data __user *data32,
			struct ion_cust_allocation_data __user *data)
{
    compat_ulong_t ul;
    compat_size_t s;
    compat_uint_t ui;
    compat_int_t i;
    unsigned char uc;
    int err;

    err = get_user(s,&data->start);
    err |= put_user(s,&data32->start);
    err |= get_user(s,&data->len);
    err |= put_user(s,&data32->len);
    err |= get_user(s,&data->align);
    err |= put_user(s,&data32->align);
    err |= get_user(ui,&data->heap_id_mask);
    err |= put_user(ui,&data32->heap_id_mask);
    err |= get_user(ui,&data->flags);
    err |= put_user(ui,&data32->flags);
    err |= get_user(i,&data->handle);
    err |= put_user(i,&data32->handle);
    err |= get_user(ul,&data->miu_offset);
    err |= put_user(ul,&data32->miu_offset);
    err |= get_user(uc,&data->miu);
    err |= put_user(uc,&data32->miu);
    return err;
}
#endif

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
static int compat_get_ion_flush_cache_data(
	struct compat_ion_cache_flush_data __user *data32,
	struct ion_cache_flush_data __user *data)
{
	int err;
	compat_size_t s;

	err = get_user(s,&data32->start);
	err |= put_user(s,&data->start);
	err |= get_user(s,&data32->len);
	err |= put_user(s,&data->len);

	return err;
}
#endif

static int compat_put_ion_allocation_data(
			struct compat_ion_allocation_data __user *data32,
			struct ion_allocation_data __user *data)
{
	compat_size_t s;
	compat_uint_t u;
	compat_int_t i;
	int err;

	err = get_user(s, &data->len);
	err |= put_user(s, &data32->len);
	err |= get_user(s, &data->align);
	err |= put_user(s, &data32->align);
	err |= get_user(u, &data->heap_id_mask);
	err |= put_user(u, &data32->heap_id_mask);
	err |= get_user(u, &data->flags);
	err |= put_user(u, &data32->flags);
	err |= get_user(i, &data->handle);
	err |= put_user(i, &data32->handle);

	return err;
}

static int compat_get_ion_custom_data(
			struct compat_ion_custom_data __user *data32,
			struct ion_custom_data __user *data)
{
	compat_uint_t cmd;
	compat_ulong_t arg;
	int err;

	err = get_user(cmd, &data32->cmd);
	err |= put_user(cmd, &data->cmd);
	err |= get_user(arg, &data32->arg);
	err |= put_user(arg, &data->arg);

	return err;
};

long compat_ion_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret;

	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_ION_IOC_ALLOC:
	{
		struct compat_ion_allocation_data __user *data32;
		struct ion_allocation_data __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (!data)
			return -EFAULT;

		err = compat_get_ion_allocation_data(data32, data);
		if (err)
			return err;
		ret = filp->f_op->unlocked_ioctl(filp, ION_IOC_ALLOC,
							(unsigned long)data);
		err = compat_put_ion_allocation_data(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_ION_IOC_FREE:
	{
		struct compat_ion_handle_data __user *data32;
		struct ion_handle_data __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (!data)
			return -EFAULT;

		err = compat_get_ion_handle_data(data32, data);
		if (err)
			return err;

		return filp->f_op->unlocked_ioctl(filp, ION_IOC_FREE,
							(unsigned long)data);
	}
	case COMPAT_ION_IOC_CUSTOM: {
		struct compat_ion_custom_data __user *data32;
		struct ion_custom_data __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (!data)
			return -EFAULT;

		err = compat_get_ion_custom_data(data32, data);
		if (err)
			return err;

		return filp->f_op->unlocked_ioctl(filp, ION_IOC_CUSTOM,
							(unsigned long)data);
	}
	case ION_IOC_SHARE:
	case ION_IOC_MAP:
	case ION_IOC_IMPORT:
	case ION_IOC_SYNC:
		return filp->f_op->unlocked_ioctl(filp, cmd,
						(unsigned long)compat_ptr(arg));
#if (MP_ION_PATCH_MSTAR==1)
	case COMPAT_ION_IOC_GET_CMA_BUFFER_INFO:
	{
		struct compat_ion_user_data __user *data32;
		struct ion_user_data __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (data == NULL)
			return -EFAULT;

		err = compat_get_ion_user_data(data32, data);
		if (err)
			return err;
		ret = filp->f_op->unlocked_ioctl(filp, ION_IOC_GET_CMA_BUFFER_INFO,
							(unsigned long)data);
		err = compat_put_ion_user_data(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_ION_IOC_CUST_ALLOC:
	{
		struct compat_ion_cust_allocation_data __user *data32;
		struct ion_cust_allocation_data __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (data == NULL)
			return -EFAULT;

		err = compat_get_ion_cust_allocation_data(data32, data);
		if (err)
			return err;
		ret = filp->f_op->unlocked_ioctl(filp, ION_IOC_CUST_ALLOC,
							(unsigned long)data);
		err = compat_put_ion_cust_allocation_data(data32, data);
		return ret ? ret : err;
	}
#endif
#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
	case COMPAT_ION_IOC_CACHE_FLUSH:
	{
		struct compat_ion_cache_flush_data __user *data32;
		struct ion_cache_flush_data __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (data == NULL)
			return -EFAULT;

		err = compat_get_ion_flush_cache_data(data32, data);
		if(err)
			return err;
		return filp->f_op->unlocked_ioctl(filp, ION_IOC_CACHE_FLUSH,
			(unsigned long)data);
	}
#endif
#if (MP_ION_PATCH_MSTAR==1)
	default:
		return -ENOIOCTLCMD;
	}
#endif
}
