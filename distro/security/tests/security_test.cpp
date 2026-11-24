/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Unit tests for libr5security.so (BSecurityKit):
 * - RFC 9106 Section 5.3 Argon2id known-answer test vector
 * - draft-irtf-cfrg-xchacha-03 Section A.3.1 XChaCha20-Poly1305 AEAD vector
 * - BPasswordHash PHC encode, verify, NeedsRehash, malformed inputs, and timing
 * - BSealedData round-trip, tampering (every byte), wrong key/AAD, file sealing
 * - BSecureBuffer wipe and constant-time equality
 * - BRandom entropy output
 */


#include "R5Security.h"
#include "monocypher/monocypher.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>


using namespace BSecurityKit;


static int sFailures = 0;


#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		sFailures++; \
	} \
} while (0)


static void
test_argon2id_rfc9106_vector()
{
	// RFC 9106 Section 5.3: Argon2id test vector
	// t=3, m=32 KiB, p=4, tagLen=32
	uint8 password[32];
	uint8 salt[16];
	uint8 secretKey[8];
	uint8 ad[12];
	memset(password, 0x01, sizeof(password));
	memset(salt, 0x02, sizeof(salt));
	memset(secretKey, 0x03, sizeof(secretKey));
	memset(ad, 0x04, sizeof(ad));

	static const uint8 kExpected[32] = {
		0x0d, 0x64, 0x0d, 0xf5, 0x8d, 0x78, 0x76, 0x6c,
		0x08, 0xc0, 0x37, 0xa3, 0x4a, 0x8b, 0x53, 0xc9,
		0xd0, 0x1e, 0xf0, 0x45, 0x2d, 0x75, 0xb6, 0x5e,
		0xb5, 0x25, 0x20, 0xe9, 0x6b, 0x01, 0xe6, 0x59
	};

	BPasswordHash::Parameters params;
	params.memoryKiB = 32;
	params.passes = 3;
	params.lanes = 4;

	BSecureBuffer derived;
	status_t status = BPasswordHash::DeriveKey(password, sizeof(password),
		salt, sizeof(salt), secretKey, sizeof(secretKey), ad, sizeof(ad),
		params, derived, sizeof(kExpected));
	CHECK(status == B_OK, "RFC 9106 Argon2id DeriveKey returned B_OK");
	CHECK(derived.Size() == sizeof(kExpected), "RFC 9106 output size 32");
	CHECK(derived.Data() != NULL
		&& memcmp(derived.Data(), kExpected, sizeof(kExpected)) == 0,
		"RFC 9106 Argon2id tag matches specification vector");
}


static void
test_xchacha20_poly1305_vector()
{
	// draft-irtf-cfrg-xchacha-03 Section A.3.1
	static const uint8 kKey[32] = {
		0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
		0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
		0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
		0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
	};
	static const uint8 kNonce[24] = {
		0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
		0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
		0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57
	};
	static const uint8 kAad[12] = {
		0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3,
		0xc4, 0xc5, 0xc6, 0xc7
	};
	static const char kPlain[] =
		"Ladies and Gentlemen of the class of '99: "
		"If I could offer you only one tip for the future, "
		"sunscreen would be it.";
	const size_t kPlainLen = sizeof(kPlain) - 1;

	static const uint8 kExpectedCipher[114] = {
		0xbd, 0x6d, 0x17, 0x9d, 0x3e, 0x83, 0xd4, 0x3b,
		0x95, 0x76, 0x57, 0x94, 0x93, 0xc0, 0xe9, 0x39,
		0x57, 0x2a, 0x17, 0x00, 0x25, 0x2b, 0xfa, 0xcc,
		0xbe, 0xd2, 0x90, 0x2c, 0x21, 0x39, 0x6c, 0xbb,
		0x73, 0x1c, 0x7f, 0x1b, 0x0b, 0x4a, 0xa6, 0x44,
		0x0b, 0xf3, 0xa8, 0x2f, 0x4e, 0xda, 0x7e, 0x39,
		0xae, 0x64, 0xc6, 0x70, 0x8c, 0x54, 0xc2, 0x16,
		0xcb, 0x96, 0xb7, 0x2e, 0x12, 0x13, 0xb4, 0x52,
		0x2f, 0x8c, 0x9b, 0xa4, 0x0d, 0xb5, 0xd9, 0x45,
		0xb1, 0x1b, 0x69, 0xb9, 0x82, 0xc1, 0xbb, 0x9e,
		0x3f, 0x3f, 0xac, 0x2b, 0xc3, 0x69, 0x48, 0x8f,
		0x76, 0xb2, 0x38, 0x35, 0x65, 0xd3, 0xff, 0xf9,
		0x21, 0xf9, 0x66, 0x4c, 0x97, 0x63, 0x7d, 0xa9,
		0x76, 0x88, 0x12, 0xf6, 0x15, 0xc6, 0x8b, 0x13,
		0xb5, 0x2e
	};
	static const uint8 kExpectedMac[16] = {
		0xc0, 0x87, 0x59, 0x24, 0xc1, 0xc7, 0x98, 0x79,
		0x47, 0xde, 0xaf, 0xd8, 0x78, 0x0a, 0xcf, 0x49
	};

	uint8 cipher[114];
	uint8 mac[16];
	uint8 plain[114];
	crypto_aead_lock(cipher, mac, kKey, kNonce, kAad, sizeof(kAad),
		reinterpret_cast<const uint8*>(kPlain), kPlainLen);

	CHECK(memcmp(cipher, kExpectedCipher, kPlainLen) == 0,
		"XChaCha20-Poly1305 ciphertext matches draft-irtf-cfrg-xchacha A.3.1");
	CHECK(memcmp(mac, kExpectedMac, sizeof(kExpectedMac)) == 0,
		"XChaCha20-Poly1305 MAC matches draft-irtf-cfrg-xchacha A.3.1");

	int rc = crypto_aead_unlock(plain, mac, kKey, kNonce, kAad, sizeof(kAad),
		cipher, kPlainLen);
	CHECK(rc == 0, "XChaCha20-Poly1305 unlock succeeds");
	CHECK(memcmp(plain, kPlain, kPlainLen) == 0,
		"XChaCha20-Poly1305 decrypted plaintext matches");
}


