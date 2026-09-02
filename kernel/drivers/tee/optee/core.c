/*
 * Copyright (c) 2015, Linaro Limited
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include "optee_private.h"
#include "optee_smc.h"
#include "shm_pool.h"

#define DRIVER_NAME "optee"

#define OPTEE_SHM_NUM_PRIV_PAGES	CONFIG_OPTEE_SHM_NUM_PRIV_PAGES

#ifdef CONFIG_MSTAR_CHIP
#include <linux/freezer.h>
#include <linux/delay.h>
int optee_version = 0;
EXPORT_SYMBOL(optee_version);
#define TEE_SUP_NAME "tee-supplicant"

atomic_t tee_sup_used = ATOMIC_INIT(0);

atomic_t STR = ATOMIC_INIT(0);
EXPORT_SYMBOL(STR);
atomic_t SMC_UNLOCKED = ATOMIC_INIT(1);

int _tee_get_supplicant_count(void)
{
	return (int)atomic_read(&tee_sup_used);
}
EXPORT_SYMBOL(_tee_get_supplicant_count);
#endif

/**
 * optee_from_msg_param() - convert from OPTEE_MSG parameters to
 *			    struct tee_param
 * @params:	subsystem internal parameter representation
 * @num_params:	number of elements in the parameter arrays
 * @msg_params:	OPTEE_MSG parameters
 * Returns 0 on success or <0 on failure
 */
int optee_from_msg_param(struct tee_param *params, size_t num_params,
			 const struct optee_msg_param *msg_params)
{
	int rc;
	size_t n;
	struct tee_shm *shm;
	phys_addr_t pa;

	for (n = 0; n < num_params; n++) {
		struct tee_param *p = params + n;
		const struct optee_msg_param *mp = msg_params + n;
		u32 attr = mp->attr & OPTEE_MSG_ATTR_TYPE_MASK;

		switch (attr) {
		case OPTEE_MSG_ATTR_TYPE_NONE:
			p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
			memset(&p->u, 0, sizeof(p->u));
			break;
		case OPTEE_MSG_ATTR_TYPE_VALUE_INPUT:
		case OPTEE_MSG_ATTR_TYPE_VALUE_OUTPUT:
		case OPTEE_MSG_ATTR_TYPE_VALUE_INOUT:
			p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT +
				  attr - OPTEE_MSG_ATTR_TYPE_VALUE_INPUT;
			p->u.value.a = mp->u.value.a;
			p->u.value.b = mp->u.value.b;
			p->u.value.c = mp->u.value.c;
			break;
		case OPTEE_MSG_ATTR_TYPE_TMEM_INPUT:
		case OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT:
		case OPTEE_MSG_ATTR_TYPE_TMEM_INOUT:
			p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT +
				  attr - OPTEE_MSG_ATTR_TYPE_TMEM_INPUT;
			p->u.memref.size = mp->u.tmem.size;
			shm = (struct tee_shm *)(unsigned long)
				mp->u.tmem.shm_ref;
			if (!shm) {
				p->u.memref.shm_offs = 0;
				p->u.memref.shm = NULL;
				break;
			}
			rc = tee_shm_get_pa(shm, 0, &pa);
			if (rc)
				return rc;
			p->u.memref.shm_offs = mp->u.tmem.buf_ptr - pa;
			p->u.memref.shm = shm;

			/* Check that the memref is covered by the shm object */
			if (p->u.memref.size) {
				size_t o = p->u.memref.shm_offs +
					   p->u.memref.size - 1;

				rc = tee_shm_get_pa(shm, o, NULL);
				if (rc)
					return rc;
			}
			break;
		case OPTEE_MSG_ATTR_TYPE_RMEM_INPUT:
		case OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT:
		case OPTEE_MSG_ATTR_TYPE_RMEM_INOUT:
			p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT +
				  attr - OPTEE_MSG_ATTR_TYPE_RMEM_INPUT;
			p->u.memref.size = mp->u.rmem.size;
			shm = (struct tee_shm *)(unsigned long)
				mp->u.rmem.shm_ref;

			if (!shm) {
				p->u.memref.shm_offs = 0;
				p->u.memref.shm = NULL;
				break;
			}
			p->u.memref.shm_offs = mp->u.rmem.offs;
			p->u.memref.shm = shm;

			break;

		default:
			return -EINVAL;
		}
	}
	return 0;
}

static int to_msg_param_tmp_mem(struct optee_msg_param *mp,
				const struct tee_param *p)
{
	int rc;
	phys_addr_t pa;

	mp->attr = OPTEE_MSG_ATTR_TYPE_TMEM_INPUT + p->attr -
		   TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;

	mp->u.tmem.shm_ref = (unsigned long)p->u.memref.shm;
	mp->u.tmem.size = p->u.memref.size;

	if (!p->u.memref.shm) {
		mp->u.tmem.buf_ptr = 0;
		return 0;
	}

	rc = tee_shm_get_pa(p->u.memref.shm, p->u.memref.shm_offs, &pa);
	if (rc)
		return rc;

	mp->u.tmem.buf_ptr = pa;
	mp->attr |= OPTEE_MSG_ATTR_CACHE_PREDEFINED <<
		    OPTEE_MSG_ATTR_CACHE_SHIFT;

	return 0;
}

