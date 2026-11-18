/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT license.
 */


#include <stdint.h>
#include <string.h>
#undef memcmp


#if !defined(__x86_64__) || defined(_BOOT_MODE)

#define MISALIGNMENT(PTR, TYPE) ((addr_t)(PTR) & (sizeof(TYPE) - 1))

int
memcmp(const void *_a, const void *_b, size_t count)
{
	const unsigned char *a = (const unsigned char *)_a;
	const unsigned char *b = (const unsigned char *)_b;

	if (MISALIGNMENT(a, size_t) == 0 && MISALIGNMENT(b, size_t) == 0) {
		size_t *asz = (size_t *)a;
		size_t *bsz = (size_t *)b;

		while (count >= sizeof(size_t) && *asz == *bsz) {
			asz++;
			bsz++;
			count -= sizeof(size_t);
		}
		a = (const unsigned char *)asz;
		b = (const unsigned char *)bsz;
	}

	while (count > 0 && *a == *b) {
		a++;
		b++;
		count--;
	}

	if (count == 0)
		return 0;

	return *a - *b;
}

#else

/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <r5_string_x86.h>

static int
memcmp_bytes(const unsigned char *a, const unsigned char *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (a[i] != b[i])
			return (int)a[i] - (int)b[i];
	}
	return 0;
}


static int
memcmp16(const unsigned char *a, const unsigned char *b, size_t n)
{
	__m128i va = r5_load16(a);
	__m128i vb = r5_load16(b);
	unsigned equal = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb));
	unsigned bits = n >= 16 ? 0xffffu : (1u << n) - 1u;
	unsigned stop = (~equal) & bits;
	if (stop == 0)
		return 0;
	int index = __builtin_ctz(stop);
	return (int)a[index] - (int)b[index];
}


static int
memcmp_sse(const unsigned char *a, const unsigned char *b, size_t count)
{
	while (count > 0) {
		unsigned roomA = r5_page_room(a);
		unsigned roomB = r5_page_room(b);
		unsigned room = roomA < roomB ? roomA : roomB;
		if (room > count)
			room = (unsigned)count;
		if (room < 16) {
			int diff = memcmp_bytes(a, b, room);
			if (diff != 0 || room == count)
				return diff;
			a += room;
			b += room;
			count -= room;
			continue;
		}
		int diff = memcmp16(a, b, 16);
		if (diff != 0)
			return diff;
		a += 16;
		b += 16;
		count -= 16;
	}
	return 0;
}


__attribute__((target("avx2")))
static int
memcmp_avx2(const unsigned char *a, const unsigned char *b, size_t count)
{
	while (count >= 32 && r5_page_room(a) >= 32 && r5_page_room(b) >= 32
		&& ((uintptr_t)a & 31u) == 0 && ((uintptr_t)b & 31u) == 0) {
		__m256i va = _mm256_load_si256((const __m256i*)a);
		__m256i vb = _mm256_load_si256((const __m256i*)b);
		unsigned equal = (unsigned)_mm256_movemask_epi8(
			_mm256_cmpeq_epi8(va, vb));
		unsigned stop = ~equal;
		if (stop != 0) {
			int index = __builtin_ctz(stop);
			return (int)a[index] - (int)b[index];
		}
		a += 32;
		b += 32;
		count -= 32;
	}
	return memcmp_sse(a, b, count);
}


static int (*sMemcmpLong)(const unsigned char *, const unsigned char *, size_t)
	= memcmp_sse;
static int sMemcmpChosen;


int
memcmp(const void *_a, const void *_b, size_t count)
{
	const unsigned char *a = _a;
	const unsigned char *b = _b;
	if (count < R5_STR_INLINE)
		return memcmp_sse(a, b, count);
	if (__atomic_load_n(&sMemcmpChosen, __ATOMIC_ACQUIRE) == 0) {
		if ((r5_x86_cpu_flags() & R5_CPU_AVX2) != 0)
			sMemcmpLong = memcmp_avx2;
		__atomic_store_n(&sMemcmpChosen, 1, __ATOMIC_RELEASE);
	}
	return sMemcmpLong(a, b, count);
}

/* SPDX-SnippetEnd */

#endif