static void
test_secure_buffer()
{
	BSecureBuffer buf;
	CHECK(buf.InitCheck() == B_OK && buf.IsEmpty(), "empty BSecureBuffer");

	static const uint8 kSecret[16] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
	};
	CHECK(buf.SetTo(kSecret, sizeof(kSecret)) == B_OK, "SetTo succeeds");
	CHECK(buf.Size() == sizeof(kSecret), "Size matches");

	BSecureBuffer copy;
	CHECK(copy.SetTo(kSecret, sizeof(kSecret)) == B_OK, "copy SetTo");
	CHECK(buf.Equals(copy), "Equals true for identical buffers");

	copy.Data()[7] ^= 0x01;
	CHECK(!buf.Equals(copy), "Equals false after 1-bit flip");

	// Verify Wipe zeroes bytes in place.
	buf.Wipe();
	const volatile uint8* p = buf.Data();
	bool allZero = true;
	for (size_t i = 0; i < buf.Size(); i++) {
		if (p[i] != 0)
			allZero = false;
	}
	CHECK(allZero, "BSecureBuffer::Wipe zeroes all bytes");

	buf.Clear();
	CHECK(buf.IsEmpty() && buf.Data() == NULL && buf.Size() == 0,
		"Clear frees and resets");

	// Verify static WipeMemory zeroes stack buffer.
	uint8 stackSecret[32];
	memset(stackSecret, 0xA5, sizeof(stackSecret));
	BSecureBuffer::WipeMemory(stackSecret, sizeof(stackSecret));
	const volatile uint8* sp = stackSecret;
	bool stackZero = true;
	for (size_t i = 0; i < sizeof(stackSecret); i++) {
		if (sp[i] != 0)
			stackZero = false;
	}
	CHECK(stackZero, "BSecureBuffer::WipeMemory zeroes stack buffer");
}


static void
test_random()
{
	BSecureBuffer a;
	BSecureBuffer b;
	CHECK(BRandom::Fill(a, 32) == B_OK, "BRandom::Fill a");
	CHECK(BRandom::Fill(b, 32) == B_OK, "BRandom::Fill b");
	CHECK(a.Size() == 32 && b.Size() == 32, "BRandom output sizes");
	CHECK(!a.Equals(b), "Two 32-byte random draws differ");

	bool anyNonZero = false;
	for (size_t i = 0; i < a.Size(); i++) {
		if (a.Data()[i] != 0)
			anyNonZero = true;
	}
	CHECK(anyNonZero, "Random buffer is not all zero");
}


