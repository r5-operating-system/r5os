/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Checks the LE Security Manager toolbox (SMPCrypto.cpp) against the sample
 * data of Core 6.0, Vol 3, Part H, Appendix D, and the P-256 ECDH against
 * the debug key pair (Vol 3, Part H, 2.3.5.6.1) and the P-256 sample data
 * (Vol 2, Part G, 7.1.2). Never prints private keys.
 */


#include <stdio.h>
#include <string.h>

#include <SupportDefs.h>

#include "SMPCrypto.h"


static int sFailures = 0;


static void
report(const char* name, bool ok)
{
	printf("%-44s %s\n", name, ok ? "ok" : "FAILED");
	if (!ok)
		sFailures++;
}


static int
hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


/* Parses hex as printed in the spec (MSB-first, spaces ignored) into out,
 * which must hold exactly length bytes. */
static void
parse_msb(const char* hex, uint8* out, size_t length)
{
	size_t count = 0;
	int high = -1;
	for (; *hex != '\0'; hex++) {
		int value = hex_value(*hex);
		if (value < 0)
			continue;
		if (high < 0) {
			high = value;
			continue;
		}
		if (count < length)
			out[count] = (uint8)((high << 4) | value);
		count++;
		high = -1;
	}
	if (count != length || high >= 0) {
		fprintf(stderr, "bt_smp_crypto_test: bad test vector length\n");
		sFailures++;
	}
}


/* Parses hex as printed in the spec into LSB-first order, as the SMP
 * functions take it. */
static void
parse_lsb(const char* hex, uint8* out, size_t length)
{
	uint8 msb[64];
	parse_msb(hex, msb, length);
	for (size_t i = 0; i < length; i++)
		out[i] = msb[length - 1 - i];
}


/*	#pragma mark - D.1 AES-CMAC */


static void
test_aes_cmac()
{
	uint8 key[16];
	uint8 message[64];
	uint8 expected[16];
	uint8 out[16];
	parse_msb("2b7e1516 28aed2a6 abf71588 09cf4f3c", key, 16);
	parse_msb("6bc1bee2 2e409f96 e93d7e11 7393172a"
		"ae2d8a57 1e03ac9c 9eb76fac 45af8e51"
		"30c81c46 a35ce411 e5fbc119 1a0a52ef"
		"f69f2445 df4f9b17 ad2b417b e66c3710", message, 64);

	static const struct {
		const char*	name;
		size_t		length;
		const char*	mac;
	} kCases[] = {
		{ "D.1 AES-CMAC, empty message", 0,
			"bb1d6929 e9593728 7fa37d12 9b756746" },
		{ "D.1 AES-CMAC, 16 bytes", 16,
			"070a16b4 6b4d4144 f79bdd9d d04a287c" },
		{ "D.1 AES-CMAC, 40 bytes", 40,
			"dfa66747 de9ae630 30ca3261 1497c827" },
		{ "D.1 AES-CMAC, 64 bytes", 64,
			"51f0bebf 7e3b9d92 fc497417 79363cfe" },
	};

	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
		parse_msb(kCases[i].mac, expected, 16);
		bool ok = smp_aes_cmac(key, kCases[i].length == 0 ? NULL : message,
			kCases[i].length, out);
		report(kCases[i].name, ok && memcmp(out, expected, 16) == 0);
	}
}


/*	#pragma mark - Secure Connections functions */


static const char* kU = "20b003d2 f297be2c 5e2c83a7 e9f9a5b9"
	"eff49111 acf4fddb cc030148 0e359de6";
static const char* kV = "55188b3d 32f6bb9a 900afcfb eed4e72a"
	"59cb9ac2 f19d7cfb 6b4fdd49 f47fc5fd";
static const char* kN1 = "d5cb8454 d177733e ffffb2ec 712baeab";
static const char* kN2 = "a6e8e7cc 25a75f6e 216583f7 ff3dc4cf";
static const char* kDHKey = "ec0234a3 57c8ad05 341010a6 0a397d9b"
	"99796b13 b4f866f1 868d34f3 73bfa698";
static const char* kA1 = "00 561237 37bfce";
static const char* kA2 = "00 a71370 2dcfc1";
static const char* kMacKey = "2965f176 a1084a02 fd3f6a20 ce636e20";


static void
test_f4()
{
	uint8 u[32], v[32], x[16], expected[16], out[16];
	parse_lsb(kU, u, 32);
	parse_lsb(kV, v, 32);
	parse_lsb(kN1, x, 16);
	parse_lsb("f2c916f1 07a9bd1c f1eda1be a974872d", expected, 16);

	bool ok = smp_f4(u, v, x, 0x00, out);
	report("D.2 f4", ok && memcmp(out, expected, 16) == 0);
}


