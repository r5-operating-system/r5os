/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth LE Security Manager cryptographic toolbox, Core 6.0, Vol 3,
 * Part H, 2.2, on OpenSSL 3 libcrypto (EVP interfaces). See SMPCrypto.h for
 * the byte order of every argument.
 */


#include "SMPCrypto.h"

#include <string.h>

#include <SupportDefs.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/params.h>


static const char* kCurveName = "prime256v1";

/* SALT of f5, MSB-first. */
static const uint8 kF5Salt[16] = {
	0x6c, 0x88, 0x83, 0x91, 0xaa, 0xf5, 0xa5, 0x38,
	0x60, 0x37, 0x0b, 0xdb, 0x5a, 0x60, 0x83, 0xbe
};

/* keyID of f5, "btle", MSB-first. */
static const uint8 kF5KeyId[4] = { 0x62, 0x74, 0x6c, 0x65 };


/* Copies len bytes from src to dst in reverse order. dst and src must not
 * overlap. */
static void
swap_copy(uint8* dst, const uint8* src, size_t len)
{
	for (size_t i = 0; i < len; i++)
		dst[i] = src[len - 1 - i];
}


/* AES-128 of one block, MSB-first in and out. */
static bool
aes_encrypt_msb(const uint8 key[16], const uint8 plain[16], uint8 out[16])
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return false;

	bool ok = false;
	int length = 0;
	int finalLength = 0;
	if (EVP_EncryptInit_ex2(ctx, EVP_aes_128_ecb(), key, NULL, NULL) == 1
		&& EVP_CIPHER_CTX_set_padding(ctx, 0) == 1
		&& EVP_EncryptUpdate(ctx, out, &length, plain, 16) == 1
		&& length == 16
		&& EVP_EncryptFinal_ex(ctx, out + length, &finalLength) == 1
		&& finalLength == 0) {
		ok = true;
	}

	EVP_CIPHER_CTX_free(ctx);
	return ok;
}


bool
smp_aes_cmac(const uint8 key[16], const uint8* msg, size_t len, uint8 out[16])
{
	if (key == NULL || out == NULL || (msg == NULL && len != 0))
		return false;

	EVP_MAC* mac = EVP_MAC_fetch(NULL, "CMAC", NULL);
	if (mac == NULL)
		return false;

	EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
	if (ctx == NULL) {
		EVP_MAC_free(mac);
		return false;
	}

	char cipherName[] = "AES-128-CBC";
	OSSL_PARAM params[2];
	params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_CIPHER,
		cipherName, 0);
	params[1] = OSSL_PARAM_construct_end();

	bool ok = false;
	size_t outLength = 0;
	if (EVP_MAC_init(ctx, key, 16, params) == 1
		&& (len == 0 || EVP_MAC_update(ctx, msg, len) == 1)
		&& EVP_MAC_final(ctx, out, &outLength, 16) == 1
		&& outLength == 16) {
		ok = true;
	}

	EVP_MAC_CTX_free(ctx);
	EVP_MAC_free(mac);
	return ok;
}


bool
smp_e(const uint8 key[16], const uint8 plain[16], uint8 out[16])
{
	if (key == NULL || plain == NULL || out == NULL)
		return false;

	uint8 keyMsb[16];
	uint8 plainMsb[16];
	uint8 outMsb[16];
	swap_copy(keyMsb, key, 16);
	swap_copy(plainMsb, plain, 16);

	bool ok = aes_encrypt_msb(keyMsb, plainMsb, outMsb);
	if (ok)
		swap_copy(out, outMsb, 16);

	OPENSSL_cleanse(keyMsb, sizeof(keyMsb));
	OPENSSL_cleanse(plainMsb, sizeof(plainMsb));
	OPENSSL_cleanse(outMsb, sizeof(outMsb));
	return ok;
}


bool
smp_f4(const uint8 u[32], const uint8 v[32], const uint8 x[16], uint8 z,
	uint8 out[16])
{
	if (u == NULL || v == NULL || x == NULL || out == NULL)
		return false;

	/* M = U || V || Z, MSB-first. */
	uint8 message[65];
	uint8 key[16];
	uint8 result[16];
	swap_copy(message, u, 32);
	swap_copy(message + 32, v, 32);
	message[64] = z;
	swap_copy(key, x, 16);

	bool ok = smp_aes_cmac(key, message, sizeof(message), result);
	if (ok)
		swap_copy(out, result, 16);

	OPENSSL_cleanse(message, sizeof(message));
	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(result, sizeof(result));
	return ok;
}


