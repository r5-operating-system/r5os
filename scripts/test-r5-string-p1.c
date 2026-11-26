/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Exhaustive user-space test for R5 x86-64 string functions (memcmp, strcmp,
 * strlen, strchrnul, memchr) and APIC timer scale factor math.
 * Covers lengths 0..4200 and offsets 0..63 next to an unmapped guard page.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Prototypes of the R5 implementations (compiled separately and renamed via
 * objcopy --redefine-sym so we test the exact unmodified translation units). */
int r5_test_memcmp(const void *a, const void *b, size_t n);
int r5_test_strcmp(const char *a, const char *b);
size_t r5_test_strlen(const char *s);
char *r5_test_strchrnul(const char *s, int c);
void *r5_test_memchr(const void *s, int c, size_t n);


static int
ref_memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	for (size_t i = 0; i < n; i++) {
		if (pa[i] != pb[i])
			return (int)pa[i] - (int)pb[i];
	}
	return 0;
}


static int
ref_strcmp(const char *a, const char *b)
{
	for (;;) {
		unsigned char ca = (unsigned char)*a++;
		unsigned char cb = (unsigned char)*b++;
		if (ca != cb || ca == 0)
			return (int)ca - (int)cb;
	}
}


static size_t
ref_strlen(const char *s)
{
	size_t n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}


static char *
ref_strchrnul(const char *s, int c)
{
	unsigned char uc = (unsigned char)c;
	while (*s != '\0' && (unsigned char)*s != uc)
		s++;
	return (char *)s;
}


static void *
ref_memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = (const unsigned char *)s;
	unsigned char uc = (unsigned char)c;
	for (size_t i = 0; i < n; i++) {
		if (p[i] == uc)
			return (void *)(p + i);
	}
	return NULL;
}


static int
sign_of(int x)
{
	return (x > 0) - (x < 0);
}


static int
test_apic_scale(void)
{
	const uint64_t freqs[] = {
		2295732986ULL,
		2300000000ULL,
		1000000000ULL,
		3579545ULL,
		999999ULL,
		1000001ULL,
		4341234567ULL
	};
	const uint64_t kMaxFast = 18446744073709ULL;
	for (size_t fi = 0; fi < sizeof(freqs) / sizeof(freqs[0]); fi++) {
		uint64_t freq = freqs[fi];
		uint64_t q = freq / 1000000ULL;
		uint64_t rem = freq % 1000000ULL;
		uint64_t scale = (uint64_t)((((__uint128_t)rem << 64) + 999999ULL)
			/ 1000000ULL);
		for (uint64_t t = 1; t <= 200000; t++) {
			uint64_t want = (uint64_t)(((__uint128_t)t * freq) / 1000000ULL);
			uint64_t got = t * q + (uint64_t)(((__uint128_t)t * scale) >> 64);
			if (got != want) {
				fprintf(stderr, "FAIL apic scale freq=%llu t=%llu got=%llu want=%llu\n",
					(unsigned long long)freq, (unsigned long long)t,
					(unsigned long long)got, (unsigned long long)want);
				return 1;
			}
		}
		const uint64_t edge[] = {
			1000000ULL, 1000001ULL, 999999ULL, 1000000000ULL,
			1000000000000ULL, kMaxFast - 1, kMaxFast
		};
		for (size_t ei = 0; ei < sizeof(edge) / sizeof(edge[0]); ei++) {
			uint64_t t = edge[ei];
			uint64_t want = (uint64_t)(((__uint128_t)t * freq) / 1000000ULL);
			uint64_t got = t * q + (uint64_t)(((__uint128_t)t * scale) >> 64);
			if (got != want) {
				fprintf(stderr, "FAIL apic edge freq=%llu t=%llu got=%llu want=%llu\n",
					(unsigned long long)freq, (unsigned long long)t,
					(unsigned long long)got, (unsigned long long)want);
				return 1;
			}
		}
	}
	printf("ok apic timer scale (exact to the tick across 7 frequencies)\n");
	return 0;
}


