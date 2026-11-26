/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT license.
 */


#include <stdint.h>
#include <string.h>
#undef strcmp


#if !defined(__x86_64__) || defined(_BOOT_MODE) || defined(_KERNEL_MODE)

#define LACKS_ZERO_BYTE(value) \
	(((value - 0x01010101) & ~value & 0x80808080) == 0)

int
strcmp(char const *a, char const *b)
{
	if ((((addr_t)a) & 3) == 0 && (((addr_t)b) & 3) == 0) {
		uint32_t *a32 = (uint32_t *)a;
		uint32_t *b32 = (uint32_t *)b;

		while (*a32 == *b32 && LACKS_ZERO_BYTE((*a32))) {
			a32++;
			b32++;
		}
		a = (const char *)a32;
		b = (const char *)b32;
	}

	while (*a == *b && *a != 0) {
		a++;
		b++;
	}

	return (unsigned char)*a - (unsigned char)*b;
}

#else

/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <r5_string_x86.h>

static int
cmp_bytes(const char *a, const char *b, size_t n, int *done)
{
	size_t i;
	for (i = 0; i < n; i++) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca != cb || ca == 0) {
			*done = 1;
			return (int)ca - (int)cb;
		}
	}
	*done = 0;
	return 0;
}


static int
cmp16(const char *a, const char *b, int *done)
{
	__m128i va = r5_load16(a);
	__m128i vb = r5_load16(b);
	unsigned equal = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb));
	unsigned zeros = (unsigned)_mm_movemask_epi8(
		_mm_cmpeq_epi8(va, _mm_setzero_si128()));
	unsigned stop = ((~equal) | zeros) & 0xffffu;
	if (stop == 0) {
		*done = 0;
		return 0;
	}
	*done = 1;
	int index = __builtin_ctz(stop);
	return (int)(unsigned char)a[index] - (int)(unsigned char)b[index];
}


static int
strcmp_sse(const char *a, const char *b)
{
	for (;;) {
		unsigned roomA = r5_page_room(a);
		unsigned roomB = r5_page_room(b);
		unsigned room = roomA < roomB ? roomA : roomB;
		int done = 0;
		int diff;
		if (room < 16) {
			diff = cmp_bytes(a, b, room == 0 ? 1 : room, &done);
			if (done)
				return diff;
			a += room;
			b += room;
			continue;
		}
		diff = cmp16(a, b, &done);
		if (done)
			return diff;
		a += 16;
		b += 16;
	}
}


__attribute__((target("avx2")))
static int
strcmp_avx2(const char *a, const char *b)
{
	for (;;) {
		unsigned roomA = r5_page_room(a);
		unsigned roomB = r5_page_room(b);
		unsigned room = roomA < roomB ? roomA : roomB;
		if (room < 32) {
			int done = 0;
			int diff;
			if (room >= 16) {
				diff = cmp16(a, b, &done);
				if (done)
					return diff;
				a += 16;
				b += 16;
				room -= 16;
			}
			if (room > 0) {
				diff = cmp_bytes(a, b, room, &done);
				if (done)
					return diff;
				a += room;
				b += room;
			}
			continue;
		}
		__m256i va = _mm256_loadu_si256((const __m256i*)a);
		__m256i vb = _mm256_loadu_si256((const __m256i*)b);
		unsigned equal = (unsigned)_mm256_movemask_epi8(
			_mm256_cmpeq_epi8(va, vb));
		unsigned zeros = (unsigned)_mm256_movemask_epi8(
			_mm256_cmpeq_epi8(va, _mm256_setzero_si256()));
		unsigned stop = ~equal | zeros;
		if (stop != 0) {
			int index = __builtin_ctz(stop);
			return (int)(unsigned char)a[index]
				- (int)(unsigned char)b[index];
		}
		a += 32;
		b += 32;
	}
}


static int (*sStrcmpLong)(const char *, const char *);


int
strcmp(char const *a, char const *b)
{
	size_t seen = 0;
	while (seen < R5_STR_INLINE) {
		unsigned roomA = r5_page_room(a);
		unsigned roomB = r5_page_room(b);
		unsigned room = roomA < roomB ? roomA : roomB;
		int done = 0;
		int diff;
		if (room < 16) {
			diff = cmp_bytes(a, b, room == 0 ? 1 : room, &done);
			if (done)
				return diff;
			a += room;
			b += room;
			seen += room;
			continue;
		}
		diff = cmp16(a, b, &done);
		if (done)
			return diff;
		a += 16;
		b += 16;
		seen += 16;
	}
	int (*fn)(const char *, const char *)
		= __atomic_load_n(&sStrcmpLong, __ATOMIC_ACQUIRE);
	if (fn == NULL) {
		fn = (r5_x86_cpu_flags() & R5_CPU_AVX2) != 0
			? strcmp_avx2 : strcmp_sse;
		__atomic_store_n(&sStrcmpLong, fn, __ATOMIC_RELEASE);
	}
	return fn(a, b);
}

/* SPDX-SnippetEnd */

#endif