static void
test_f5()
{
	uint8 w[32], n1[16], n2[16], a1[7], a2[7];
	uint8 expectedMacKey[16], expectedLtk[16], macKey[16], ltk[16];
	parse_lsb(kDHKey, w, 32);
	parse_lsb(kN1, n1, 16);
	parse_lsb(kN2, n2, 16);
	parse_lsb(kA1, a1, 7);
	parse_lsb(kA2, a2, 7);
	parse_lsb(kMacKey, expectedMacKey, 16);
	parse_lsb("69867911 69d7cd23 980522b5 94750a38", expectedLtk, 16);

	bool ok = smp_f5(w, n1, n2, a1, a2, macKey, ltk);
	report("D.3 f5 MacKey", ok && memcmp(macKey, expectedMacKey, 16) == 0);
	report("D.3 f5 LTK", ok && memcmp(ltk, expectedLtk, 16) == 0);
}


static void
test_f6()
{
	uint8 w[16], n1[16], n2[16], r[16], ioCap[3], a1[7], a2[7];
	uint8 expected[16], out[16];
	parse_lsb(kMacKey, w, 16);
	parse_lsb(kN1, n1, 16);
	parse_lsb(kN2, n2, 16);
	parse_lsb("12a3343b b453bb54 08da42d2 0c2d0fc8", r, 16);
	parse_lsb("010102", ioCap, 3);
	parse_lsb(kA1, a1, 7);
	parse_lsb(kA2, a2, 7);
	parse_lsb("e3c47398 9cd0e8c5 d26c0b09 da958f61", expected, 16);

	bool ok = smp_f6(w, n1, n2, r, ioCap, a1, a2, out);
	report("D.4 f6", ok && memcmp(out, expected, 16) == 0);
}


static void
test_g2()
{
	uint8 u[32], v[32], x[16], y[16];
	parse_lsb(kU, u, 32);
	parse_lsb(kV, v, 32);
	parse_lsb(kN1, x, 16);
	parse_lsb(kN2, y, 16);

	/* AES-CMAC = 1536d18d e3d20df9 9b7044c1 2f9ed5ba,
	 * 0x2f9ed5ba mod 1000000 = 938554 */
	uint32 value = 0;
	bool ok = smp_g2(u, v, x, y, &value);
	report("D.5 g2", ok && value == 938554);
}


/*	#pragma mark - D.6 ah, D.7 c1, D.8 s1 */


static void
test_ah()
{
	uint8 irk[16], r[3], expected[3], out[3];
	parse_lsb("ec0234a3 57c8ad05 341010a6 0a397d9b", irk, 16);
	parse_lsb("708194", r, 3);
	parse_lsb("0dfbaa", expected, 3);

	bool ok = smp_ah(irk, r, out);
	report("D.6 ah", ok && memcmp(out, expected, 3) == 0);
}


static void
test_c1()
{
	uint8 k[16], r[16], preq[7], pres[7], ia[6], ra[6];
	uint8 expected[16], out[16];
	memset(k, 0, sizeof(k));
	parse_lsb("5783d521 56ad6f0e 6388274e c6702ee0", r, 16);
	parse_lsb("07071000000101", preq, 7);
	parse_lsb("05000800000302", pres, 7);
	parse_lsb("a1a2a3a4a5a6", ia, 6);
	parse_lsb("b1b2b3b4b5b6", ra, 6);
	parse_lsb("1e1e3fef 878988ea d2a74dc5 bef13b86", expected, 16);

	/* The Pairing Request PDU starts with its opcode 0x01. */
	bool ok = preq[0] == 0x01 && pres[0] == 0x02
		&& smp_c1(k, r, preq, pres, 0x01, 0x00, ia, ra, out);
	report("D.7 c1", ok && memcmp(out, expected, 16) == 0);
}


static void
test_s1()
{
	uint8 k[16], r1[16], r2[16], expected[16], out[16];
	memset(k, 0, sizeof(k));
	parse_lsb("000f0e0d 0c0b0a09 11223344 55667788", r1, 16);
	parse_lsb("01020304 05060708 99aabbcc ddeeff00", r2, 16);
	parse_lsb("9a1fe1f0 e8b0f49b 5b4216ae 796da062", expected, 16);

	bool ok = smp_s1(k, r1, r2, out);
	report("D.8 s1", ok && memcmp(out, expected, 16) == 0);
}


static void
test_e()
{
	/* e is the AES block of s1: e(0, 112233445566778899aabbccddeeff00). */
	uint8 k[16], plain[16], expected[16], out[16];
	memset(k, 0, sizeof(k));
	parse_lsb("11223344 55667788 99aabbcc ddeeff00", plain, 16);
	parse_lsb("9a1fe1f0 e8b0f49b 5b4216ae 796da062", expected, 16);

	bool ok = smp_e(k, plain, out);
	report("e (LSB-first, from D.8)", ok && memcmp(out, expected, 16) == 0);
}


