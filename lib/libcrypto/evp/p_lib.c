/* $OpenBSD: p_lib.c,v 1.38 2023/11/19 15:46:10 tb Exp $ */
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

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/bn.h>
#include <openssl/cmac.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif
#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#include "asn1_local.h"
#include "evp_local.h"

static void EVP_PKEY_free_it(EVP_PKEY *x);

int
EVP_PKEY_bits(const EVP_PKEY *pkey)
{
	if (pkey && pkey->ameth && pkey->ameth->pkey_bits)
		return pkey->ameth->pkey_bits(pkey);
	return 0;
}

int
EVP_PKEY_security_bits(const EVP_PKEY *pkey)
{
	if (pkey == NULL)
		return 0;
	if (pkey->ameth == NULL || pkey->ameth->pkey_security_bits == NULL)
		return -2;

	return pkey->ameth->pkey_security_bits(pkey);
}

int
EVP_PKEY_size(const EVP_PKEY *pkey)
{
	if (pkey && pkey->ameth && pkey->ameth->pkey_size)
		return pkey->ameth->pkey_size(pkey);
	return 0;
}

int
EVP_PKEY_save_parameters(EVP_PKEY *pkey, int mode)
{
#ifndef OPENSSL_NO_DSA
	if (pkey->type == EVP_PKEY_DSA) {
		int ret = pkey->save_parameters;

		if (mode >= 0)
			pkey->save_parameters = mode;
		return (ret);
	}
#endif
#ifndef OPENSSL_NO_EC
	if (pkey->type == EVP_PKEY_EC) {
		int ret = pkey->save_parameters;

		if (mode >= 0)
			pkey->save_parameters = mode;
		return (ret);
	}
#endif
	return (0);
}

int
EVP_PKEY_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from)
{
	if (to->type != from->type) {
		EVPerror(EVP_R_DIFFERENT_KEY_TYPES);
		goto err;
	}

	if (EVP_PKEY_missing_parameters(from)) {
		EVPerror(EVP_R_MISSING_PARAMETERS);
		goto err;
	}
	if (from->ameth && from->ameth->param_copy)
		return from->ameth->param_copy(to, from);

err:
	return 0;
}

int
EVP_PKEY_missing_parameters(const EVP_PKEY *pkey)
{
	if (pkey->ameth && pkey->ameth->param_missing)
		return pkey->ameth->param_missing(pkey);
	return 0;
}

int
EVP_PKEY_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (a->type != b->type)
		return -1;
	if (a->ameth && a->ameth->param_cmp)
		return a->ameth->param_cmp(a, b);
	return -2;
}

int
EVP_PKEY_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (a->type != b->type)
		return -1;

	if (a->ameth) {
		int ret;
		/* Compare parameters if the algorithm has them */
		if (a->ameth->param_cmp) {
			ret = a->ameth->param_cmp(a, b);
			if (ret <= 0)
				return ret;
		}

		if (a->ameth->pub_cmp)
			return a->ameth->pub_cmp(a, b);
	}

	return -2;
}

EVP_PKEY *
EVP_PKEY_new(void)
{
	EVP_PKEY *ret;

	ret = malloc(sizeof(EVP_PKEY));
	if (ret == NULL) {
		EVPerror(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}
	ret->type = EVP_PKEY_NONE;
	ret->save_type = EVP_PKEY_NONE;
	ret->references = 1;
	ret->ameth = NULL;
	ret->engine = NULL;
	ret->pkey.ptr = NULL;
	ret->attributes = NULL;
	ret->save_parameters = 1;
	return (ret);
}

int
EVP_PKEY_up_ref(EVP_PKEY *pkey)
{
	int refs = CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY);
	return ((refs > 1) ? 1 : 0);
}

/* Setup a public key ASN1 method and ENGINE from a NID or a string.
 * If pkey is NULL just return 1 or 0 if the algorithm exists.
 */

static int
pkey_set_type(EVP_PKEY *pkey, ENGINE *e, int type, const char *str, int len)
{
	const EVP_PKEY_ASN1_METHOD *ameth;
	ENGINE **eptr = NULL;

	if (e == NULL)
		eptr = &e;

	if (pkey) {
		if (pkey->pkey.ptr)
			EVP_PKEY_free_it(pkey);
		/* If key type matches and a method exists then this
		 * lookup has succeeded once so just indicate success.
		 */
		if ((type == pkey->save_type) && pkey->ameth)
			return 1;
	}
	if (str)
		ameth = EVP_PKEY_asn1_find_str(eptr, str, len);
	else
		ameth = EVP_PKEY_asn1_find(eptr, type);
	if (!ameth) {
		EVPerror(EVP_R_UNSUPPORTED_ALGORITHM);
		return 0;
	}
	if (pkey) {
		pkey->ameth = ameth;
		pkey->engine = e;

		pkey->type = pkey->ameth->pkey_id;
		pkey->save_type = type;
	}
	return 1;
}

int
EVP_PKEY_set_type(EVP_PKEY *pkey, int type)
{
	return pkey_set_type(pkey, NULL, type, NULL, -1);
}