int
main(void)
{
	/* Allocate 3 pages + 1 PROT_NONE guard page for both A and B. */
	const size_t kMapped = 3 * 4096;
	unsigned char *bufA = (unsigned char *)mmap(NULL, kMapped + 4096,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	unsigned char *bufB = (unsigned char *)mmap(NULL, kMapped + 4096,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (bufA == MAP_FAILED || bufB == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	if (mprotect(bufA + kMapped, 4096, PROT_NONE) != 0
		|| mprotect(bufB + kMapped, 4096, PROT_NONE) != 0) {
		perror("mprotect");
		return 1;
	}

	/* Fill with non-zero pattern. */
	for (size_t i = 0; i < kMapped; i++) {
		unsigned char v = (unsigned char)(1 + (i * 37 + 13) % 250);
		bufA[i] = v;
		bufB[i] = v;
	}

	/* 1. Lengths 0..4200 at page edge (end sits right against PROT_NONE). */
	for (size_t len = 0; len <= 4200; len++) {
		unsigned char *a = bufA + kMapped - len;
		unsigned char *b = bufB + kMapped - len;
		/* Restore pattern in tail. */
		for (size_t i = 0; i < len; i++) {
			unsigned char v = (unsigned char)(1 + ((i + len) % 250));
			a[i] = v;
			b[i] = v;
		}
		if (r5_test_memcmp(a, b, len) != 0) {
			fprintf(stderr, "FAIL memcmp eq len=%zu\n", len);
			return 1;
		}
		if (len > 0) {
			size_t posList[4] = { 0, len / 2, len > 32 ? len - 32 : 0, len - 1 };
			for (int k = 0; k < 4; k++) {
				size_t p = posList[k];
				unsigned char saved = b[p];
				b[p] = (unsigned char)(saved ^ 0x5a);
				int got = r5_test_memcmp(a, b, len);
				int want = ref_memcmp(a, b, len);
				if (sign_of(got) != sign_of(want)) {
					fprintf(stderr, "FAIL memcmp diff len=%zu p=%zu\n", len, p);
					return 1;
				}
				b[p] = saved;
			}
		}

		/* memchr: not found and found at edge */
		if (r5_test_memchr(a, 0xff, len) != ref_memchr(a, 0xff, len)) {
			fprintf(stderr, "FAIL memchr miss len=%zu\n", len);
			return 1;
		}
		if (len > 0) {
			unsigned char saved = a[len - 1];
			a[len - 1] = 0xff;
			if (r5_test_memchr(a, 0xff, len) != a + len - 1) {
				fprintf(stderr, "FAIL memchr tail len=%zu\n", len);
				return 1;
			}
			a[len - 1] = saved;
		}

		/* NUL-terminated strings of length len ending right at guard page. */
		char *sa = (char *)(bufA + kMapped - (len + 1));
		char *sb = (char *)(bufB + kMapped - (len + 1));
		for (size_t i = 0; i < len; i++) {
			char v = (char)(1 + ((i + len) % 250));
			sa[i] = v;
			sb[i] = v;
		}
		sa[len] = '\0';
		sb[len] = '\0';

		if (r5_test_strlen(sa) != ref_strlen(sa)) {
			fprintf(stderr, "FAIL strlen len=%zu\n", len);
			return 1;
		}
		if (r5_test_strchrnul(sa, 0xff) != ref_strchrnul(sa, 0xff)) {
			fprintf(stderr, "FAIL strchrnul miss len=%zu\n", len);
			return 1;
		}
		if (r5_test_strchrnul(sa, 0) != ref_strchrnul(sa, 0)) {
			fprintf(stderr, "FAIL strchrnul nul len=%zu\n", len);
			return 1;
		}
		if (len > 0) {
			char saved = sa[len - 1];
			sa[len - 1] = (char)0xff;
			if (r5_test_strchrnul(sa, 0xff) != sa + len - 1) {
				fprintf(stderr, "FAIL strchrnul hit len=%zu\n", len);
				return 1;
			}
			sa[len - 1] = saved;
		}
		if (r5_test_strcmp(sa, sb) != 0) {
			fprintf(stderr, "FAIL strcmp eq len=%zu\n", len);
			return 1;
		}
		if (len > 0) {
			char saved = sb[len - 1];
			sb[len - 1] = (char)((unsigned char)saved ^ 0x20);
			if (sign_of(r5_test_strcmp(sa, sb)) != sign_of(ref_strcmp(sa, sb))) {
				fprintf(stderr, "FAIL strcmp diff len=%zu\n", len);
				return 1;
			}
			sb[len - 1] = saved;
		}
	}

	/* 2. Every offset 0..63 next to a page edge (crossing and non-crossing),
	 *    including mismatched offsets between A and B. */
	for (size_t offA = 0; offA < 64; offA++) {
		for (size_t offB = 0; offB < 64; offB++) {
			size_t len = 4150;
			unsigned char *a = bufA + kMapped - len - offA;
			unsigned char *b = bufB + kMapped - len - offB;
			for (size_t i = 0; i < len; i++) {
				unsigned char v = (unsigned char)(1 + (i % 250));
				a[i] = v;
				b[i] = v;
			}
			a[len - 1] = '\0';
			b[len - 1] = '\0';
			if (r5_test_memcmp(a, b, len) != 0) {
				fprintf(stderr, "FAIL memcmp offA=%zu offB=%zu\n", offA, offB);
				return 1;
			}
			if (r5_test_strcmp((const char *)a, (const char *)b) != 0) {
				fprintf(stderr, "FAIL strcmp offA=%zu offB=%zu\n", offA, offB);
				return 1;
			}
			/* Early mismatch right before the page edge. */
			b[len - 2] = (unsigned char)0xfe;
			if (sign_of(r5_test_memcmp(a, b, len))
					!= sign_of(ref_memcmp(a, b, len))) {
				fprintf(stderr, "FAIL memcmp edge diff offA=%zu offB=%zu\n",
					offA, offB);
				return 1;
			}
			if (sign_of(r5_test_strcmp((const char *)a, (const char *)b))
					!= sign_of(ref_strcmp((const char *)a, (const char *)b))) {
				fprintf(stderr, "FAIL strcmp edge diff offA=%zu offB=%zu\n",
					offA, offB);
				return 1;
			}
		}
	}

	printf("ok string functions (memcmp/strcmp/strlen/strchrnul/memchr: "
		"len 0..4200, offsets 0..63 vs PROT_NONE guard page)\n");
	if (test_apic_scale() != 0)
		return 1;
	return 0;
}
