/* $OpenBSD: evp_enc.c,v 1.55 2023/11/19 15:46:09 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include <openssl/opensslconf.h>

#include <openssl/err.h>
#include <openssl/evp.h>

#include "evp_local.h"

int
EVP_CipherInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv, int enc)
{
	if (cipher != NULL)
		EVP_CIPHER_CTX_cleanup(ctx);
	return EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, enc);
}

int
EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
    const unsigned char *key, const unsigned char *iv, int enc)
{
	if (enc == -1)
		enc = ctx->encrypt;
	else {
		if (enc)
			enc = 1;
		ctx->encrypt = enc;
	}
	if (cipher) {
		/* Ensure a context left lying around from last time is cleared
		 * (the previous check attempted to avoid this if the same
		 * ENGINE and EVP_CIPHER could be used). */
		if (ctx->cipher) {
			unsigned long flags = ctx->flags;
			EVP_CIPHER_CTX_cleanup(ctx);
			/* Restore encrypt and flags */
			ctx->encrypt = enc;
			ctx->flags = flags;
		}

		ctx->cipher = cipher;
		if (ctx->cipher->ctx_size) {
			ctx->cipher_data = calloc(1, ctx->cipher->ctx_size);
			if (ctx->cipher_data == NULL) {
				EVPerror(ERR_R_MALLOC_FAILURE);
				return 0;
			}
		} else {
			ctx->cipher_data = NULL;
		}
		ctx->key_len = cipher->key_len;
		ctx->flags &= EVP_CIPHER_CTX_FLAG_WRAP_ALLOW;
		if (ctx->cipher->flags & EVP_CIPH_CTRL_INIT) {
			if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_INIT, 0, NULL)) {
				EVPerror(EVP_R_INITIALIZATION_ERROR);
				return 0;
			}
		}
	} else if (!ctx->cipher) {
		EVPerror(EVP_R_NO_CIPHER_SET);
		return 0;
	}
	/* we assume block size is a power of 2 in *cryptUpdate */
	if (ctx->cipher->block_size != 1 &&
	    ctx->cipher->block_size != 8 &&
	    ctx->cipher->block_size != 16) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}

	if (!(ctx->flags & EVP_CIPHER_CTX_FLAG_WRAP_ALLOW) &&
	    EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_WRAP_MODE) {
		EVPerror(EVP_R_WRAP_MODE_NOT_ALLOWED);
		return 0;
	}

	if (!(EVP_CIPHER_CTX_flags(ctx) & EVP_CIPH_CUSTOM_IV)) {
		int iv_len;

		switch (EVP_CIPHER_CTX_mode(ctx)) {

		case EVP_CIPH_STREAM_CIPHER:
		case EVP_CIPH_ECB_MODE:
			break;

		case EVP_CIPH_CFB_MODE:
		case EVP_CIPH_OFB_MODE:

			ctx->num = 0;
			/* fall-through */

		case EVP_CIPH_CBC_MODE:
			iv_len = EVP_CIPHER_CTX_iv_length(ctx);
			if (iv_len < 0 || iv_len > sizeof(ctx->oiv)) {
				EVPerror(EVP_R_IV_TOO_LARGE);
				return 0;
			}
			if (iv != NULL)
				memcpy(ctx->oiv, iv, iv_len);
			memcpy(ctx->iv, ctx->oiv, iv_len);
			break;

		case EVP_CIPH_CTR_MODE:
			ctx->num = 0;
			iv_len = EVP_CIPHER_CTX_iv_length(ctx);
			if (iv_len < 0 || iv_len > sizeof(ctx->iv)) {
				EVPerror(EVP_R_IV_TOO_LARGE);
				return 0;
			}
			/* Don't reuse IV for CTR mode */
			if (iv != NULL)
				memcpy(ctx->iv, iv, iv_len);
			break;

		default:
			return 0;
			break;
		}
	}

	if (key || (ctx->cipher->flags & EVP_CIPH_ALWAYS_CALL_INIT)) {
		if (!ctx->cipher->init(ctx, key, iv, enc))
			return 0;
	}
	ctx->buf_len = 0;
	ctx->final_used = 0;
	ctx->block_mask = ctx->cipher->block_size - 1;
	return 1;
}

