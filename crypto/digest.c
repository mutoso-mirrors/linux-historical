/*
 * Cryptographic API.
 *
 * Digest operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include "internal.h"

static void init(struct crypto_tfm *tfm)
{
	tfm->__crt_alg->cra_digest.dia_init(crypto_tfm_ctx(tfm));
}

static void update(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg)
{
	unsigned int i;
	
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page, 0) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(crypto_tfm_ctx(tfm),
		                                      p, sg[i].length);
		crypto_kunmap(p, 0);
		crypto_yield(tfm);
	}
}

static void final(struct crypto_tfm *tfm, u8 *out)
{
	tfm->__crt_alg->cra_digest.dia_final(crypto_tfm_ctx(tfm), out);
}

static void digest(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	unsigned int i;

	tfm->crt_digest.dit_init(tfm);
		
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page, 0) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(crypto_tfm_ctx(tfm),
		                                      p, sg[i].length);
		crypto_kunmap(p, 0);
		crypto_yield(tfm);
	}
	crypto_digest_final(tfm, out);
}

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags)
{
	return crypto_cipher_flags(flags) ? -EINVAL : 0;
}

int crypto_init_digest_ops(struct crypto_tfm *tfm)
{
	struct digest_tfm *ops = &tfm->crt_digest;
	
	ops->dit_init	= init;
	ops->dit_update	= update;
	ops->dit_final	= final;
	ops->dit_digest	= digest;
	
	return crypto_alloc_hmac_block(tfm);
}

void crypto_exit_digest_ops(struct crypto_tfm *tfm)
{
	crypto_free_hmac_block(tfm);
}