static void
test_password_hash()
{
	BPasswordHash::Parameters fastParams = { 1024, 2, 1 };
	BString encoded;
	CHECK(BPasswordHash::Hash("correct horse battery staple", encoded,
		fastParams) == B_OK, "BPasswordHash::Hash succeeds");
	CHECK(strncmp(encoded.String(), "$argon2id$v=19$m=1024,t=2,p=1$", 30) == 0,
		"Encoded string has expected PHC prefix");

	bool matches = false;
	CHECK(BPasswordHash::Verify("correct horse battery staple",
		encoded.String(), &matches) == B_OK && matches,
		"Verify succeeds for right password");

	matches = true;
	CHECK(BPasswordHash::Verify("wrong password",
		encoded.String(), &matches) == B_OK && !matches,
		"Verify returns false for wrong password");

	// Malformed PHC strings must fail and leave matches == false.
	const char* badStrings[] = {
		"",
		"$s$14$abc$def",
		"$argon2id$v=18$m=1024,t=2,p=1$AAAAAAAAAAAAAAAAAAAAAA$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"$argon2id$v=19$m=1024,t=2,p=1$short$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"$argon2id$v=19$m=0,t=2,p=1$AAAAAAAAAAAAAAAAAAAAAA$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"$argon2id$v=19$m=1024,t=2,p=1$AAAAAAAAAAAAAAAAAAAAAB$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", // non-canonical b64
		"$argon2id$v=19$m=1024,t=2,p=1$AAAAAAAAAAAAAAAAAAAAAA$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA$extra"
	};
	for (size_t i = 0; i < sizeof(badStrings) / sizeof(badStrings[0]); i++) {
		matches = true;
		status_t st = BPasswordHash::Verify("pw", badStrings[i], &matches);
		CHECK(st != B_OK && !matches, "Malformed PHC string rejected");
	}

	CHECK(!BPasswordHash::NeedsRehash(encoded.String(), fastParams),
		"NeedsRehash false for matching parameters");
	CHECK(BPasswordHash::NeedsRehash(encoded.String(),
		BPasswordHash::DefaultParameters()),
		"NeedsRehash true when memory/passes below default");
	CHECK(BPasswordHash::NeedsRehash("$s$14$legacy$hash",
		BPasswordHash::DefaultParameters()),
		"NeedsRehash true for legacy scrypt hash");

	// Measure default cost (64 MiB, 3 passes, 1 lane) on this host.
	auto t0 = std::chrono::steady_clock::now();
	BString defaultEncoded;
	CHECK(BPasswordHash::Hash("r5-timing-check", defaultEncoded) == B_OK,
		"Default 64 MiB / 3 passes Hash succeeds");
	auto t1 = std::chrono::steady_clock::now();
	long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		t1 - t0).count();
	printf("Info: Argon2id default (64 MiB, 3 passes, 1 lane) took %lld ms on host\n",
		ms);
}