int
EVP_CipherUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl)
{
	if (ctx->encrypt)
		return EVP_EncryptUpdate(ctx, out, outl, in, inl);
	else
		return EVP_DecryptUpdate(ctx, out, outl, in, inl);
}

int
EVP_CipherFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
	if (ctx->encrypt)
		return EVP_EncryptFinal_ex(ctx, out, outl);
	else
		return EVP_DecryptFinal_ex(ctx, out, outl);
}

__warn_references(EVP_CipherFinal,
    "EVP_CipherFinal is often misused, please use EVP_CipherFinal_ex and EVP_CIPHER_CTX_cleanup");

int
EVP_CipherFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
	int ret;
	if (ctx->encrypt)
		ret = EVP_EncryptFinal_ex(ctx, out, outl);
	else
		ret = EVP_DecryptFinal_ex(ctx, out, outl);
	return ret;
}

int
EVP_EncryptInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit(ctx, cipher, key, iv, 1);
}

int
EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit_ex(ctx, cipher, impl, key, iv, 1);
}

int
EVP_DecryptInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit(ctx, cipher, key, iv, 0);
}

int
EVP_DecryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit_ex(ctx, cipher, impl, key, iv, 0);
}

int
EVP_EncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl)
{
	int i, j, bl;

	*outl = 0;

	if (inl < 0)
		return 0;

	if (inl == 0 && EVP_CIPHER_mode(ctx->cipher) != EVP_CIPH_CCM_MODE)
		return 1;

	if (ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) {
		i = ctx->cipher->do_cipher(ctx, out, in, inl);
		if (i < 0)
			return 0;
		else
			*outl = i;
		return 1;
	}

	if (ctx->buf_len == 0 && (inl&(ctx->block_mask)) == 0) {
		if (ctx->cipher->do_cipher(ctx, out, in, inl)) {
			*outl = inl;
			return 1;
		} else {
			*outl = 0;
			return 0;
		}
	}
	i = ctx->buf_len;
	bl = ctx->cipher->block_size;
	if ((size_t)bl > sizeof(ctx->buf)) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		*outl = 0;
		return 0;
	}
	if (i != 0) {
		if (bl - i > inl) {
			memcpy(&(ctx->buf[i]), in, inl);
			ctx->buf_len += inl;
			*outl = 0;
			return 1;
		} else {
			j = bl - i;

			/*
			 * Once we've processed the first j bytes from in, the
			 * amount of data left that is a multiple of the block
			 * length is (inl - j) & ~(bl - 1).  Ensure this plus
			 * the block processed from ctx-buf doesn't overflow.
			 */
			if (((inl - j) & ~(bl - 1)) > INT_MAX - bl) {
				EVPerror(EVP_R_TOO_LARGE);
				return 0;
			}
			memcpy(&(ctx->buf[i]), in, j);
			if (!ctx->cipher->do_cipher(ctx, out, ctx->buf, bl))
				return 0;
			inl -= j;
			in += j;
			out += bl;
			*outl = bl;
		}
	} else
		*outl = 0;
	i = inl&(bl - 1);
	inl -= i;
	if (inl > 0) {
		if (!ctx->cipher->do_cipher(ctx, out, in, inl))
			return 0;
		*outl += inl;
	}

	if (i != 0)
		memcpy(ctx->buf, &(in[inl]), i);
	ctx->buf_len = i;
	return 1;
}

__warn_references(EVP_EncryptFinal,
    "EVP_EncryptFinal is often misused, please use EVP_EncryptFinal_ex and EVP_CIPHER_CTX_cleanup");

int
EVP_EncryptFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
	int ret;

	ret = EVP_EncryptFinal_ex(ctx, out, outl);
	return ret;
}