EVP_PKEY *
EVP_PKEY_new_raw_private_key(int type, ENGINE *engine,
    const unsigned char *private_key, size_t len)
{
	EVP_PKEY *ret;

	if ((ret = EVP_PKEY_new()) == NULL)
		goto err;

	if (!pkey_set_type(ret, engine, type, NULL, -1))
		goto err;

	if (ret->ameth->set_priv_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		goto err;
	}
	if (!ret->ameth->set_priv_key(ret, private_key, len)) {
		EVPerror(EVP_R_KEY_SETUP_FAILED);
		goto err;
	}

	return ret;

 err:
	EVP_PKEY_free(ret);

	return NULL;
}

EVP_PKEY *
EVP_PKEY_new_raw_public_key(int type, ENGINE *engine,
    const unsigned char *public_key, size_t len)
{
	EVP_PKEY *ret;

	if ((ret = EVP_PKEY_new()) == NULL)
		goto err;

	if (!pkey_set_type(ret, engine, type, NULL, -1))
		goto err;

	if (ret->ameth->set_pub_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		goto err;
	}
	if (!ret->ameth->set_pub_key(ret, public_key, len)) {
		EVPerror(EVP_R_KEY_SETUP_FAILED);
		goto err;
	}

	return ret;

 err:
	EVP_PKEY_free(ret);

	return NULL;
}

int
EVP_PKEY_get_raw_private_key(const EVP_PKEY *pkey,
    unsigned char *out_private_key, size_t *out_len)
{
	if (pkey->ameth->get_priv_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return 0;
	}
	if (!pkey->ameth->get_priv_key(pkey, out_private_key, out_len)) {
		EVPerror(EVP_R_GET_RAW_KEY_FAILED);
		return 0;
	}

	return 1;
}

int
EVP_PKEY_get_raw_public_key(const EVP_PKEY *pkey,
    unsigned char *out_public_key, size_t *out_len)
{
	if (pkey->ameth->get_pub_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return 0;
	}
	if (!pkey->ameth->get_pub_key(pkey, out_public_key, out_len)) {
		EVPerror(EVP_R_GET_RAW_KEY_FAILED);
		return 0;
	}

	return 1;
}

EVP_PKEY *
EVP_PKEY_new_CMAC_key(ENGINE *e, const unsigned char *priv, size_t len,
    const EVP_CIPHER *cipher)
{
	EVP_PKEY *ret = NULL;
	CMAC_CTX *cmctx = NULL;

	if ((ret = EVP_PKEY_new()) == NULL)
		goto err;
	if ((cmctx = CMAC_CTX_new()) == NULL)
		goto err;

	if (!pkey_set_type(ret, e, EVP_PKEY_CMAC, NULL, -1))
		goto err;

	if (!CMAC_Init(cmctx, priv, len, cipher, e)) {
		EVPerror(EVP_R_KEY_SETUP_FAILED);
		goto err;
	}

	ret->pkey.ptr = cmctx;

	return ret;

 err:
	EVP_PKEY_free(ret);
	CMAC_CTX_free(cmctx);
	return NULL;
}

int
EVP_PKEY_set_type_str(EVP_PKEY *pkey, const char *str, int len)
{
	return pkey_set_type(pkey, NULL, EVP_PKEY_NONE, str, len);
}

int
EVP_PKEY_assign(EVP_PKEY *pkey, int type, void *key)
{
	if (!EVP_PKEY_set_type(pkey, type))
		return 0;
	pkey->pkey.ptr = key;
	return (key != NULL);
}

void *
EVP_PKEY_get0(const EVP_PKEY *pkey)
{
	return pkey->pkey.ptr;
}

const unsigned char *
EVP_PKEY_get0_hmac(const EVP_PKEY *pkey, size_t *len)
{
	ASN1_OCTET_STRING *os;

	if (pkey->type != EVP_PKEY_HMAC) {
		EVPerror(EVP_R_EXPECTING_AN_HMAC_KEY);
		return NULL;
	}

	os = EVP_PKEY_get0(pkey);
	*len = os->length;

	return os->data;
}

#ifndef OPENSSL_NO_RSA
RSA *
EVP_PKEY_get0_RSA(EVP_PKEY *pkey)
{
	if (pkey->type == EVP_PKEY_RSA || pkey->type == EVP_PKEY_RSA_PSS)
		return pkey->pkey.rsa;

	EVPerror(EVP_R_EXPECTING_AN_RSA_KEY);
	return NULL;
}

RSA *
EVP_PKEY_get1_RSA(EVP_PKEY *pkey)
{
	RSA *rsa;

	if ((rsa = EVP_PKEY_get0_RSA(pkey)) == NULL)
		return NULL;

	RSA_up_ref(rsa);

	return rsa;
}

int
EVP_PKEY_set1_RSA(EVP_PKEY *pkey, RSA *key)
{
	int ret = EVP_PKEY_assign_RSA(pkey, key);
	if (ret != 0)
		RSA_up_ref(key);
	return ret;
}
#endif

