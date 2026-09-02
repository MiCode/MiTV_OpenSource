/*
 * Cryptographic API.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include "mdrv_mzc_drv.h"
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/lzo.h>
#include <linux/highmem.h>
#include <crypto/internal/scompress.h>

struct hybrid_lzo_ctx {
	void *lzo_comp_mem;
};


static int MZC_hybrid_init(struct crypto_tfm *tfm)
{
	// lzo part.
	struct hybrid_lzo_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->lzo_comp_mem = kmalloc(LZO1X_MEM_COMPRESS,
				    GFP_KERNEL | __GFP_NOWARN);
	if (!ctx->lzo_comp_mem)
		ctx->lzo_comp_mem = vmalloc(LZO1X_MEM_COMPRESS);
	if (!ctx->lzo_comp_mem)
		BUG_ON(1);

	// MZC part is a kernel module

	return 0;
}

static void MZC_hybrid_exit(struct crypto_tfm *tfm)
{
	// lzo part
	struct hybrid_lzo_ctx *ctx = crypto_tfm_ctx(tfm);

	kvfree(ctx->lzo_comp_mem);

	// MZC part is a kernel module
}

static int __MZC_hybrid_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	int err;
	bool is_mzc;
	struct hybrid_lzo_ctx *ctx = crypto_tfm_ctx(tfm);
#ifdef CONFIG_MP_ZSM
	u32 crc32 = 0;
#endif
	if (tfm->is_mzc == INITIAL_VALUE) {
#ifdef ENABLE_SINGLE_MODE
#ifdef CONFIG_MP_ZSM
		err = MDrv_lenc_hybrid_single_run(src, dst, dlen, &crc32);
#else
		err = MDrv_lenc_hybrid_single_run(src, dst, dlen);
#endif
#else
#ifdef CONFIG_MP_ZSM
		err = MDrv_lenc_hybrid_cmdq_run(src, dst, dlen, &crc32);
#else
		err = MDrv_lenc_hybrid_cmdq_run(src, dst, dlen);
#endif
#endif
		if (err == MZC_ERR_NO_ENC_RESOURCE) {
			size_t tmp_len = *dlen;
			unsigned long slen = PAGE_SIZE;
#ifdef CONFIG_MP_ZSM
			err = lzo1x_1_compress_crc(src, slen,dst, &tmp_len,
					ctx->lzo_comp_mem, &crc32);
#else
			err = lzo1x_1_compress(src, slen,dst, &tmp_len, ctx->lzo_comp_mem);
#endif
			if (err == LZO_E_OK)
				*dlen = tmp_len;
			tfm->is_mzc = LZO;
		} else {
			tfm->is_mzc = MZC;
		}
	}
	else
	{
		if (tfm->is_mzc == LZO)
		{
			size_t tmp_len = *dlen;
			unsigned long slen = PAGE_SIZE;
			err = lzo1x_1_compress(src, slen, dst, &tmp_len, ctx->lzo_comp_mem);
			if (err == LZO_E_OK)
				*dlen = tmp_len;
		}
		else if (tfm->is_mzc == MZC)
		{
#ifdef ENABLE_SINGLE_MODE
#ifdef CONFIG_MP_ZSM
			err = MDrv_lenc_single_run(src, dst, dlen, &crc32);
#else
			err = MDrv_lenc_single_run(src, dst, dlen);
#endif
#else
#ifdef CONFIG_MP_ZSM
			err = MDrv_lenc_cmdq_run(src, dst, dlen, &crc32);
#else
			err = MDrv_lenc_cmdq_run(src, dst, dlen);
#endif
#endif
		}
		else {
			BUG_ON(1);
        }
	}	
#ifdef CONFIG_MP_ZSM
	tfm->crc32 = crc32;
#endif
	return err;
}

static int __MZC_hybrid_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	int err;
	if (!tfm->is_mzc)
	{
		size_t tmp_len = *dlen;
		err = lzo1x_decompress_safe(src, slen, dst, &tmp_len);
		if (err == LZO_E_OK)
			*dlen = tmp_len;
		return err;
	}
	else {
#ifdef ENABLE_SINGLE_MODE
    err = MDrv_ldec_single_run(src,dst);
    // if supporting split for single mode, add codes here
#else
#ifdef CONFIG_MP_MZCCMDQ_HW_SPLIT
	if (src == NULL)
		err = MDrv_ldec_cmdq_run_split(tfm->addr1,tfm->addr2,dst);
	else
#endif
	err = MDrv_ldec_cmdq_run(src,dst);
#endif
		return err;
	}
}

static int MZC_hybrid_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __MZC_hybrid_compress(tfm,src,slen,dst,dlen);
}

static int MZC_hybrid_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __MZC_hybrid_decompress(tfm,src,slen,dst,dlen);
}

static int MZC_hybrid_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen, void *ctx)
{
	return __MZC_hybrid_decompress(&tfm->base, src, slen, dst, dlen);
}

static int MZC_hybrid_scompress(struct crypto_scomp *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen, void *ctx)
{
	return __MZC_hybrid_compress(&tfm->base, src, slen, dst, dlen);
}

static void *MZC_hybrid_alloc_ctx(struct crypto_scomp *tfm)
{
	;
}

static void MZC_hybrid_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	;
}


static struct crypto_alg alg = {
	.cra_name		= "mzc_hybrid",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct hybrid_lzo_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= MZC_hybrid_init,
	.cra_exit		= MZC_hybrid_exit,
	.cra_u			= { .compress = {
	.coa_compress 		= MZC_hybrid_compress,
	.coa_decompress  	= MZC_hybrid_decompress } }
};

static struct scomp_alg scomp = {
	.alloc_ctx		= MZC_hybrid_alloc_ctx,
	.free_ctx		= MZC_hybrid_free_ctx,
	.compress		= MZC_hybrid_scompress,
	.decompress		= MZC_hybrid_sdecompress,
	.base			= {
		.cra_name	= "mzc",
		.cra_driver_name = "mzc-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init MZC_hybrid_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit MZC_hybrid_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(MZC_hybrid_mod_init);
module_exit(MZC_hybrid_mod_fini);

MODULE_DESCRIPTION("mzc_hybrid Compression Algorithm");
MODULE_ALIAS_CRYPTO("mzc_hybrid");