bool
smp_f5(const uint8 w[32], const uint8 n1[16], const uint8 n2[16],
	const uint8 a1[7], const uint8 a2[7], uint8 macKey[16], uint8 ltk[16])
{
	if (w == NULL || n1 == NULL || n2 == NULL || a1 == NULL || a2 == NULL
		|| macKey == NULL || ltk == NULL) {
		return false;
	}

	uint8 dhKey[32];
	uint8 t[16];
	uint8 result[16];
	swap_copy(dhKey, w, 32);

	/* T = AES-CMAC_SALT(W) */
	bool ok = smp_aes_cmac(kF5Salt, dhKey, sizeof(dhKey), t);

	/* Counter || keyID || N1 || N2 || A1 || A2 || Length, MSB-first. */
	uint8 message[53];
	message[0] = 0;
	memcpy(message + 1, kF5KeyId, 4);
	swap_copy(message + 5, n1, 16);
	swap_copy(message + 21, n2, 16);
	swap_copy(message + 37, a1, 7);
	swap_copy(message + 44, a2, 7);
	message[51] = 0x01;
	message[52] = 0x00;
		/* Length = 256 */

	if (ok)
		ok = smp_aes_cmac(t, message, sizeof(message), result);
	if (ok) {
		swap_copy(macKey, result, 16);
		message[0] = 1;
		ok = smp_aes_cmac(t, message, sizeof(message), result);
	}
	if (ok)
		swap_copy(ltk, result, 16);

	OPENSSL_cleanse(dhKey, sizeof(dhKey));
	OPENSSL_cleanse(t, sizeof(t));
	OPENSSL_cleanse(result, sizeof(result));
	OPENSSL_cleanse(message, sizeof(message));
	return ok;
}


bool
smp_f6(const uint8 w[16], const uint8 n1[16], const uint8 n2[16],
	const uint8 r[16], const uint8 ioCap[3], const uint8 a1[7],
	const uint8 a2[7], uint8 out[16])
{
	if (w == NULL || n1 == NULL || n2 == NULL || r == NULL || ioCap == NULL
		|| a1 == NULL || a2 == NULL || out == NULL) {
		return false;
	}

	/* N1 || N2 || R || IOcap || A1 || A2, MSB-first. */
	uint8 message[65];
	uint8 key[16];
	uint8 result[16];
	swap_copy(message, n1, 16);
	swap_copy(message + 16, n2, 16);
	swap_copy(message + 32, r, 16);
	swap_copy(message + 48, ioCap, 3);
	swap_copy(message + 51, a1, 7);
	swap_copy(message + 58, a2, 7);
	swap_copy(key, w, 16);

	bool ok = smp_aes_cmac(key, message, sizeof(message), result);
	if (ok)
		swap_copy(out, result, 16);

	OPENSSL_cleanse(message, sizeof(message));
	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(result, sizeof(result));
	return ok;
}


bool
smp_g2(const uint8 u[32], const uint8 v[32], const uint8 x[16],
	const uint8 y[16], uint32* value)
{
	if (u == NULL || v == NULL || x == NULL || y == NULL || value == NULL)
		return false;

	/* U || V || Y, MSB-first, keyed with X. */
	uint8 message[80];
	uint8 key[16];
	uint8 result[16];
	swap_copy(message, u, 32);
	swap_copy(message + 32, v, 32);
	swap_copy(message + 64, y, 16);
	swap_copy(key, x, 16);

	bool ok = smp_aes_cmac(key, message, sizeof(message), result);
	if (ok) {
		/* g2 = AES-CMAC mod 2^32, the last four octets MSB-first. */
		uint32 low = ((uint32)result[12] << 24) | ((uint32)result[13] << 16)
			| ((uint32)result[14] << 8) | (uint32)result[15];
		*value = low % 1000000;
	}

	OPENSSL_cleanse(message, sizeof(message));
	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(result, sizeof(result));
	return ok;
}