/*	#pragma mark - P-256 ECDH */


static void
test_ecdh()
{
	uint8 privA[32], privB[32], publicA[64], publicB[64], dhKey[32];
	uint8 expectedDHKey[32], otherDHKey[32];
	SMPKeyPair a, b;

	/* Debug key, Vol 3, Part H, 2.3.5.6.1; also Private A of the P-256
	 * sample data. */
	parse_lsb("3f49f6d4 a3c55f38 74c9b3e3 d2103f50"
		"4aff607b eb40b799 5899b8a6 cd3c1abd", privA, 32);
	parse_lsb(kU, publicA, 32);
	parse_lsb("dc809c49 652aeb6d 63329abf 5a52155c"
		"766345c2 8fed3024 741c8ed0 1589d28b", publicA + 32, 32);

	/* Private B of the P-256 sample data, Vol 2, Part G, 7.1.2. */
	parse_lsb(kV, privB, 32);
	parse_lsb("1ea1f0f0 1faf1d96 09592284 f19e4c00"
		"47b58afd 8615a69f 559077b2 2faaa190", publicB, 32);
	parse_lsb("4c55f33e 429dad37 7356703a 9ab85160"
		"472d1130 e28e3676 5f89aff9 15b1214a", publicB + 32, 32);
	parse_lsb(kDHKey, expectedDHKey, 32);

	bool ok = smp_ecdh_from_private(privA, &a);
	report("ECDH debug key, public key",
		ok && memcmp(a.publicKey, publicA, 64) == 0);

	bool okB = smp_ecdh_from_private(privB, &b);
	report("ECDH sample key B, public key",
		okB && memcmp(b.publicKey, publicB, 64) == 0);

	bool okDH = ok && okB && smp_ecdh_compute(&a, publicB, dhKey)
		&& memcmp(dhKey, expectedDHKey, 32) == 0;
	report("ECDH sample DHKey, A with public B", okDH);

	okDH = ok && okB && smp_ecdh_compute(&b, publicA, dhKey)
		&& memcmp(dhKey, expectedDHKey, 32) == 0;
	report("ECDH sample DHKey, B with public A", okDH);

	/* A point not on the curve: the debug public key with one bit of Y
	 * flipped. */
	uint8 badPoint[64];
	memcpy(badPoint, publicA, 64);
	badPoint[32] ^= 0x01;
	bool rejected = ok && !smp_ecdh_compute(&b, badPoint, dhKey);
	for (int i = 0; i < 32; i++) {
		if (dhKey[i] != 0)
			rejected = false;
	}
	report("ECDH rejects a point not on the curve", rejected);

	/* X = Y = 0 is not on P-256 either (and stands in for infinity). */
	memset(badPoint, 0, sizeof(badPoint));
	report("ECDH rejects the zero point",
		ok && !smp_ecdh_compute(&b, badPoint, dhKey));

	/* A private key of 0 is refused. */
	SMPKeyPair zero;
	uint8 zeroKey[32];
	memset(zeroKey, 0, sizeof(zeroKey));
	report("ECDH refuses private key 0", !smp_ecdh_from_private(zeroKey, &zero));

	smp_ecdh_free(&a);
	smp_ecdh_free(&b);
	memset(privA, 0, sizeof(privA));
	memset(privB, 0, sizeof(privB));

	/* Two fresh pairs agree on the DHKey. */
	SMPKeyPair c, d;
	bool okC = smp_ecdh_generate(&c);
	bool okD = smp_ecdh_generate(&d);
	report("ECDH generate two pairs", okC && okD
		&& memcmp(c.publicKey, d.publicKey, 64) != 0);

	bool same = okC && okD && smp_ecdh_compute(&c, d.publicKey, dhKey)
		&& smp_ecdh_compute(&d, c.publicKey, otherDHKey)
		&& memcmp(dhKey, otherDHKey, 32) == 0;
	report("ECDH fresh pairs, same DHKey both sides", same);

	smp_ecdh_free(&c);
	smp_ecdh_free(&d);
	memset(dhKey, 0, sizeof(dhKey));
	memset(otherDHKey, 0, sizeof(otherDHKey));
}


int
main()
{
	test_aes_cmac();
	test_f4();
	test_f5();
	test_f6();
	test_g2();
	test_ah();
	test_c1();
	test_s1();
	test_e();
	test_ecdh();

	if (sFailures != 0) {
		printf("bt_smp_crypto_test: FAILED (%d)\n", sFailures);
		return 1;
	}

	printf("bt_smp_crypto_test: ok\n");
	return 0;
}
