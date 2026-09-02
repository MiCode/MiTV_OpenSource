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
#include <crypto/internal/scompress.h>


static int MZC_init(struct crypto_tfm *tfm)
{
	return 0;
}

static void *MZC_alloc_ctx(struct crypto_scomp *tfm)
{
	;
}

static void MZC_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	;
}

static void MZC_exit(struct crypto_tfm *tfm)
{
}

static int __MZC_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	int err;
#ifdef CONFIG_MP_ZSM
	u32 crc32;
#endif
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
#ifdef CONFIG_MP_ZSM
	tfm->crc32 = crc32;
#endif
	return err;
}

static int __MZC_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	int err;
#ifdef ENABLE_SINGLE_MODE
    err = MDrv_ldec_single_run(src,dst);
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

static int MZC_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
	tfm->is_mzc = 1;
#endif
	return __MZC_compress(tfm, src, slen, dst, dlen);
}

static int MZC_scompress(struct crypto_scomp *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen, void *ctx)
{
	return __MZC_compress(&tfm->base, src, slen, dst, dlen);
}

static int MZC_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen, void *ctx)
{
	return __MZC_decompress(&tfm->base, src,  slen,  dst,  dlen);
}

static int MZC_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __MZC_decompress(tfm, src,  slen,  dst,  dlen);
}

static struct crypto_alg alg = {
	.cra_name		= "mzc",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= 0,
	.cra_module		= THIS_MODULE,
	.cra_init		= MZC_init,
	.cra_exit		= MZC_exit,
	.cra_u			= { .compress = {
	.coa_compress 		= MZC_compress,
	.coa_decompress  	= MZC_decompress } }
};

static struct scomp_alg scomp = {
	.alloc_ctx		= MZC_alloc_ctx,
	.free_ctx		= MZC_free_ctx,
	.compress		= MZC_scompress,
	.decompress		= MZC_sdecompress,
	.base			= {
		.cra_name	= "mzc",
		.cra_driver_name = "mzc-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init MZC_mod_init(void)
{
	int ret;
	
	ret = crypto_register_alg(&alg);
	if (ret)
		return ret;
	ret = crypto_register_scomp(&scomp);
	if (ret) {
		crypto_unregister_alg(&alg);
		return ret;
	}

	return ret;	
}

static void __exit MZC_mod_fini(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_scomp(&scomp);
}

module_init(MZC_mod_init);
module_exit(MZC_mod_fini);

MODULE_DESCRIPTION("MZC Compression Algorithm");
MODULE_ALIAS_CRYPTO("mzc");