#ifndef OPENSSL_NO_DSA
DSA *
EVP_PKEY_get0_DSA(EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_DSA) {
		EVPerror(EVP_R_EXPECTING_A_DSA_KEY);
		return NULL;
	}
	return pkey->pkey.dsa;
}

DSA *
EVP_PKEY_get1_DSA(EVP_PKEY *pkey)
{
	DSA *dsa;

	if ((dsa = EVP_PKEY_get0_DSA(pkey)) == NULL)
		return NULL;

	DSA_up_ref(dsa);

	return dsa;
}

int
EVP_PKEY_set1_DSA(EVP_PKEY *pkey, DSA *key)
{
	int ret = EVP_PKEY_assign_DSA(pkey, key);
	if (ret != 0)
		DSA_up_ref(key);
	return ret;
}
#endif

#ifndef OPENSSL_NO_EC
EC_KEY *
EVP_PKEY_get0_EC_KEY(EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_EC) {
		EVPerror(EVP_R_EXPECTING_A_EC_KEY);
		return NULL;
	}
	return pkey->pkey.ec;
}

EC_KEY *
EVP_PKEY_get1_EC_KEY(EVP_PKEY *pkey)
{
	EC_KEY *key;

	if ((key = EVP_PKEY_get0_EC_KEY(pkey)) == NULL)
		return NULL;

	EC_KEY_up_ref(key);

	return key;
}

int
EVP_PKEY_set1_EC_KEY(EVP_PKEY *pkey, EC_KEY *key)
{
	int ret = EVP_PKEY_assign_EC_KEY(pkey, key);
	if (ret != 0)
		EC_KEY_up_ref(key);
	return ret;
}
#endif


#ifndef OPENSSL_NO_DH
DH *
EVP_PKEY_get0_DH(EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_DH) {
		EVPerror(EVP_R_EXPECTING_A_DH_KEY);
		return NULL;
	}
	return pkey->pkey.dh;
}

DH *
EVP_PKEY_get1_DH(EVP_PKEY *pkey)
{
	DH *dh;

	if ((dh = EVP_PKEY_get0_DH(pkey)) == NULL)
		return NULL;

	DH_up_ref(dh);

	return dh;
}

int
EVP_PKEY_set1_DH(EVP_PKEY *pkey, DH *key)
{
	int ret = EVP_PKEY_assign_DH(pkey, key);
	if (ret != 0)
		DH_up_ref(key);
	return ret;
}
#endif

int
EVP_PKEY_type(int type)
{
	int ret;
	const EVP_PKEY_ASN1_METHOD *ameth;
	ENGINE *e;
	ameth = EVP_PKEY_asn1_find(&e, type);
	if (ameth)
		ret = ameth->pkey_id;
	else
		ret = NID_undef;
	return ret;
}

int
EVP_PKEY_id(const EVP_PKEY *pkey)
{
	return pkey->type;
}

int
EVP_PKEY_base_id(const EVP_PKEY *pkey)
{
	return EVP_PKEY_type(pkey->type);
}

void
EVP_PKEY_free(EVP_PKEY *x)
{
	int i;

	if (x == NULL)
		return;

	i = CRYPTO_add(&x->references, -1, CRYPTO_LOCK_EVP_PKEY);
	if (i > 0)
		return;

	EVP_PKEY_free_it(x);
	if (x->attributes)
		sk_X509_ATTRIBUTE_pop_free(x->attributes, X509_ATTRIBUTE_free);
	free(x);
}

static void
EVP_PKEY_free_it(EVP_PKEY *x)
{
	if (x->ameth && x->ameth->pkey_free) {
		x->ameth->pkey_free(x);
		x->pkey.ptr = NULL;
	}
}

static int
unsup_alg(BIO *out, const EVP_PKEY *pkey, int indent, const char *kstr)
{
	if (!BIO_indent(out, indent, 128))
		return 0;
	BIO_printf(out, "%s algorithm \"%s\" unsupported\n",
	    kstr, OBJ_nid2ln(pkey->type));
	return 1;
}

int
EVP_PKEY_print_public(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx)
{
	if (pkey->ameth && pkey->ameth->pub_print)
		return pkey->ameth->pub_print(out, pkey, indent, pctx);

	return unsup_alg(out, pkey, indent, "Public Key");
}

int
EVP_PKEY_print_private(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx)
{
	if (pkey->ameth && pkey->ameth->priv_print)
		return pkey->ameth->priv_print(out, pkey, indent, pctx);

	return unsup_alg(out, pkey, indent, "Private Key");
}

int
EVP_PKEY_print_params(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx)
{
	if (pkey->ameth && pkey->ameth->param_print)
		return pkey->ameth->param_print(out, pkey, indent, pctx);
	return unsup_alg(out, pkey, indent, "Parameters");
}

int
EVP_PKEY_get_default_digest_nid(EVP_PKEY *pkey, int *pnid)
{
	if (!pkey->ameth || !pkey->ameth->pkey_ctrl)
		return -2;
	return pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_DEFAULT_MD_NID,
	    0, pnid);
}