int
EVP_EncryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
	int n, ret;
	unsigned int i, b, bl;

	if (ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) {
		ret = ctx->cipher->do_cipher(ctx, out, NULL, 0);
		if (ret < 0)
			return 0;
		else
			*outl = ret;
		return 1;
	}

	b = ctx->cipher->block_size;
	if (b > sizeof ctx->buf) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}
	if (b == 1) {
		*outl = 0;
		return 1;
	}
	bl = ctx->buf_len;
	if (ctx->flags & EVP_CIPH_NO_PADDING) {
		if (bl) {
			EVPerror(EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH);
			return 0;
		}
		*outl = 0;
		return 1;
	}

	n = b - bl;
	for (i = bl; i < b; i++)
		ctx->buf[i] = n;
	ret = ctx->cipher->do_cipher(ctx, out, ctx->buf, b);


	if (ret)
		*outl = b;

	return ret;
}

int
EVP_DecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl)
{
	int fix_len;
	unsigned int b;

	*outl = 0;

	if (inl < 0)
		return 0;

	if (inl == 0 && EVP_CIPHER_mode(ctx->cipher) != EVP_CIPH_CCM_MODE)
		return 1;

	if (ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) {
		fix_len = ctx->cipher->do_cipher(ctx, out, in, inl);
		if (fix_len < 0) {
			*outl = 0;
			return 0;
		} else
			*outl = fix_len;
		return 1;
	}

	if (ctx->flags & EVP_CIPH_NO_PADDING)
		return EVP_EncryptUpdate(ctx, out, outl, in, inl);

	b = ctx->cipher->block_size;
	if (b > sizeof ctx->final) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}

	if (ctx->final_used) {
		/*
		 * final_used is only ever set if buf_len is 0. Therefore the
		 * maximum length output we will ever see from EVP_EncryptUpdate
		 * is inl & ~(b - 1). Since final_used is set, the final output
		 * length is (inl & ~(b - 1)) + b. Ensure it doesn't overflow.
		 */
		if ((inl & ~(b - 1)) > INT_MAX - b) {
			EVPerror(EVP_R_TOO_LARGE);
			return 0;
		}
		memcpy(out, ctx->final, b);
		out += b;
		fix_len = 1;
	} else
		fix_len = 0;


	if (!EVP_EncryptUpdate(ctx, out, outl, in, inl))
		return 0;

	/* if we have 'decrypted' a multiple of block size, make sure
	 * we have a copy of this last block */
	if (b > 1 && !ctx->buf_len) {
		*outl -= b;
		ctx->final_used = 1;
		memcpy(ctx->final, &out[*outl], b);
	} else
		ctx->final_used = 0;

	if (fix_len)
		*outl += b;

	return 1;
}

__warn_references(EVP_DecryptFinal,
    "EVP_DecryptFinal is often misused, please use EVP_DecryptFinal_ex and EVP_CIPHER_CTX_cleanup");

int
EVP_DecryptFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
	int ret;

	ret = EVP_DecryptFinal_ex(ctx, out, outl);
	return ret;
}

int
EVP_DecryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
	int i, n;
	unsigned int b;
	*outl = 0;

	if (ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) {
		i = ctx->cipher->do_cipher(ctx, out, NULL, 0);
		if (i < 0)
			return 0;
		else
			*outl = i;
		return 1;
	}

	b = ctx->cipher->block_size;
	if (ctx->flags & EVP_CIPH_NO_PADDING) {
		if (ctx->buf_len) {
			EVPerror(EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH);
			return 0;
		}
		*outl = 0;
		return 1;
	}
	if (b > 1) {
		if (ctx->buf_len || !ctx->final_used) {
			EVPerror(EVP_R_WRONG_FINAL_BLOCK_LENGTH);
			return (0);
		}
		if (b > sizeof ctx->final) {
			EVPerror(EVP_R_BAD_BLOCK_LENGTH);
			return 0;
		}
		n = ctx->final[b - 1];
		if (n == 0 || n > (int)b) {
			EVPerror(EVP_R_BAD_DECRYPT);
			return (0);
		}
		for (i = 0; i < n; i++) {
			if (ctx->final[--b] != n) {
				EVPerror(EVP_R_BAD_DECRYPT);
				return (0);
			}
		}
		n = ctx->cipher->block_size - n;
		for (i = 0; i < n; i++)
			out[i] = ctx->final[i];
		*outl = n;
	} else
		*outl = 0;
	return (1);
}