bool
smp_c1(const uint8 k[16], const uint8 r[16], const uint8 preq[7],
	const uint8 pres[7], uint8 iat, uint8 rat, const uint8 ia[6],
	const uint8 ra[6], uint8 out[16])
{
	if (k == NULL || r == NULL || preq == NULL || pres == NULL || ia == NULL
		|| ra == NULL || out == NULL) {
		return false;
	}

	uint8 key[16];
	uint8 block[16];
	uint8 p1[16];
	uint8 p2[16];
	swap_copy(key, k, 16);
	swap_copy(block, r, 16);

	/* p1 = pres || preq || rat' || iat', MSB-first. */
	swap_copy(p1, pres, 7);
	swap_copy(p1 + 7, preq, 7);
	p1[14] = rat & 0x01;
	p1[15] = iat & 0x01;

	/* p2 = padding || ia || ra, MSB-first. */
	memset(p2, 0, 4);
	swap_copy(p2 + 4, ia, 6);
	swap_copy(p2 + 10, ra, 6);

	/* c1 = e(k, e(k, r XOR p1) XOR p2) */
	for (int i = 0; i < 16; i++)
		block[i] ^= p1[i];
	bool ok = aes_encrypt_msb(key, block, block);
	if (ok) {
		for (int i = 0; i < 16; i++)
			block[i] ^= p2[i];
		ok = aes_encrypt_msb(key, block, block);
	}
	if (ok)
		swap_copy(out, block, 16);

	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(block, sizeof(block));
	OPENSSL_cleanse(p1, sizeof(p1));
	OPENSSL_cleanse(p2, sizeof(p2));
	return ok;
}


bool
smp_s1(const uint8 k[16], const uint8 r1[16], const uint8 r2[16],
	uint8 out[16])
{
	if (k == NULL || r1 == NULL || r2 == NULL || out == NULL)
		return false;

	/* r' = r1' || r2', the least significant 64 bits of each, MSB-first.
	 * LSB-first that is the low half of r2 followed by the low half of
	 * r1. */
	uint8 plain[16];
	memcpy(plain, r2, 8);
	memcpy(plain + 8, r1, 8);

	bool ok = smp_e(k, plain, out);

	OPENSSL_cleanse(plain, sizeof(plain));
	return ok;
}


bool
smp_ah(const uint8 irk[16], const uint8 r[3], uint8 out[3])
{
	if (irk == NULL || r == NULL || out == NULL)
		return false;

	/* r' = padding || r, LSB-first r in the low three octets. */
	uint8 plain[16];
	uint8 result[16];
	memset(plain, 0, sizeof(plain));
	memcpy(plain, r, 3);

	bool ok = smp_e(irk, plain, result);
	if (ok)
		memcpy(out, result, 3);
			/* ah = e(k, r') mod 2^24 */

	OPENSSL_cleanse(plain, sizeof(plain));
	OPENSSL_cleanse(result, sizeof(result));
	return ok;
}


/*	#pragma mark - P-256 ECDH */


/* Writes the public key of pkey as X || Y, each LSB-first. */
static bool
export_public_key(EVP_PKEY* pkey, uint8 publicKey[64])
{
	uint8 encoded[65];
	size_t length = 0;
	if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY,
			encoded, sizeof(encoded), &length) != 1
		|| length != sizeof(encoded) || encoded[0] != 0x04) {
		return false;
	}

	swap_copy(publicKey, encoded + 1, 32);
	swap_copy(publicKey + 32, encoded + 33, 32);
	return true;
}


/* Builds an EVP_PKEY from the uncompressed point 0x04 || X || Y (and the
 * private scalar when privateKey is not NULL). */
static EVP_PKEY*
build_key(const uint8 encodedPublic[65], const BIGNUM* privateKey)
{
	OSSL_PARAM_BLD* builder = OSSL_PARAM_BLD_new();
	if (builder == NULL)
		return NULL;

	EVP_PKEY* pkey = NULL;
	OSSL_PARAM* params = NULL;
	EVP_PKEY_CTX* ctx = NULL;

	if (OSSL_PARAM_BLD_push_utf8_string(builder, OSSL_PKEY_PARAM_GROUP_NAME,
			kCurveName, 0) != 1
		|| OSSL_PARAM_BLD_push_octet_string(builder, OSSL_PKEY_PARAM_PUB_KEY,
			encodedPublic, 65) != 1
		|| (privateKey != NULL
			&& OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_PRIV_KEY,
				privateKey) != 1)) {
		goto out;
	}

	params = OSSL_PARAM_BLD_to_param(builder);
	if (params == NULL)
		goto out;

	ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
	if (ctx == NULL || EVP_PKEY_fromdata_init(ctx) != 1
		|| EVP_PKEY_fromdata(ctx, &pkey,
			privateKey != NULL ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY,
			params) != 1) {
		EVP_PKEY_free(pkey);
		pkey = NULL;
	}

out:
	EVP_PKEY_CTX_free(ctx);
	OSSL_PARAM_free(params);
		/* A secure BIGNUM (BN_secure_new()) is placed in the secure part of
		 * params, which OSSL_PARAM_free() clears before freeing. */
	OSSL_PARAM_BLD_free(builder);
	return pkey;
}


/* Fails unless the public point of pkey is a valid point on P-256 (on the
 * curve, not the point at infinity, in the prime-order group). */