static int to_msg_param_reg_mem(struct optee_msg_param *mp,
				const struct tee_param *p)
{
	mp->attr = OPTEE_MSG_ATTR_TYPE_RMEM_INPUT + p->attr -
		   TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;

	mp->u.rmem.shm_ref = (unsigned long)p->u.memref.shm;
	mp->u.rmem.size = p->u.memref.size;
	mp->u.rmem.offs = p->u.memref.shm_offs;
	return 0;
}

/**
 * optee_to_msg_param() - convert from struct tee_params to OPTEE_MSG parameters
 * @msg_params:	OPTEE_MSG parameters
 * @num_params:	number of elements in the parameter arrays
 * @params:	subsystem itnernal parameter representation
 * Returns 0 on success or <0 on failure
 */
int optee_to_msg_param(struct optee_msg_param *msg_params, size_t num_params,
		       const struct tee_param *params)
{
	int rc;
	size_t n;

	for (n = 0; n < num_params; n++) {
		const struct tee_param *p = params + n;
		struct optee_msg_param *mp = msg_params + n;

		switch (p->attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_NONE:
			mp->attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
			memset(&mp->u, 0, sizeof(mp->u));
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			mp->attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT + p->attr -
				   TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
			mp->u.value.a = p->u.value.a;
			mp->u.value.b = p->u.value.b;
			mp->u.value.c = p->u.value.c;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			if (tee_shm_is_registered(p->u.memref.shm))
				rc = to_msg_param_reg_mem(mp, p);
			else
				rc = to_msg_param_tmp_mem(mp, p);
			if (rc)
				return rc;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static void optee_get_version(struct tee_device *teedev,
			      struct tee_ioctl_version_data *vers)
{
	struct tee_ioctl_version_data v = {
		.impl_id = TEE_IMPL_ID_OPTEE,
		.impl_caps = TEE_OPTEE_CAP_TZ,
		.gen_caps = TEE_GEN_CAP_GP,
	};
	struct optee *optee = tee_get_drvdata(teedev);

	if (optee->sec_caps & OPTEE_SMC_SEC_CAP_DYNAMIC_SHM)
		v.gen_caps |= TEE_GEN_CAP_REG_MEM;
	*vers = v;
}

static int optee_open(struct tee_context *ctx)
{
	struct optee_context_data *ctxdata;
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);

	ctxdata = kzalloc(sizeof(*ctxdata), GFP_KERNEL);
	if (!ctxdata)
		return -ENOMEM;

	if (teedev == optee->supp_teedev) {
		bool busy = true;

		mutex_lock(&optee->supp.mutex);
		if (!optee->supp.ctx) {
			busy = false;
			optee->supp.ctx = ctx;
		}
		mutex_unlock(&optee->supp.mutex);
		if (busy) {
			kfree(ctxdata);
			return -EBUSY;
		}
	}

	mutex_init(&ctxdata->mutex);
	INIT_LIST_HEAD(&ctxdata->sess_list);
#ifdef CONFIG_MSTAR_CHIP
	/*
	 * XXX: Add for tee-supplicant alive
	*/
	if (!strncmp(TEE_SUP_NAME, current->comm, strlen(TEE_SUP_NAME))) {
		int cnt = atomic_read(&tee_sup_used);
		if (cnt == 0) {
			pr_info("tee-supplicant %s \n", __func__);
			atomic_set(&tee_sup_used, 1);
		} else {
			pr_err("%s: [ERROR] should not go here, "
				"only 1 tee-supplicant exist!\n", __func__);
			atomic_set(&tee_sup_used, 1);
		}
	}
#endif

	ctx->data = ctxdata;
	return 0;
}

static void optee_release(struct tee_context *ctx)
{
	struct optee_context_data *ctxdata = ctx->data;
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);
	struct tee_shm *shm;
	struct optee_msg_arg *arg = NULL;
	phys_addr_t parg;
	struct optee_session *sess;
	struct optee_session *sess_tmp;

	if (!ctxdata)
		return;

	shm = tee_shm_alloc(ctx, sizeof(struct optee_msg_arg), TEE_SHM_MAPPED);
	if (!IS_ERR(shm)) {
		arg = tee_shm_get_va(shm, 0);
		/*
		 * If va2pa fails for some reason, we can't call into
		 * secure world, only free the memory. Secure OS will leak
		 * sessions and finally refuse more sessions, but we will
		 * at least let normal world reclaim its memory.
		 */
		if (!IS_ERR(arg))
			if (tee_shm_va2pa(shm, arg, &parg))
				arg = NULL; /* prevent usage of parg below */
	}

	list_for_each_entry_safe(sess, sess_tmp, &ctxdata->sess_list,
				 list_node) {
		list_del(&sess->list_node);
		if (!IS_ERR_OR_NULL(arg)) {
			memset(arg, 0, sizeof(*arg));
			arg->cmd = OPTEE_MSG_CMD_CLOSE_SESSION;
			arg->session = sess->session_id;
			optee_do_call_with_arg(ctx, parg);
		}
		kfree(sess);
	}
	kfree(ctxdata);

	if (!IS_ERR(shm))
		tee_shm_free(shm);

	ctx->data = NULL;

	if (teedev == optee->supp_teedev)
		optee_supp_release(&optee->supp);
#ifdef CONFIG_MSTAR_CHIP
	/*
	 * XXX: Add for tee-supplicant alive
	*/
	if (!strncmp(TEE_SUP_NAME, current->comm, strlen(TEE_SUP_NAME))) {
		int cnt = atomic_read(&tee_sup_used);
		if (cnt == 1) {
			pr_info("tee-supplicant %s \n", __func__);
			atomic_set(&tee_sup_used, 0);
		} else if (cnt == 0) {
			pr_err("[ERROR] Should not go here, tee-supplicant "
				"should already call %s!\n", __func__);
		} else {
			pr_err("%s: [ERROR] Should not go here, tee-supplicant "
				"should be 0 or 1!\n", __func__);
			atomic_set(&tee_sup_used, 0);
		}
	}
#endif
}

static const struct tee_driver_ops optee_ops = {
	.get_version = optee_get_version,
	.open = optee_open,
	.release = optee_release,
	.open_session = optee_open_session,
	.close_session = optee_close_session,
	.invoke_func = optee_invoke_func,
	.cancel_req = optee_cancel_req,
	.shm_register = optee_shm_register,
	.shm_unregister = optee_shm_unregister,
};

static const struct tee_desc optee_desc = {
	.name = DRIVER_NAME "-clnt",
	.ops = &optee_ops,
	.owner = THIS_MODULE,
};

static const struct tee_driver_ops optee_supp_ops = {
	.get_version = optee_get_version,
	.open = optee_open,
	.release = optee_release,
	.supp_recv = optee_supp_recv,
	.supp_send = optee_supp_send,
	.shm_register = optee_shm_register_supp,
	.shm_unregister = optee_shm_unregister_supp,
};

static const struct tee_desc optee_supp_desc = {
	.name = DRIVER_NAME "-supp",
	.ops = &optee_supp_ops,
	.owner = THIS_MODULE,
	.flags = TEE_DESC_PRIVILEGED,
};

static bool optee_msg_api_uid_is_optee_api(optee_invoke_fn *invoke_fn)
{
	struct arm_smccc_res res;

	invoke_fn(OPTEE_SMC_CALLS_UID, 0, 0, 0, 0, 0, 0, 0, &res);

	if (res.a0 == OPTEE_MSG_UID_0 && res.a1 == OPTEE_MSG_UID_1 &&
	    res.a2 == OPTEE_MSG_UID_2 && res.a3 == OPTEE_MSG_UID_3)
		return true;
	return false;
}

static void optee_msg_get_os_revision(optee_invoke_fn *invoke_fn)
{
	union {
		struct arm_smccc_res smccc;
		struct optee_smc_call_get_os_revision_result result;
	} res = {
		.result = {
			.build_id = 0
		}
	};

	invoke_fn(OPTEE_SMC_CALL_GET_OS_REVISION, 0, 0, 0, 0, 0, 0, 0,
		  &res.smccc);
#ifdef CONFIG_MSTAR_CHIP
	optee_version = res.result.major;
#endif
	if (res.result.build_id)
		pr_info("revision %lu.%lu (%08lx)", res.result.major,
			res.result.minor, res.result.build_id);
	else
		pr_info("revision %lu.%lu", res.result.major, res.result.minor);
}

static bool optee_msg_api_revision_is_compatible(optee_invoke_fn *invoke_fn)
{
	union {
		struct arm_smccc_res smccc;
		struct optee_smc_calls_revision_result result;
	} res;

	invoke_fn(OPTEE_SMC_CALLS_REVISION, 0, 0, 0, 0, 0, 0, 0, &res.smccc);

	if (res.result.major == OPTEE_MSG_REVISION_MAJOR &&
	    (int)res.result.minor >= OPTEE_MSG_REVISION_MINOR)
		return true;
	return false;
}

static bool optee_msg_exchange_capabilities(optee_invoke_fn *invoke_fn,
					    u32 *sec_caps)
{
	union {
		struct arm_smccc_res smccc;
		struct optee_smc_exchange_capabilities_result result;
	} res;
	u32 a1 = 0;

	/*
	 * TODO This isn't enough to tell if it's UP system (from kernel
	 * point of view) or not, is_smp() returns the the information
	 * needed, but can't be called directly from here.
	 */
	if (!IS_ENABLED(CONFIG_SMP) || nr_cpu_ids == 1)
		a1 |= OPTEE_SMC_NSEC_CAP_UNIPROCESSOR;

	invoke_fn(OPTEE_SMC_EXCHANGE_CAPABILITIES, a1, 0, 0, 0, 0, 0, 0,
		  &res.smccc);

	if (res.result.status != OPTEE_SMC_RETURN_OK)
		return false;

	*sec_caps = res.result.capabilities;
	return true;
}

static struct tee_shm_pool *
optee_config_shm_memremap(optee_invoke_fn *invoke_fn, void **memremaped_shm,
			  u32 sec_caps)
{
	union {
		struct arm_smccc_res smccc;
		struct optee_smc_get_shm_config_result result;
	} res;
	unsigned long vaddr;
	phys_addr_t paddr;
	size_t size;
	phys_addr_t begin;
	phys_addr_t end;
	void *va;
	struct tee_shm_pool_mgr *priv_mgr;
	struct tee_shm_pool_mgr *dmabuf_mgr;
	void *rc;

	invoke_fn(OPTEE_SMC_GET_SHM_CONFIG, 0, 0, 0, 0, 0, 0, 0, &res.smccc);
	if (res.result.status != OPTEE_SMC_RETURN_OK) {
		pr_info("shm service not available\n");
		return ERR_PTR(-ENOENT);
	}

	if (res.result.settings != OPTEE_SMC_SHM_CACHED) {
		pr_err("only normal cached shared memory supported\n");
		return ERR_PTR(-EINVAL);
	}

	begin = roundup(res.result.start, PAGE_SIZE);
	end = rounddown(res.result.start + res.result.size, PAGE_SIZE);
	paddr = begin;
	size = end - begin;

	if (size < 2 * OPTEE_SHM_NUM_PRIV_PAGES * PAGE_SIZE) {
		pr_err("too small shared memory area\n");
		return ERR_PTR(-EINVAL);
	}

	va = memremap(paddr, size, MEMREMAP_WB);
	if (!va) {
		pr_err("shared memory ioremap failed\n");
		return ERR_PTR(-EINVAL);
	}
	vaddr = (unsigned long)va;

	/*
	 * If OP-TEE can work with unregistered SHM, we will use own pool
	 * for private shm
	 */
	if (sec_caps & OPTEE_SMC_SEC_CAP_DYNAMIC_SHM) {
		rc = optee_shm_pool_alloc_pages();
		if (IS_ERR(rc))
			goto err_memunmap;
		priv_mgr = rc;
	} else {
		const size_t sz = OPTEE_SHM_NUM_PRIV_PAGES * PAGE_SIZE;

		rc = tee_shm_pool_mgr_alloc_res_mem(vaddr, paddr, sz,
						    3 /* 8 bytes aligned */);
		if (IS_ERR(rc))
			goto err_memunmap;
		priv_mgr = rc;

		vaddr += sz;
		paddr += sz;
		size -= sz;
	}

	rc = tee_shm_pool_mgr_alloc_res_mem(vaddr, paddr, size, PAGE_SHIFT);
	if (IS_ERR(rc))
		goto err_free_priv_mgr;
	dmabuf_mgr = rc;

	rc = tee_shm_pool_alloc(priv_mgr, dmabuf_mgr);
	if (IS_ERR(rc))
		goto err_free_dmabuf_mgr;

	*memremaped_shm = va;

	return rc;

err_free_dmabuf_mgr:
	tee_shm_pool_mgr_destroy(dmabuf_mgr);
err_free_priv_mgr:
	tee_shm_pool_mgr_destroy(priv_mgr);
err_memunmap:
	memunmap(va);
	return rc;
}

/* Simple wrapper functions to be able to use a function pointer */
static void optee_smccc_smc(unsigned long a0, unsigned long a1,
			    unsigned long a2, unsigned long a3,
			    unsigned long a4, unsigned long a5,
			    unsigned long a6, unsigned long a7,
			    struct arm_smccc_res *res)
{
	arm_smccc_smc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

static void optee_smccc_hvc(unsigned long a0, unsigned long a1,
			    unsigned long a2, unsigned long a3,
			    unsigned long a4, unsigned long a5,
			    unsigned long a6, unsigned long a7,
			    struct arm_smccc_res *res)
{
	arm_smccc_hvc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

static optee_invoke_fn *get_invoke_func(struct device_node *np)
{
	const char *method;

	pr_info("probing for conduit method from DT.\n");

	if (of_property_read_string(np, "method", &method)) {
		pr_warn("missing \"method\" property\n");
		return ERR_PTR(-ENXIO);
	}

	if (!strcmp("hvc", method))
		return optee_smccc_hvc;
	else if (!strcmp("smc", method))
		return optee_smccc_smc;

	pr_warn("invalid \"method\" property: %s\n", method);
	return ERR_PTR(-EINVAL);
}
#ifdef CONFIG_MSTAR_CHIP
#include <linux/kthread.h>
#include <linux/ctype.h>
#include <linux/fs.h>

#define tee_ramlog_write_rpoint(value)	\
	(*(volatile unsigned int *)(tee_ramlog_buf_adr+4) = (value - tee_ramlog_buf_adr))
#define tee_ramlog_read_wpoint	\
	((*(volatile unsigned int *)(tee_ramlog_buf_adr)) + tee_ramlog_buf_adr)
#define tee_ramlog_read_rpoint	\
	((*(volatile unsigned int *)(tee_ramlog_buf_adr+4)) + tee_ramlog_buf_adr)
#define MAX_PRINT_SIZE      256
#define TEESMC32_ST_FASTCALL_RAMLOG 0xb200585C

static DEFINE_MUTEX(tee_ramlog_lock);
static struct task_struct *tee_ramlog_tsk = NULL;
static unsigned long tee_ramlog_buf_adr = 0;
static unsigned long tee_ramlog_buf_len = 0;
static void *ramlog_vaddr = NULL;

static inline bool valid_ramlog_ptr(unsigned long ptr)
{
	if (ptr < tee_ramlog_buf_adr ||
		ptr > tee_ramlog_buf_adr + tee_ramlog_buf_len)
		return false;

	return true;
}

static int tee_ramlog_init_addr(unsigned long buf_adr,unsigned long buf_len)
{
	if ((buf_adr == 0) || (buf_len == 0)) {
		pr_err("%s(%d): bad argument!\n", __func__, __LINE__);
		return -EINVAL;
	}

	tee_ramlog_buf_adr = buf_adr;
	tee_ramlog_buf_len = buf_len;
	return 0;
}

static void tee_ramlog_dump(void)
{
	char log_buf[MAX_PRINT_SIZE];
	char *log_buff_read_point = NULL;
	char *tmp_point = NULL;
	int log_count = 0;
	unsigned long log_buff_write_point = tee_ramlog_read_wpoint;
	log_buff_read_point = (char* )tee_ramlog_read_rpoint;

	if (!valid_ramlog_ptr(log_buff_write_point) || !valid_ramlog_ptr((unsigned long)log_buff_read_point)) {
		pr_err("tee ramlog: invalid read or write pointer\n");
		return;
	}

	if ((unsigned long)log_buff_read_point == log_buff_write_point)
		return;

	mutex_lock(&tee_ramlog_lock);

	while ((unsigned long)log_buff_read_point != log_buff_write_point) {
		if (isascii(*(log_buff_read_point)) && *(log_buff_read_point) != '\0') {
			if (log_count >= MAX_PRINT_SIZE) {
				log_buf[log_count-2] = '\n';
				log_buf[log_count-1] = '\0';
				pr_info("%s", log_buf);
				log_count = 0;
			} else {
				log_buf[log_count] = *(log_buff_read_point);
				log_count++;
				if (*(log_buff_read_point) == '\n') {
					if (log_count >= MAX_PRINT_SIZE) {
						log_buf[log_count-2] = '\n';
						log_buf[log_count-1] = '\0';
						pr_info("%s", log_buf);
					} else {
						log_buf[log_count] = '\0';
						pr_info("%s", log_buf);
					}
					log_count = 0;
				}
			}
		}

		log_buff_read_point ++;
		tmp_point = (char* )(tee_ramlog_buf_adr+tee_ramlog_buf_len);
		if(log_buff_read_point == tmp_point) {
			tmp_point = (char* )(tee_ramlog_buf_adr + 8);
			log_buff_read_point = tmp_point;
		}
	}
	tee_ramlog_write_rpoint((unsigned long)log_buff_read_point);

	mutex_unlock(&tee_ramlog_lock);
}

static int tee_ramlog_loop(void *p)
{
	while(1) {
		tee_ramlog_dump();
		msleep(500);
	}
	return 0;
}

static int tee_ramlog_set_addr_len(unsigned long buf_adr,unsigned long buf_len)
{
	void* mapping_vaddr = NULL;

	if ((buf_adr == 0) || (buf_len == 0))
		return -EINVAL;

	if (tee_ramlog_tsk) {
		return 0;
	}

	//Check ramlog address and size were configured or not.
	if ((tee_ramlog_buf_adr || tee_ramlog_buf_len) == 0) {
		//Need to init ramlog_mutex
		mutex_init(&tee_ramlog_lock);
	}

	mutex_lock(&tee_ramlog_lock);
	mapping_vaddr = ioremap_cache(buf_adr, buf_len);
	if (mapping_vaddr == NULL) {
		mutex_unlock(&tee_ramlog_lock);
		printk("\033[0;32;31m [OPTEE][RAMLOG] %s %d \033[m\n",__func__,__LINE__);
		return -ENOMEM;
	}

	tee_ramlog_buf_adr = mapping_vaddr;
	tee_ramlog_buf_len = buf_len;
	mutex_unlock(&tee_ramlog_lock);

	if(tee_ramlog_tsk == NULL)
		tee_ramlog_tsk = kthread_run(tee_ramlog_loop, NULL, "tee_ramlog_loop");

	return 0;
}

int optee_config_ramlog(optee_invoke_fn *invoke_fn, struct tee_device *teedev)
{
	struct tee_shm *shm = NULL;
	struct tee_shm_pool_mgr *poolm = NULL;
	int ret = 0;
	int dataSize = 16;
	struct file *pRamlogEnableFile = NULL;
	mm_segment_t old_fs;
	union {
		struct arm_smccc_res smccc;
		struct optee_smc_get_shm_config_result result;
	} res;

	ret = tee_shm_alloc_tmp(&shm, dataSize, teedev);
	if (!shm) {
		pr_err("%s(%d): tee_shm_alloc_tmp failed!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	pRamlogEnableFile = filp_open("/data/tee/ramlog_enable.bin", O_RDONLY, 0);
	if (!IS_ERR(pRamlogEnableFile)) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		pRamlogEnableFile->f_op->read(pRamlogEnableFile,
				tee_shm_get_kaddr(shm), dataSize,
				&pRamlogEnableFile->f_pos);
		set_fs(old_fs);
		filp_close(pRamlogEnableFile, NULL);
	}

	invoke_fn(TEESMC32_ST_FASTCALL_RAMLOG, tee_shm_get_paddr(shm),
				dataSize, 0, 0, 0, 0, 0, &res.smccc);
	if (res.result.status != OPTEE_SMC_RETURN_OK) {
		pr_info("%s(%d): ramlog service not available\n",
			__func__, __LINE__);
		ret = -EINVAL;
		goto exit;
	}

	if (res.result.settings == OPTEE_SMC_SHM_CACHED)
		ramlog_vaddr = ioremap_cache(res.result.start, res.result.size);
	else
		ramlog_vaddr = ioremap_nocache(res.result.start, res.result.size);

	if (!ramlog_vaddr) {
		pr_err("%s(%d): ioremap for ramlog VA failed!\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto exit;
	}

	pr_info("optee_smc_shm is %s\n",
			res.result.settings == OPTEE_SMC_SHM_CACHED ?
			"CACHED" : "NONCACHED");

	ret = tee_ramlog_init_addr((unsigned long)ramlog_vaddr, res.result.size);
	if (ret)
		goto exit;

	tee_ramlog_tsk = kthread_run(tee_ramlog_loop, NULL, "tee_ramlog_loop");
	if (IS_ERR(tee_ramlog_tsk)) {
		pr_err("%s(%d): create ramlog dump thread failed!\n",
				__func__, __LINE__);
		ret = -ENOMEM;
		goto exit;
	}

	pr_info("[OPTEE][RAMLOG] VA %pK, PA %lX, size 0x%lx byte\n",
			ramlog_vaddr, res.result.start, res.result.size);
exit:
	tee_shm_free_tmp(shm, teedev);
	kfree(shm);
	return ret;
}
#endif
static struct optee *optee_probe(struct device_node *np)
{
	optee_invoke_fn *invoke_fn;
	struct tee_shm_pool *pool;
	struct optee *optee = NULL;
	void *memremaped_shm = NULL;
	struct tee_device *teedev;
	u32 sec_caps;
	int rc;

	invoke_fn = get_invoke_func(np);
	if (IS_ERR(invoke_fn))
		return (void *)invoke_fn;

	if (!optee_msg_api_uid_is_optee_api(invoke_fn)) {
		pr_warn("api uid mismatch\n");
		return ERR_PTR(-EINVAL);
	}

	optee_msg_get_os_revision(invoke_fn);

	if (!optee_msg_api_revision_is_compatible(invoke_fn)) {
		pr_warn("api revision mismatch\n");
		return ERR_PTR(-EINVAL);
	}

	if (!optee_msg_exchange_capabilities(invoke_fn, &sec_caps)) {
		pr_warn("capabilities mismatch\n");
		return ERR_PTR(-EINVAL);
	}

	/*
	 * We have no other option for shared memory, if secure world
	 * doesn't have any reserved memory we can use we can't continue.
	 */
	if (!(sec_caps & OPTEE_SMC_SEC_CAP_HAVE_RESERVED_SHM))
		return ERR_PTR(-EINVAL);

	pool = optee_config_shm_memremap(invoke_fn, &memremaped_shm, sec_caps);
	if (IS_ERR(pool))
		return (void *)pool;

	optee = kzalloc(sizeof(*optee), GFP_KERNEL);
	if (!optee) {
		rc = -ENOMEM;
		goto err;
	}

	optee->invoke_fn = invoke_fn;
	optee->sec_caps = sec_caps;

	teedev = tee_device_alloc(&optee_desc, NULL, pool, optee);
	if (IS_ERR(teedev)) {
		rc = PTR_ERR(teedev);
		goto err;
	}
	optee->teedev = teedev;

#ifdef CONFIG_MSTAR_CHIP
	rc = optee_config_ramlog(invoke_fn, teedev);
	if (rc) {
		pr_err("%s(%d): optee_config_ramlog failed!\n", __func__, __LINE__);
	}
#endif

	teedev = tee_device_alloc(&optee_supp_desc, NULL, pool, optee);
	if (IS_ERR(teedev)) {
		rc = PTR_ERR(teedev);
		goto err;
	}
	optee->supp_teedev = teedev;

	rc = tee_device_register(optee->teedev);
	if (rc)
		goto err;

	rc = tee_device_register(optee->supp_teedev);
	if (rc)
		goto err;

	mutex_init(&optee->call_queue.mutex);
	INIT_LIST_HEAD(&optee->call_queue.waiters);
	optee_wait_queue_init(&optee->wait_queue);
	optee_supp_init(&optee->supp);
	optee->memremaped_shm = memremaped_shm;
	optee->pool = pool;

	optee_enable_shm_cache(optee);

	pr_info("initialized driver\n");
	return optee;
err:
	if (optee) {
		/*
		 * tee_device_unregister() is safe to call even if the
		 * devices hasn't been registered with
		 * tee_device_register() yet.
		 */
		tee_device_unregister(optee->supp_teedev);
		tee_device_unregister(optee->teedev);
		kfree(optee);
	}
	if (pool)
		tee_shm_pool_free(pool);
	if (memremaped_shm)
		memunmap(memremaped_shm);
	return ERR_PTR(rc);
}

static void optee_remove(struct optee *optee)
{
	/*
	 * Ask OP-TEE to free all cached shared memory objects to decrease
	 * reference counters and also avoid wild pointers in secure world
	 * into the old shared memory range.
	 */
	optee_disable_shm_cache(optee);

	/*
	 * The two devices has to be unregistered before we can free the
	 * other resources.
	 */
	tee_device_unregister(optee->supp_teedev);
	tee_device_unregister(optee->teedev);

	tee_shm_pool_free(optee->pool);
	if (optee->memremaped_shm)
		memunmap(optee->memremaped_shm);
	optee_wait_queue_exit(&optee->wait_queue);
	optee_supp_uninit(&optee->supp);
	mutex_destroy(&optee->call_queue.mutex);

	kfree(optee);
#ifdef CONFIG_MSTAR_CHIP
	if (ramlog_vaddr)
		iounmap(ramlog_vaddr);
#endif
}

static const struct of_device_id optee_match[] = {
	{ .compatible = "linaro,optee-tz" },
	{},
};

static struct optee *optee_svc;

#ifdef CONFIG_MSTAR_CHIP
#include <linux/proc_fs.h>
#include <linux/suspend.h>


static void notify_lock_smc(void *info)
{
	pr_info("%s(%d): Notify TEE CPU to Back for STR CPU %u\n",
		__func__, __LINE__, smp_processor_id());
	atomic_set(&SMC_UNLOCKED,0);
}

static void notify_unlock_smc(void)
{
	pr_info("%s(%d): Notify TEE CPU Lock SMC\n", __func__, __LINE__);
	atomic_set(&SMC_UNLOCKED,1);
	atomic_set(&STR, 0);
}

static int optee_driver_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	switch (event) {
	case PM_HIBERNATION_PREPARE:
		pr_info("%s(%d): PM_HIBERNATION_PREPARE\n", __func__, __LINE__);
		break;
	case PM_POST_HIBERNATION:
		pr_info("%s(%d): PM_POST_HIBERNATION\n", __func__, __LINE__);
		break;
	case PM_SUSPEND_PREPARE:
		pr_info("%s(%d): PM_SUSPEND_PREPARE\n", __func__, __LINE__);
		atomic_set(&STR, 1);
		smp_call_function(notify_lock_smc, NULL, 1);
		break;
	case PM_POST_SUSPEND:
		pr_info("%s(%d): PM_POST_SUSPEND\n", __func__, __LINE__);
		notify_unlock_smc();
		add_timestamp("tee unlock");
		break;
	case PM_RESTORE_PREPARE:
		pr_info("%s(%d): PM_RESTORE_PREPARE\n", __func__, __LINE__);
		atomic_set(&STR, 1);
		smp_call_function(notify_lock_smc, NULL, 1);
		break;
	case PM_POST_RESTORE :
		pr_info("%s(%d): PM_POST_RESTORE\n", __func__, __LINE__);
		notify_unlock_smc();
		break;
	default:
		pr_info("%s(%d): unknow event 0x%lx\n", __func__, __LINE__, event);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block optee_pm_notifer = {
	.notifier_call = optee_driver_event,
	.priority = INT_MAX, // need to be first
};

static ssize_t tz_write(struct file *filp, const char __user *buffer,
                                    size_t count, loff_t *ppos)
{
	char local_buf[128];
	uint32_t loglevel;
	struct tee *tee;
	struct arm_smccc_res res;

	if (count >= sizeof(local_buf))
		return -EINVAL;

	if (copy_from_user(local_buf, buffer, count))
		return -EFAULT;

	local_buf[count] = 0;

	loglevel = simple_strtol(local_buf, NULL, 10);
	pr_info("[OPTEE][LOGLEVEL] set log level to %d\n", loglevel);

	// Mstar chip supported SMC method only!
	optee_smccc_smc(0xb2005858, loglevel, 0, 0, 0, 0, 0, 0, &res);

	return count;
}

static ssize_t tz_write_ramlog_addr(struct file *filp, const char __user *buffer,
                                    size_t count, loff_t *ppos)
{
	char local_buf[256];
	char* const delim = " ";
  	char *token, *cur;
	int i;
	unsigned long param_value[2];
	struct tee *tee;
	unsigned long val;

	if(count >= 256) {
		pr_err("%s %d count(%zu) > 256 \n", __func__, __LINE__, count);
		return -EINVAL;
	}

	if (copy_from_user(local_buf, buffer, count)) {
		pr_err("%s %d copy_from_user fail\n", __func__, __LINE__);
		return -EFAULT;
	}

	local_buf[count] = 0;
	cur = local_buf;

	for (i = 0 ; i < (sizeof(param_value)/sizeof(param_value[0])) ; i++) {
		int ret;

		token = strsep(&cur, delim);
		if (!token) {
			pr_err("%s %d token is NULL\n", __func__, __LINE__);
			return -EINVAL;
		}

		ret = kstrtoul(token, 16, &val);
		if (ret) {
			pr_err("%s %d kstrtoul error:%d\n", __func__, __LINE__, ret);
			return ret;
		}
		param_value[i] = val;
	}

	printk("\033[0;32;31m [RAMLOG] %s addr = 0x%lx , length = %lu\033[m\n",
		__func__,param_value[0],param_value[1]);

	if (tee_ramlog_set_addr_len(param_value[0],param_value[1])) {
		pr_err("%s - can't configure ramlog\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static struct file_operations tz_fops = {
	.owner   = THIS_MODULE, // system
	.write   = tz_write,
};

static struct file_operations ramlog_fops = {
	.owner	= THIS_MODULE, // system
	.write	= tz_write_ramlog_addr,
};

static ssize_t tz_version_read(struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	const char version_2_4[] = "2.4";
	const char version_3_2[] = "3.2";

	if (count < sizeof(version_3_2)) {
		pr_err("%s(%d): invalid count\n", __func__, __LINE__);
		return count;
	}

	if (optee_version == 3)
		copy_to_user(buf , &version_3_2, sizeof(version_3_2));
	else if (optee_version == 2)
		copy_to_user(buf , &version_2_4, sizeof(version_2_4));

	return count;

}

static struct file_operations tz_fops_version = {
	.owner	= THIS_MODULE, // system
	.read	= tz_version_read,
};

#define USER_ROOT_DIR "tz2_mstar"
#endif

static int __init optee_driver_init(void)
{
	struct device_node *fw_np;
	struct device_node *np;
	struct optee *optee;
#ifdef CONFIG_MSTAR_CHIP
	struct proc_dir_entry *root;
	struct proc_dir_entry *dir;
#endif

	/* Node is supposed to be below /firmware */
	fw_np = of_find_node_by_name(NULL, "firmware");
	if (!fw_np)
		return -ENODEV;

	np = of_find_matching_node(fw_np, optee_match);
	if (!np || !of_device_is_available(np)) {
		of_node_put(np);
		return -ENODEV;
	}

	optee = optee_probe(np);
	of_node_put(np);

	if (IS_ERR(optee))
		return PTR_ERR(optee);

	optee_svc = optee;

#ifdef CONFIG_MSTAR_CHIP
	root = proc_mkdir(USER_ROOT_DIR, NULL);
	if (!root) {
		pr_err("%s(%d): create /proc/%s failed!\n",
			__func__, __LINE__, USER_ROOT_DIR);
		return -ENOMEM;
	}

	dir = proc_create("ramlog_setup", 0644, root, &ramlog_fops);
	if (!dir) {
		pr_err("%s(%d): create /proc/%s/ramlog_setup failed!\n",
			__func__, __LINE__, USER_ROOT_DIR);
		return -ENOMEM;
	}

	dir = proc_create("log_level", 0644, root, &tz_fops);
	if (!dir) {
		pr_err("%s(%d): create /proc/%s/log_level failed!\n",
			__func__, __LINE__, USER_ROOT_DIR);
		remove_proc_entry(USER_ROOT_DIR, NULL);
		return -ENOMEM;
	}

	dir = proc_create("version", 0644, root, &tz_fops_version);
	if (!dir) {
		pr_err("%s(%d): create /proc/%s/version failed!\n",
			__func__, __LINE__, USER_ROOT_DIR);
		remove_proc_entry(USER_ROOT_DIR, NULL);
		return -ENOMEM;
	}

#ifdef CONFIG_PM
	register_pm_notifier(&optee_pm_notifer);
#endif
	if (IS_ENABLED(CONFIG_TEE_2_4))
		pr_warn("Warning: TEE Driver not supported OPTEE-2.4 in this kernel version!\n");
#endif
	return 0;
}
module_init(optee_driver_init);

static void __exit optee_driver_exit(void)
{
	struct optee *optee = optee_svc;

	optee_svc = NULL;
	if (optee)
		optee_remove(optee);
#ifdef CONFIG_MSTAR_CHIP
	remove_proc_subtree(USER_ROOT_DIR, NULL);
#ifdef CONFIG_PM
	unregister_pm_notifier(&optee_pm_notifer);
#endif
#endif
}
module_exit(optee_driver_exit);

MODULE_AUTHOR("Linaro");
MODULE_DESCRIPTION("OP-TEE driver");
MODULE_SUPPORTED_DEVICE("");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