static void
test_sealed_data()
{
	BSecureBuffer key;
	CHECK(BRandom::Fill(key, BSealedData::kKeySize) == B_OK, "Generate key");

	const char* msg = "R5 Operating System keystore secret payload";
	const size_t msgLen = strlen(msg);
	const char* aad = "R5KS:v2:test-aad";
	const size_t aadLen = strlen(aad);

	uint8 sealed[256];
	size_t sealedLen = 0;
	CHECK(BSealedData::Seal(key, msg, msgLen, aad, aadLen,
		sealed, sizeof(sealed), &sealedLen) == B_OK, "Seal succeeds");
	CHECK(sealedLen == BSealedData::kOverhead + msgLen,
		"Sealed length equals overhead + plaintext");
	CHECK(memcmp(sealed, "R5SD\x01\x01\x00\x00", 8) == 0,
		"Sealed header matches R5SD v1");

	BSecureBuffer opened;
	CHECK(BSealedData::Open(key, sealed, sealedLen, aad, aadLen, opened) == B_OK,
		"Open succeeds");
	CHECK(opened.Size() == msgLen
		&& memcmp(opened.Data(), msg, msgLen) == 0,
		"Opened plaintext matches");

	// In-place Open (outPlaintext aliases sealed input buffer).
	{
		BSecureBuffer inplace;
		CHECK(inplace.SetTo(sealed, sealedLen) == B_OK, "inplace SetTo");
		CHECK(BSealedData::Open(key, inplace.Data(), inplace.Size(),
			aad, aadLen, inplace) == B_OK
			&& inplace.Size() == msgLen
			&& memcmp(inplace.Data(), msg, msgLen) == 0,
			"In-place Open with aliased buffer succeeds");
	}

	// Flip every single byte of the sealed blob and verify refusal.
	for (size_t i = 0; i < sealedLen; i++) {
		uint8 tampered[256];
		memcpy(tampered, sealed, sealedLen);
		tampered[i] ^= 0x01;
		BSecureBuffer out;
		status_t st = BSealedData::Open(key, tampered, sealedLen, aad, aadLen, out);
		CHECK(st != B_OK && out.IsEmpty(),
			"Tampered sealed blob byte refused and output cleared");
	}

	// Wrong AAD refused.
	{
		BSecureBuffer out;
		CHECK(BSealedData::Open(key, sealed, sealedLen, "wrong-aad", 9, out)
			== B_NOT_ALLOWED && out.IsEmpty(), "Wrong AAD refused");
	}

	// Wrong key refused.
	{
		BSecureBuffer wrongKey;
		CHECK(BRandom::Fill(wrongKey, BSealedData::kKeySize) == B_OK, "wrongKey");
		BSecureBuffer out;
		CHECK(BSealedData::Open(wrongKey, sealed, sealedLen, aad, aadLen, out)
			== B_NOT_ALLOWED && out.IsEmpty(), "Wrong key refused");
	}

	// Empty plaintext round-trip.
	{
		uint8 emptySealed[BSealedData::kOverhead];
		size_t outLen = 0;
		CHECK(BSealedData::Seal(key, NULL, 0, aad, aadLen,
			emptySealed, sizeof(emptySealed), &outLen) == B_OK
			&& outLen == BSealedData::kOverhead, "Seal empty plaintext");
		BSecureBuffer out;
		CHECK(BSealedData::Open(key, emptySealed, outLen, aad, aadLen, out) == B_OK
			&& out.IsEmpty(), "Open empty plaintext");
	}

	// Large AAD (> 256 bytes, heap path).
	{
		uint8 bigAad[512];
		memset(bigAad, 0x5C, sizeof(bigAad));
		uint8 bigSealed[256];
		size_t outLen = 0;
		CHECK(BSealedData::Seal(key, msg, msgLen, bigAad, sizeof(bigAad),
			bigSealed, sizeof(bigSealed), &outLen) == B_OK, "Seal large AAD");
		BSecureBuffer out;
		CHECK(BSealedData::Open(key, bigSealed, outLen, bigAad, sizeof(bigAad),
			out) == B_OK && out.Size() == msgLen, "Open large AAD");
	}

	// File seal/open and tampered file refusal.
	char path[128];
	snprintf(path, sizeof(path), "/tmp/r5-security-test-%ld.sealed",
		static_cast<long>(getpid()));
	unlink(path);

	CHECK(BSealedData::SealToFile(key, msg, msgLen, aad, aadLen, path) == B_OK,
		"SealToFile succeeds");

	struct stat st;
	CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600,
		"Sealed file permissions are 0600");

	BSecureBuffer fromFile;
	CHECK(BSealedData::OpenFromFile(key, path, aad, aadLen, fromFile) == B_OK
		&& fromFile.Size() == msgLen
		&& memcmp(fromFile.Data(), msg, msgLen) == 0,
		"OpenFromFile round-trip matches");

	// Symlink to sealed file must be refused (O_NOFOLLOW).
	char linkPath[144];
	snprintf(linkPath, sizeof(linkPath), "%s.link", path);
	unlink(linkPath);
	if (symlink(path, linkPath) == 0) {
		BSecureBuffer linkOut;
		CHECK(BSealedData::OpenFromFile(key, linkPath, aad, aadLen, linkOut)
			== B_BAD_DATA && linkOut.IsEmpty(),
			"OpenFromFile refuses symlink");
		unlink(linkPath);
	}

	// Tamper one byte of the file on disk and verify refusal.
	FILE* f = fopen(path, "r+b");
	CHECK(f != NULL, "Open sealed file for tamper test");
	if (f != NULL) {
		fseek(f, BSealedData::kOverhead, SEEK_SET);
		int ch = fgetc(f);
		fseek(f, BSealedData::kOverhead, SEEK_SET);
		fputc(ch ^ 0x80, f);
		fclose(f);
	}
	BSecureBuffer tamperedFileOut;
	CHECK(BSealedData::OpenFromFile(key, path, aad, aadLen, tamperedFileOut)
		== B_NOT_ALLOWED && tamperedFileOut.IsEmpty(),
		"Tampered sealed file refused");

	unlink(path);
}


int
main()
{
	test_argon2id_rfc9106_vector();
	test_xchacha20_poly1305_vector();
	test_secure_buffer();
	test_random();
	test_password_hash();
	test_sealed_data();

	if (sFailures == 0) {
		printf("ALL SECURITY KIT TESTS PASSED\n");
		return 0;
	}
	fprintf(stderr, "%d SECURITY KIT TEST(S) FAILED\n", sFailures);
	return 1;
}
