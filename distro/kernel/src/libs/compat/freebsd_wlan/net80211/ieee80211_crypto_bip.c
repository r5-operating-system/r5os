/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * AES-CMAC-128 for management-frame integrity (BIP). The MIC is the
 * first 8 bytes. The construction follows NIST SP 800-38B and IEEE
 * 802.11: the additional data is the frame control (retry, power and
 * more-data cleared, and the protected bit cleared) and the three
 * addresses, then the body, then the MMIE with its MIC set to zero.
 */

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

#include <crypto/rijndael/rijndael.h>

#define BIP_MMIE_ID 76
#define BIP_MMIE_LEN 16
#define BIP_MMIE_SIZE 18
#define BIP_MIC_LEN 8
#define BIP_HDR_LEN 24
#define BIP_MAX 1600

struct bip_ctx {
	struct ieee80211vap *vap;
	rijndael_ctx aes;
};

static void *bip_attach(struct ieee80211vap *, struct ieee80211_key *);
static void bip_detach(struct ieee80211_key *);
static int bip_setkey(struct ieee80211_key *);
static void bip_setiv(struct ieee80211_key *, uint8_t *);
static int bip_encap(struct ieee80211_key *, struct mbuf *);
static int bip_decap(struct ieee80211_key *, struct mbuf *, int);
static int bip_enmic(struct ieee80211_key *, struct mbuf *, int);
static int bip_demic(struct ieee80211_key *, struct mbuf *, int);

static const struct ieee80211_cipher bip = {
	.ic_name	= "AES-CMAC",
	.ic_cipher	= IEEE80211_CIPHER_BIP_CMAC_128,
	.ic_header	= 0,
	.ic_trailer	= BIP_MMIE_SIZE,
	.ic_miclen	= 0,
	.ic_attach	= bip_attach,
	.ic_detach	= bip_detach,
	.ic_setkey	= bip_setkey,
	.ic_setiv	= bip_setiv,
	.ic_encap	= bip_encap,
	.ic_decap	= bip_decap,
	.ic_enmic	= bip_enmic,
	.ic_demic	= bip_demic,
};

static int nrefs = 0;


