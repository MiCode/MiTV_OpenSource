/*
 * smaf-optee.c
 *
 * Copyright (C) Linaro SA 2015
 * Author: Benjamin Gaignard <benjamin.gaignard@linaro.org> for Linaro.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "smaf-secure.h"

/* TODO: cleanup include directories */
#include <linux/tee_kernel_api.h>
#include <linux/tee_client_api.h>

/* Those define are copied from ta_sdp.h */
#define TA_SDP_UUID { 0xb9aa5f00, 0xd229, 0x11e4, \
		{ 0x92, 0x5c, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b} }

#define TA_SDP_CREATE_REGION    0
#define TA_SDP_DESTROY_REGION   1
#define TA_SDP_UPDATE_REGION    2
#define TA_SDP_DUMP_STATUS	3

struct smaf_optee_device {
	struct list_head clients_head;
	/* mutex to serialize list manipulation */
	struct mutex lock;
	struct dentry *debug_root;
	TEEC_Context ctx;
	TEEC_Session session;
	bool session_initialized;
};

struct sdp_client {
	struct list_head client_node;
	struct list_head regions_head;
	struct mutex lock;
	const char *name;
};

struct sdp_region {
	struct list_head region_node;
	dma_addr_t addr;
	size_t size;
	int id;
};

static struct smaf_optee_device so_dev;

/* trusted application call */

/**
 * sdp_ta_create_region -create a region with a given address and size
 *
 * in case of success return a region id (>=0) else -EINVAL
 */
static int sdp_ta_region_create(dma_addr_t addr, size_t size)
{
	TEEC_Operation op;
	TEEC_Result res;
	uint32_t err_origin;

	memset(&op, 0, sizeof(op));

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_VALUE_INPUT,
					 TEEC_VALUE_OUTPUT, TEEC_NONE);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#error "not implemented"
#else
	op.params[0].value.a = 0x0;
	op.params[0].value.b = addr;
#endif
	op.params[1].value.a = size;

	res = TEEC_InvokeCommand(&so_dev.session, TA_SDP_CREATE_REGION,
				 &op, &err_origin);
	if (res != TEEC_SUCCESS) {
		printk(KERN_ERR "failed to create region 0x%x 0x%x\n",
		       res, err_origin);
		return -EINVAL;
	}

	return op.params[2].value.a;
}

static int sdp_ta_region_destroy(struct sdp_region *region)
{
	TEEC_Operation op;
	TEEC_Result res;
	uint32_t err_origin;

	memset(&op, 0, sizeof(op));

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
					 TEEC_NONE, TEEC_NONE);

	op.params[0].value.a = region->id;

	res = TEEC_InvokeCommand(&so_dev.session, TA_SDP_DESTROY_REGION,
				 &op, &err_origin);
	if (res != TEEC_SUCCESS) {
		printk(KERN_ERR "failed to destroy region 0x%x 0x%x\n",
		       res, err_origin);
		return -EINVAL;
	}

	return 0;
}

static int sdp_ta_region_update(struct sdp_region *region, struct device *dev,
				enum dma_data_direction dir, bool add)
{
	TEEC_Operation op;
	TEEC_Result res;
	uint32_t err_origin;

	memset(&op, 0, sizeof(op));

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT,
					 TEEC_MEMREF_TEMP_INPUT,
					 TEEC_VALUE_INPUT,
					 TEEC_NONE);

	op.params[0].value.a = region->id;
	op.params[0].value.b = add;

	op.params[1].tmpref.buffer = (void *)dev->driver->name;
	op.params[1].tmpref.size = strlen(dev->driver->name) + 1;

	op.params[2].value.a = dir;

	res = TEEC_InvokeCommand(&so_dev.session, TA_SDP_UPDATE_REGION,
				 &op, &err_origin);
	if (res != TEEC_SUCCESS) {
		printk(KERN_ERR "failed to update region 0x%x 0x%x\n",
		       res, err_origin);
		return -EINVAL;
	}

	return 0;
}

static int sdp_init_session(void)
{
	TEEC_Result res;
	uint32_t err_origin;
	TEEC_UUID uuid = TA_SDP_UUID;

	if (so_dev.session_initialized)
		return 0;

	res = TEEC_InitializeContext(NULL, &so_dev.ctx);
	if (res != TEEC_SUCCESS) {
		printk (KERN_ERR "TEEC_InitializeContext failed %d\n", res);
		return -EINVAL;
	}

	res = TEEC_OpenSession(&so_dev.ctx, &so_dev.session, &uuid,
			TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);
	if (res != TEEC_SUCCESS) {
		printk(KERN_ERR "TEEC_OpenSession failed %d\n", res);
		TEEC_FinalizeContext(&so_dev.ctx);
		return -EINVAL;
	}

	so_dev.session_initialized = true;
	return 0;
}

static void sdp_destroy_session(void)
{
	if (!so_dev.session_initialized)
		return;

	TEEC_CloseSession(&so_dev.session);
	TEEC_FinalizeContext(&so_dev.ctx);
	so_dev.session_initialized = false;
}

/* internal functions */
static int sdp_region_add(struct sdp_region *region, struct device *dev,
			  enum dma_data_direction dir)
{
	return sdp_ta_region_update(region, dev, dir, true);
}

static int sdp_region_remove(struct sdp_region *region, struct device *dev,
			     enum dma_data_direction dir)
{
	return sdp_ta_region_update(region, dev, dir, false);
}