EVP_CIPHER_CTX *
EVP_CIPHER_CTX_new(void)
{
	return calloc(1, sizeof(EVP_CIPHER_CTX));
}

void
EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx)
{
	if (ctx == NULL)
		return;

	EVP_CIPHER_CTX_cleanup(ctx);

	free(ctx);
}

void
EVP_CIPHER_CTX_init(EVP_CIPHER_CTX *ctx)
{
	memset(ctx, 0, sizeof(EVP_CIPHER_CTX));
}

int
EVP_CIPHER_CTX_reset(EVP_CIPHER_CTX *a)
{
	return EVP_CIPHER_CTX_cleanup(a);
}

int
EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *c)
{
	if (c->cipher != NULL) {
		/* XXX - Avoid leaks, so ignore return value of cleanup()... */
		if (c->cipher->cleanup != NULL)
			c->cipher->cleanup(c);
		if (c->cipher_data != NULL)
			explicit_bzero(c->cipher_data, c->cipher->ctx_size);
	}

	/* XXX - store size of cipher_data so we can always freezero(). */
	free(c->cipher_data);

	explicit_bzero(c, sizeof(EVP_CIPHER_CTX));

	return 1;
}

int
EVP_CIPHER_CTX_set_key_length(EVP_CIPHER_CTX *c, int keylen)
{
	if (c->cipher->flags & EVP_CIPH_CUSTOM_KEY_LENGTH)
		return EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_SET_KEY_LENGTH,
		    keylen, NULL);
	if (c->key_len == keylen)
		return 1;
	if ((keylen > 0) && (c->cipher->flags & EVP_CIPH_VARIABLE_LENGTH)) {
		c->key_len = keylen;
		return 1;
	}
	EVPerror(EVP_R_INVALID_KEY_LENGTH);
	return 0;
}

int
EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *ctx, int pad)
{
	if (pad)
		ctx->flags &= ~EVP_CIPH_NO_PADDING;
	else
		ctx->flags |= EVP_CIPH_NO_PADDING;
	return 1;
}

int
EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
{
	int ret;

	if (!ctx->cipher) {
		EVPerror(EVP_R_NO_CIPHER_SET);
		return 0;
	}

	if (!ctx->cipher->ctrl) {
		EVPerror(EVP_R_CTRL_NOT_IMPLEMENTED);
		return 0;
	}

	ret = ctx->cipher->ctrl(ctx, type, arg, ptr);
	if (ret == -1) {
		EVPerror(EVP_R_CTRL_OPERATION_NOT_IMPLEMENTED);
		return 0;
	}
	return ret;
}

int
EVP_CIPHER_CTX_rand_key(EVP_CIPHER_CTX *ctx, unsigned char *key)
{
	if (ctx->cipher->flags & EVP_CIPH_RAND_KEY)
		return EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_RAND_KEY, 0, key);
	arc4random_buf(key, ctx->key_len);
	return 1;
}

int
EVP_CIPHER_CTX_copy(EVP_CIPHER_CTX *out, const EVP_CIPHER_CTX *in)
{
	if ((in == NULL) || (in->cipher == NULL)) {
		EVPerror(EVP_R_INPUT_NOT_INITIALIZED);
		return 0;
	}

	EVP_CIPHER_CTX_cleanup(out);
	memcpy(out, in, sizeof *out);

	if (in->cipher_data && in->cipher->ctx_size) {
		out->cipher_data = calloc(1, in->cipher->ctx_size);
		if (out->cipher_data == NULL) {
			EVPerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		memcpy(out->cipher_data, in->cipher_data, in->cipher->ctx_size);
	}

	if (in->cipher->flags & EVP_CIPH_CUSTOM_COPY) {
		if (!in->cipher->ctrl((EVP_CIPHER_CTX *)in, EVP_CTRL_COPY,
		    0, out)) {
			/*
			 * If the custom copy control failed, assume that there
			 * may still be pointers copied in the cipher_data that
			 * we do not own. This may result in a leak from a bad
			 * custom copy control, but that's preferable to a
			 * double free...
			 */
			freezero(out->cipher_data, in->cipher->ctx_size);
			out->cipher_data = NULL;
			return 0;
		}
	}

	return 1;
}
