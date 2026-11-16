/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth LE Security Manager cryptographic toolbox
 * (Core 6.0, Vol 3, Part H, 2.2) and the P-256 ECDH used by LE Secure
 * Connections pairing (Vol 3, Part H, 2.3.5.6).
 *
 * Byte order
 * ----------
 * Except for smp_aes_cmac(), every multi-octet value in this API is
 * LSB-first: byte 0 is the least significant octet. That is the order in
 * which SMP PDUs and HCI carry these values, so a caller passes PDU or HCI
 * bytes as they are. The spec writes the same values MSB-first; the
 * functions reverse internally.
 *
 * Bluetooth device addresses (ia, ra, and the address part of a1/a2) are
 * 6 bytes LSB-first, as HCI events carry them (bdaddr_t order).
 *
 * All functions return false when OpenSSL fails or an argument is invalid;
 * the outputs are then undefined. Nothing here prints or logs key material.
 */
#ifndef _R5_SMP_CRYPTO_H
#define _R5_SMP_CRYPTO_H


#include <stddef.h>

#include <SupportDefs.h>


/* AES-CMAC (RFC 4493). key, msg and out are MSB-first, as in RFC 4493.
 * msg may be NULL when len is 0. */
bool smp_aes_cmac(const uint8 key[16], const uint8* msg, size_t len,
	uint8 out[16]);

/* Security function e: AES-128 of one block. key, plain and out are
 * LSB-first. */
bool smp_e(const uint8 key[16], const uint8 plain[16], uint8 out[16]);

/* f4 (LE Secure Connections confirm value).
 * u, v: public key X coordinates, 32 bytes LSB-first (Pairing Public Key
 * PDU order). x: the 128-bit nonce, LSB-first. z: 0 for numeric comparison
 * and Just Works, 0x80 | bit for passkey entry. out: LSB-first, the value of
 * the Pairing Confirm PDU. */
bool smp_f4(const uint8 u[32], const uint8 v[32], const uint8 x[16], uint8 z,
	uint8 out[16]);

/* f5 (MacKey and LTK).
 * w: DHKey, 32 bytes LSB-first (as smp_ecdh_compute() returns it).
 * n1, n2: nonces, LSB-first.
 * a1, a2: the 56-bit address values of the spec, LSB-first: bytes 0..5 are
 * the device address LSB-first (HCI order), byte 6 is the address type
 * (0x00 public, 0x01 random). The spec writes this value MSB-first with the
 * type as the most significant octet.
 * macKey, ltk: LSB-first. */
bool smp_f5(const uint8 w[32], const uint8 n1[16], const uint8 n2[16],
	const uint8 a1[7], const uint8 a2[7], uint8 macKey[16], uint8 ltk[16]);

/* f6 (DHKey check).
 * w: MacKey from smp_f5(), LSB-first. n1, n2, r: LSB-first.
 * ioCap: the 24-bit IOcap value LSB-first, which is the order of bytes 1..3
 * of the Pairing Request/Response PDU: ioCap[0] IO Capability, ioCap[1] OOB
 * data flag, ioCap[2] AuthReq.
 * a1, a2: as for smp_f5(). out: LSB-first, the DHKey Check PDU value. */
bool smp_f6(const uint8 w[16], const uint8 n1[16], const uint8 n2[16],
	const uint8 r[16], const uint8 ioCap[3], const uint8 a1[7],
	const uint8 a2[7], uint8 out[16]);

/* g2 (numeric comparison value).
 * u, v: public key X coordinates, 32 bytes LSB-first. x, y: nonces,
 * LSB-first. value: the six-digit number to show, already mod 1000000. */
bool smp_g2(const uint8 u[32], const uint8 v[32], const uint8 x[16],
	const uint8 y[16], uint32* value);

/* c1 (LE legacy pairing confirm value).
 * k: TK, LSB-first. r: random value, LSB-first.
 * preq, pres: the 7 bytes of the Pairing Request and Pairing Response PDU
 * in PDU order, opcode first (that is the LSB-first form of the spec value).
 * iat, rat: initiator and responder address type, 0 public, 1 random.
 * ia, ra: initiator and responder address, 6 bytes LSB-first.
 * out: LSB-first, the Pairing Confirm PDU value. */
bool smp_c1(const uint8 k[16], const uint8 r[16], const uint8 preq[7],
	const uint8 pres[7], uint8 iat, uint8 rat, const uint8 ia[6],
	const uint8 ra[6], uint8 out[16]);

/* s1 (LE legacy STK). k: TK, r1: Srand (responder), r2: Mrand
 * (initiator), all LSB-first. out: STK, LSB-first. */
bool smp_s1(const uint8 k[16], const uint8 r1[16], const uint8 r2[16],
	uint8 out[16]);

/* ah (resolvable private address hash). irk: LSB-first. r: prand, 3 bytes
 * LSB-first (bytes 3..5 of the RPA in HCI order). out: hash, 3 bytes
 * LSB-first (bytes 0..2 of the RPA in HCI order). */
bool smp_ah(const uint8 irk[16], const uint8 r[3], uint8 out[3]);


/* P-256 key pair for LE Secure Connections. */
struct SMPKeyPair {
	void*	privateKey;
		/* EVP_PKEY*, owned; NULL when empty. */
	uint8	publicKey[64];
		/* X || Y, each 32 bytes LSB-first (Pairing Public Key PDU). */
};

/* Generates a fresh key pair. Free it with smp_ecdh_free(). */
bool smp_ecdh_generate(SMPKeyPair* pair);

/* Builds a key pair from a private key, 32 bytes LSB-first. Meant for the
 * spec's debug key and for tests. Rejects 0 and values >= the curve order.
 * Free it with smp_ecdh_free(). */
bool smp_ecdh_from_private(const uint8 priv[32], SMPKeyPair* pair);

/* Computes the DHKey (X coordinate of the shared point, 32 bytes LSB-first)
 * from our pair and the peer's public key (64 bytes, Pairing Public Key PDU
 * format). Returns false, and leaves dhKey zeroed, when the peer point is
 * not on P-256. */
bool smp_ecdh_compute(const SMPKeyPair* pair, const uint8 peerPublic[64],
	uint8 dhKey[32]);

/* Frees the private key and wipes the pair. Safe on an empty pair. */
void smp_ecdh_free(SMPKeyPair* pair);


#endif	/* _R5_SMP_CRYPTO_H */