static struct sdp_region *sdp_region_create(struct sdp_client *client,
					    dma_addr_t addr, size_t size)
{
	struct sdp_region *region;
	int region_id;

	/* here call TA to create the region */
	if (sdp_init_session())
		return NULL;

	region_id = sdp_ta_region_create(addr, size);
	if (region_id < 0)
		return NULL;

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	INIT_LIST_HEAD(&region->region_node);
	region->addr = addr;
	region->size = size;
	region->id = region_id;

	mutex_lock(&client->lock);
	list_add(&region->region_node, &client->regions_head);
	mutex_unlock(&client->lock);

	return region;
}

static int sdp_region_destroy(struct sdp_client *client,
			      struct sdp_region *region)
{
	if (sdp_ta_region_destroy(region))
		return -EINVAL;

	mutex_lock(&client->lock);
	list_del(&region->region_node);
	mutex_unlock(&client->lock);

	kfree(region);
	return 0;
}

static struct sdp_region *sdp_region_find(struct sdp_client *client,
					  dma_addr_t addr, size_t size)
{
	struct sdp_region *region;

	mutex_lock(&client->lock);

	list_for_each_entry(region, &client->regions_head, region_node) {
		if (region->addr == addr && region->size == size) {
			mutex_unlock(&client->lock);
			return region;
		}
	}

	mutex_unlock(&client->lock);
	return NULL;
}

static int sdp_grant_access(struct sdp_client *client, struct device *dev,
		     dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	struct sdp_region *region;

	region = sdp_region_find(client, addr, size);

	if (!region)
		region = sdp_region_create(client, addr, size);

	if (!region)
		return -EINVAL;

	return sdp_region_add(region, dev, dir);

}

static int sdp_revoke_access(struct sdp_client *client, struct device *dev,
		     dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	struct sdp_region *region;

	region = sdp_region_find(client, addr, size);

	if (!region)
		return -EINVAL;

	return sdp_region_remove(region, dev, dir);

}

static void *smaf_optee_create_context(void)
{
	struct sdp_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	mutex_init(&client->lock);
	INIT_LIST_HEAD(&client->client_node);
	INIT_LIST_HEAD(&client->regions_head);

	client->name = kstrdup("smaf-optee", GFP_KERNEL);

	mutex_lock(&so_dev.lock);
	list_add(&client->client_node, &so_dev.clients_head);
	mutex_unlock(&so_dev.lock);

	return client;

}

static int smaf_optee_destroy_context(void *ctx)
{
	struct sdp_client *client = ctx;
	struct sdp_region *region, *tmp;

	if (!client)
		return -EINVAL;

	list_for_each_entry_safe(region, tmp, &client->regions_head, region_node) {
		sdp_region_destroy(client, region);
	}

	mutex_lock(&so_dev.lock);
	list_del(&client->client_node);
	mutex_unlock(&so_dev.lock);

	kfree(client->name);
	kfree(client);
	return 0;

}

static bool smaf_optee_grant_access(void *ctx,
				    struct device *dev,
				    size_t addr, size_t size,
				    enum dma_data_direction direction)
{
	struct sdp_client *client = ctx;

	return !sdp_grant_access(client, dev, addr, size, direction);
}

static void smaf_optee_revoke_access(void *ctx,
				     struct device *dev,
				     size_t addr, size_t size,
				     enum dma_data_direction direction)
{
	struct sdp_client *client = ctx;
	sdp_revoke_access(client, dev, addr, size, direction);
}

static bool smaf_optee_allow_cpu_access(void *ctx, enum dma_data_direction direction)
{
	return direction == DMA_TO_DEVICE;
}

static struct smaf_secure smaf_optee_sec = {
	.create_ctx = smaf_optee_create_context,
	.destroy_ctx = smaf_optee_destroy_context,
	.grant_access = smaf_optee_grant_access,
	.revoke_access = smaf_optee_revoke_access,
	.allow_cpu_access = smaf_optee_allow_cpu_access,
};

/* debugfs helpers */
#define MAX_DUMP_SIZE 2048
static int smaf_optee_ta_dump_status(struct seq_file *s, void *unused)
{
	TEEC_Operation op;
	TEEC_Result res;
	uint32_t err_origin;
	char *dump;

	if (sdp_init_session())
		return 0;

	memset(&op, 0, sizeof(op));

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);

	dump = kzalloc(MAX_DUMP_SIZE, GFP_KERNEL);
	op.params[0].tmpref.buffer = (void *)dump;
	op.params[0].tmpref.size = MAX_DUMP_SIZE - 1;

	res = TEEC_InvokeCommand(&so_dev.session, TA_SDP_DUMP_STATUS,
				 &op, &err_origin);
	if (res != TEEC_SUCCESS) {
		printk(KERN_ERR "failed to dump status 0x%x 0x%x\n",
		       res, err_origin);
		kfree(dump);
		return -EINVAL;
	}

	seq_printf(s, "%s", dump);

	kfree(dump);
	return 0;
}

static int smaf_optee_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, smaf_optee_ta_dump_status, inode->i_private);
}

static const struct file_operations so_debug_fops = {
	.open    = smaf_optee_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int __init smaf_optee_init(void)
{
	mutex_init(&so_dev.lock);
	INIT_LIST_HEAD(&so_dev.clients_head);

	so_dev.debug_root = debugfs_create_dir("smaf-optee", NULL);
	debugfs_create_file("dump", S_IRUGO, so_dev.debug_root,
			    &so_dev, &so_debug_fops);

	so_dev.session_initialized = false;

	smaf_register_secure(&smaf_optee_sec);

	return 0;
}
module_init(smaf_optee_init);

static void __exit smaf_optee_exit(void)
{
	smaf_unregister_secure(&smaf_optee_sec);
	sdp_destroy_session();
}
module_exit(smaf_optee_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SMAF for OP-TEE");
MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@linaro.org>");