static bool
check_public_key(EVP_PKEY* pkey)
{
	EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, NULL);
	if (ctx == NULL)
		return false;

	bool ok = EVP_PKEY_public_check(ctx) == 1;
	EVP_PKEY_CTX_free(ctx);
	return ok;
}


bool
smp_ecdh_generate(SMPKeyPair* pair)
{
	if (pair == NULL)
		return false;

	memset(pair, 0, sizeof(*pair));

	EVP_PKEY* pkey = EVP_PKEY_Q_keygen(NULL, NULL, "EC", kCurveName);
	if (pkey == NULL)
		return false;

	if (!export_public_key(pkey, pair->publicKey)) {
		EVP_PKEY_free(pkey);
		memset(pair, 0, sizeof(*pair));
		return false;
	}

	pair->privateKey = pkey;
	return true;
}


bool
smp_ecdh_from_private(const uint8 priv[32], SMPKeyPair* pair)
{
	if (priv == NULL || pair == NULL)
		return false;

	memset(pair, 0, sizeof(*pair));

	uint8 privMsb[32];
	uint8 encoded[65];
	swap_copy(privMsb, priv, 32);

	bool ok = false;
	EVP_PKEY* pkey = NULL;
	BN_CTX* bnContext = BN_CTX_new();
	EC_GROUP* group = EC_GROUP_new_by_curve_name_ex(NULL, NULL,
		NID_X9_62_prime256v1);
	EC_POINT* point = group != NULL ? EC_POINT_new(group) : NULL;
	BIGNUM* scalar = BN_secure_new();

	if (bnContext == NULL || group == NULL || point == NULL || scalar == NULL
		|| BN_bin2bn(privMsb, sizeof(privMsb), scalar) == NULL) {
		goto out;
	}

	/* 0 < private key < n */
	if (BN_is_zero(scalar) || BN_cmp(scalar, EC_GROUP_get0_order(group)) >= 0)
		goto out;

	if (EC_POINT_mul(group, point, scalar, NULL, NULL, bnContext) != 1
		|| EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED,
			encoded, sizeof(encoded), bnContext) != sizeof(encoded)) {
		goto out;
	}

	pkey = build_key(encoded, scalar);
	if (pkey == NULL || !export_public_key(pkey, pair->publicKey)) {
		EVP_PKEY_free(pkey);
		pkey = NULL;
		goto out;
	}

	pair->privateKey = pkey;
	ok = true;

out:
	if (!ok)
		memset(pair, 0, sizeof(*pair));
	BN_clear_free(scalar);
	EC_POINT_free(point);
	EC_GROUP_free(group);
	BN_CTX_free(bnContext);
	OPENSSL_cleanse(privMsb, sizeof(privMsb));
	return ok;
}


bool
smp_ecdh_compute(const SMPKeyPair* pair, const uint8 peerPublic[64],
	uint8 dhKey[32])
{
	if (dhKey == NULL)
		return false;

	memset(dhKey, 0, 32);
	if (pair == NULL || pair->privateKey == NULL || peerPublic == NULL)
		return false;

	uint8 encoded[65];
	encoded[0] = 0x04;
	swap_copy(encoded + 1, peerPublic, 32);
	swap_copy(encoded + 33, peerPublic + 32, 32);

	/* Building the key decodes the point; OpenSSL refuses a point that is
	 * not on the curve there. The explicit check below makes sure of it,
	 * and EVP_PKEY_derive_set_peer_ex() validates the peer once more. */
	EVP_PKEY* peer = build_key(encoded, NULL);
	if (peer == NULL)
		return false;
	if (!check_public_key(peer)) {
		EVP_PKEY_free(peer);
		return false;
	}

	bool ok = false;
	uint8 secret[32];
	size_t secretLength = sizeof(secret);
	EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_pkey(NULL,
		(EVP_PKEY*)pair->privateKey, NULL);
	if (ctx != NULL && EVP_PKEY_derive_init(ctx) == 1
		&& EVP_PKEY_derive_set_peer_ex(ctx, peer, 1) == 1
		&& EVP_PKEY_derive(ctx, secret, &secretLength) == 1
		&& secretLength == sizeof(secret)) {
		/* The shared secret is the X coordinate, MSB-first. */
		swap_copy(dhKey, secret, 32);
		ok = true;
	}

	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(peer);
	OPENSSL_cleanse(secret, sizeof(secret));
	return ok;
}


void
smp_ecdh_free(SMPKeyPair* pair)
{
	if (pair == NULL)
		return;

	EVP_PKEY_free((EVP_PKEY*)pair->privateKey);
	OPENSSL_cleanse(pair, sizeof(*pair));
}