static void *
bip_attach(struct ieee80211vap *vap, struct ieee80211_key *k)
{
	struct bip_ctx *ctx;

	ctx = (struct bip_ctx *)IEEE80211_MALLOC(sizeof(*ctx),
	    M_80211_CRYPTO, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (ctx == NULL) {
		vap->iv_stats.is_crypto_nomem++;
		return NULL;
	}
	ctx->vap = vap;
	nrefs++;
	return ctx;
}


static void
bip_detach(struct ieee80211_key *k)
{
	struct bip_ctx *ctx = k->wk_private;

	IEEE80211_FREE(ctx, M_80211_CRYPTO);
	nrefs--;
}


static int
bip_setkey(struct ieee80211_key *k)
{
	struct bip_ctx *ctx = k->wk_private;

	if (k->wk_keylen != 16)
		return 0;
	rijndael_set_key(&ctx->aes, k->wk_key, 128);
	return 1;
}


static void
bip_setiv(struct ieee80211_key *k, uint8_t *iv)
{
}


static void
cmac_dbl(uint8_t block[16])
{
	int i;
	uint8_t overflow;

	overflow = (block[0] & 0x80) != 0;
	for (i = 0; i < 15; i++)
		block[i] = (uint8_t)((block[i] << 1) | (block[i + 1] >> 7));
	block[15] = (uint8_t)(block[15] << 1);
	if (overflow)
		block[15] ^= 0x87;
}


static void
bip_mic(const rijndael_ctx *aes, const uint8_t *data, int len, uint8_t mic[8])
{
	uint8_t zero[16], l[16], k1[16], k2[16], x[16], block[16], out[16];
	int n, i, off, tail, complete;

	memset(zero, 0, sizeof(zero));
	rijndael_encrypt(aes, zero, l);
	memcpy(k1, l, 16);
	cmac_dbl(k1);
	memcpy(k2, k1, 16);
	cmac_dbl(k2);

	n = (len + 15) / 16;
	if (n == 0)
		n = 1;
	tail = len % 16;
	complete = len != 0 && tail == 0;
	memset(x, 0, sizeof(x));
	for (i = 0; i < n - 1; i++) {
		for (off = 0; off < 16; off++)
			x[off] ^= data[i * 16 + off];
		rijndael_encrypt(aes, x, out);
		memcpy(x, out, 16);
	}
	memset(block, 0, sizeof(block));
	if (complete) {
		memcpy(block, data + (n - 1) * 16, 16);
		for (off = 0; off < 16; off++)
			block[off] ^= k1[off];
	} else {
		if (len != 0)
			memcpy(block, data + (n - 1) * 16, tail);
		block[tail] = 0x80;
		for (off = 0; off < 16; off++)
			block[off] ^= k2[off];
	}
	for (off = 0; off < 16; off++)
		x[off] ^= block[off];
	rijndael_encrypt(aes, x, out);
	memcpy(mic, out, BIP_MIC_LEN);
	memset(out, 0, sizeof(out));
	memset(l, 0, sizeof(l));
	memset(k1, 0, sizeof(k1));
	memset(k2, 0, sizeof(k2));
}


static int
bip_pull(struct mbuf *m, uint8_t *buf, int cap, int *out)
{
	int n = 0;

	for (; m != NULL; m = m->m_next) {
		if (m->m_len < 0 || n > cap || m->m_len > cap - n)
			return 0;
		memcpy(buf + n, mtod(m, uint8_t *), m->m_len);
		n += m->m_len;
	}
	*out = n;
	return 1;
}


static int
bip_grow(struct mbuf *m, const uint8_t *data, int len)
{
	struct mbuf *last;

	for (last = m; last->m_next != NULL; last = last->m_next)
		;
	if (M_TRAILINGSPACE(last) < len)
		return 0;
	memcpy(mtod(last, uint8_t *) + last->m_len, data, len);
	last->m_len += len;
	if ((m->m_flags & M_PKTHDR) != 0)
		m->m_pkthdr.len += len;
	return 1;
}


static int
bip_same(const uint8_t *a, const uint8_t *b, int n)
{
	uint8_t diff = 0;
	int i;

	for (i = 0; i < n; i++)
		diff |= a[i] ^ b[i];
	return diff == 0;
}


static void
bip_aad(const uint8_t *frame, uint8_t aad[20])
{
	aad[0] = frame[0];
	aad[1] = (uint8_t)(frame[1] & ~(IEEE80211_FC1_RETRY
	    | IEEE80211_FC1_PWR_MGT | IEEE80211_FC1_MORE_DATA
	    | IEEE80211_FC1_PROTECTED));
	memcpy(aad + 2, frame + 4, IEEE80211_ADDR_LEN * 3);
}


static int
bip_protect(struct ieee80211_key *k, struct mbuf *m)
{
	struct bip_ctx *ctx = k->wk_private;
	uint8_t frame[BIP_MAX];
	uint8_t input[BIP_MAX];
	uint8_t mmie[BIP_MMIE_SIZE];
	uint8_t mic[BIP_MIC_LEN];
	uint64_t ipn;
	int flen, body, i;

	if (!bip_pull(m, frame, BIP_MAX, &flen))
		return 0;
	if (flen < BIP_HDR_LEN || (frame[0] & IEEE80211_FC0_TYPE_MASK)
	    != IEEE80211_FC0_TYPE_MGT)
		return 0;
	if (flen >= BIP_MMIE_SIZE
	    && frame[flen - BIP_MMIE_SIZE] == BIP_MMIE_ID
	    && frame[flen - BIP_MMIE_SIZE + 1] == BIP_MMIE_LEN)
		return 1;
	if (k->wk_keytsc == 0xffffffffffffULL)
		return 0;
	ipn = ++k->wk_keytsc;

	memset(mmie, 0, sizeof(mmie));
	mmie[0] = BIP_MMIE_ID;
	mmie[1] = BIP_MMIE_LEN;
	mmie[2] = (uint8_t)k->wk_keyix;
	mmie[3] = (uint8_t)(k->wk_keyix >> 8);
	for (i = 0; i < 6; i++)
		mmie[4 + i] = (uint8_t)(ipn >> (8 * i));

	body = flen - BIP_HDR_LEN;
	if (20 + body + BIP_MMIE_SIZE > BIP_MAX)
		return 0;
	bip_aad(frame, input);
	memcpy(input + 20, frame + BIP_HDR_LEN, body);
	memcpy(input + 20 + body, mmie, BIP_MMIE_SIZE);
	bip_mic(&ctx->aes, input, 20 + body + BIP_MMIE_SIZE, mic);
	memcpy(mmie + 10, mic, BIP_MIC_LEN);
	memset(input, 0, sizeof(input));
	memset(mic, 0, sizeof(mic));
	return bip_grow(m, mmie, BIP_MMIE_SIZE);
}


static int
bip_check(struct ieee80211_key *k, struct mbuf *m)
{
	struct bip_ctx *ctx = k->wk_private;
	uint8_t frame[BIP_MAX];
	uint8_t input[BIP_MAX];
	uint8_t mic[BIP_MIC_LEN];
	uint8_t found[BIP_MIC_LEN];
	uint64_t ipn;
	int flen, body, i, at;

	if (!bip_pull(m, frame, BIP_MAX, &flen))
		return 0;
	if (flen < BIP_HDR_LEN + BIP_MMIE_SIZE)
		return 0;
	at = flen - BIP_MMIE_SIZE;
	if (frame[at] != BIP_MMIE_ID || frame[at + 1] != BIP_MMIE_LEN)
		return 0;
	if (frame[at + 2] != (uint8_t)k->wk_keyix
	    || frame[at + 3] != (uint8_t)(k->wk_keyix >> 8))
		return 0;

	ipn = 0;
	for (i = 0; i < 6; i++)
		ipn |= (uint64_t)frame[at + 4 + i] << (8 * i);
	if (ipn == 0)
		return 0;
	if ((k->wk_flags & IEEE80211_KEY_NOREPLAY) == 0
	    && ipn <= k->wk_keyrsc[0])
		return 0;

	memcpy(found, frame + at + 10, BIP_MIC_LEN);
	memset(frame + at + 10, 0, BIP_MIC_LEN);
	body = at - BIP_HDR_LEN;
	if (20 + body + BIP_MMIE_SIZE > BIP_MAX)
		return 0;
	bip_aad(frame, input);
	memcpy(input + 20, frame + BIP_HDR_LEN, body);
	memcpy(input + 20 + body, frame + at, BIP_MMIE_SIZE);
	bip_mic(&ctx->aes, input, 20 + body + BIP_MMIE_SIZE, mic);
	memset(input, 0, sizeof(input));
	if (!bip_same(mic, found, BIP_MIC_LEN)) {
		memset(mic, 0, sizeof(mic));
		return 0;
	}
	memset(mic, 0, sizeof(mic));
	m_adj(m, -BIP_MMIE_SIZE);
	k->wk_keyrsc[0] = ipn;
	return 1;
}


static int
bip_encap(struct ieee80211_key *k, struct mbuf *m)
{
	return bip_protect(k, m);
}


static int
bip_enmic(struct ieee80211_key *k, struct mbuf *m, int force)
{
	return bip_protect(k, m);
}


static int
bip_decap(struct ieee80211_key *k, struct mbuf *m, int hdrlen)
{
	return bip_check(k, m);
}


static int
bip_demic(struct ieee80211_key *k, struct mbuf *m, int force)
{
	return bip_check(k, m);
}


IEEE80211_CRYPTO_MODULE(bip, 1);
