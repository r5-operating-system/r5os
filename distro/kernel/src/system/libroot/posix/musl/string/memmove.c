#include <string.h>
#include <stdint.h>
#undef memmove

#if defined(__x86_64__)
#include <emmintrin.h>
#ifndef _KERNEL_MODE
#include <immintrin.h>
#include <r5_x86_cpu.h>
#endif

/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Overlap is copied 16 bytes at a time. A backward copy steps by 16 only
 * when the two regions are at least 16 bytes apart; a closer overlap would
 * let the store reach bytes the next load still needs. Neither path uses
 * std or rep movsb. No overlap still goes through memcpy.
 */

static void
move_bytes(unsigned char* d, const unsigned char* s, size_t n, int forward)
{
	if (forward) {
		while (n--)
			*d++ = *s++;
	} else {
		while (n--)
			d[n] = s[n];
	}
}


static void
move_words64(unsigned char* d, const unsigned char* s, size_t n, int forward)
{
	if (forward) {
		while (n >= 8) {
			uint64_t value;
			__builtin_memcpy(&value, s, 8);
			__builtin_memcpy(d, &value, 8);
			d += 8;
			s += 8;
			n -= 8;
		}
	} else {
		while (n >= 8) {
			n -= 8;
			uint64_t value;
			__builtin_memcpy(&value, s + n, 8);
			__builtin_memcpy(d + n, &value, 8);
		}
	}
	move_bytes(d, s, n, forward);
}


static void
move_words32(unsigned char* d, const unsigned char* s, size_t n, int forward)
{
	if (forward) {
		while (n >= 4) {
			uint32_t value;
			__builtin_memcpy(&value, s, 4);
			__builtin_memcpy(d, &value, 4);
			d += 4;
			s += 4;
			n -= 4;
		}
	} else {
		while (n >= 4) {
			n -= 4;
			uint32_t value;
			__builtin_memcpy(&value, s + n, 4);
			__builtin_memcpy(d + n, &value, 4);
		}
	}
	move_bytes(d, s, n, forward);
}


static void
move_sse(unsigned char* d, const unsigned char* s, size_t n, int forward)
{
	if (forward) {
		while (n >= 16) {
			__m128i value = _mm_loadu_si128((const __m128i*)s);
			_mm_storeu_si128((__m128i*)d, value);
			d += 16;
			s += 16;
			n -= 16;
		}
	} else {
		while (n >= 16) {
			n -= 16;
			__m128i value = _mm_loadu_si128((const __m128i*)(s + n));
			_mm_storeu_si128((__m128i*)(d + n), value);
		}
	}
	move_bytes(d, s, n, forward);
}


#ifndef _KERNEL_MODE
static __attribute__((target("avx2"))) void
move_avx2(unsigned char* d, const unsigned char* s, size_t n, int forward)
{
	if (forward) {
		while (n >= 32) {
			__m256i value = _mm256_loadu_si256((const __m256i*)s);
			_mm256_storeu_si256((__m256i*)d, value);
			d += 32;
			s += 32;
			n -= 32;
		}
	} else {
		while (n >= 32) {
			n -= 32;
			__m256i value = _mm256_loadu_si256((const __m256i*)(s + n));
			_mm256_storeu_si256((__m256i*)(d + n), value);
		}
	}
	move_sse(d, s, n, forward);
}
#endif

/* SPDX-SnippetEnd */
#endif

#if defined(__R5_USE_IFUNC__) && defined(__HAIKU__) && defined(__x86_64__) \
	&& !defined(_KERNEL_MODE) && !defined(_BOOT_MODE) && !defined(R5_HOST_TEST)
/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
static __attribute__((target("avx2"))) void*
r5_memmove_avx2(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;
	if (d == s) return d;
	if ((uintptr_t)s - (uintptr_t)d - n <= -2 * n) return memcpy(d, s, n);
	unsigned char* destBytes = (unsigned char*)d;
	const unsigned char* srcBytes = (const unsigned char*)s;
	int forward = destBytes < srcBytes;
	uintptr_t apart = forward
		? (uintptr_t)(srcBytes - destBytes)
		: (uintptr_t)(destBytes - srcBytes);
	if (apart >= 32)
		move_avx2(destBytes, srcBytes, n, forward);
	else if (apart >= 16)
		move_sse(destBytes, srcBytes, n, forward);
	else if (apart >= 8)
		move_words64(destBytes, srcBytes, n, forward);
	else if (apart >= 4)
		move_words32(destBytes, srcBytes, n, forward);
	else
		move_bytes(destBytes, srcBytes, n, forward);
	return dest;
}

static void*
r5_memmove_sse(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;
	if (d == s) return d;
	if ((uintptr_t)s - (uintptr_t)d - n <= -2 * n) return memcpy(d, s, n);
	unsigned char* destBytes = (unsigned char*)d;
	const unsigned char* srcBytes = (const unsigned char*)s;
	int forward = destBytes < srcBytes;
	uintptr_t apart = forward
		? (uintptr_t)(srcBytes - destBytes)
		: (uintptr_t)(destBytes - srcBytes);
	if (apart >= 16)
		move_sse(destBytes, srcBytes, n, forward);
	else if (apart >= 8)
		move_words64(destBytes, srcBytes, n, forward);
	else if (apart >= 4)
		move_words32(destBytes, srcBytes, n, forward);
	else
		move_bytes(destBytes, srcBytes, n, forward);
	return dest;
}

static void* (*r5_memmove_resolve(void))(void*, const void*, size_t)
{
	if ((r5_x86_cpu_flags() & R5_CPU_AVX2) != 0)
		return r5_memmove_avx2;
	return r5_memmove_sse;
}

void* memmove(void*, const void*, size_t) __attribute__((ifunc("r5_memmove_resolve")));
/* SPDX-SnippetEnd */
#else
void *memmove(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;

	if (d==s) return d;
	if ((uintptr_t)s-(uintptr_t)d-n <= -2*n) return memcpy(d, s, n);

#if defined(__x86_64__)
	/* SPDX-SnippetBegin
	 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	 * SPDX-License-Identifier: MIT
	 */
	{
		unsigned char* destBytes = (unsigned char*)d;
		const unsigned char* srcBytes = (const unsigned char*)s;
		int forward = destBytes < srcBytes;
		uintptr_t apart = forward
			? (uintptr_t)(srcBytes - destBytes)
			: (uintptr_t)(destBytes - srcBytes);
#ifndef _KERNEL_MODE
		static int sMemmoveAvx2 = -1;
		int useAvx2 = __atomic_load_n(&sMemmoveAvx2, __ATOMIC_RELAXED);
		if (useAvx2 < 0) {
			useAvx2 = (r5_x86_cpu_flags() & R5_CPU_AVX2) != 0 ? 1 : 0;
			__atomic_store_n(&sMemmoveAvx2, useAvx2, __ATOMIC_RELAXED);
		}
		if (apart >= 32 && useAvx2)
			move_avx2(destBytes, srcBytes, n, forward);
		else
#endif
		if (apart >= 16)
			move_sse(destBytes, srcBytes, n, forward);
		else if (apart >= 8)
			move_words64(destBytes, srcBytes, n, forward);
		else if (apart >= 4)
			move_words32(destBytes, srcBytes, n, forward);
		else
			move_bytes(destBytes, srcBytes, n, forward);
	}
	/* SPDX-SnippetEnd */
#else
#if defined(__GNUC__) && __GNUC__ >= 4
	typedef __attribute__((__may_alias__)) size_t WT;
#define WS (sizeof(WT))
#endif

	if (d<s) {
#if defined(__GNUC__) && __GNUC__ >= 4
		if ((uintptr_t)s % WS == (uintptr_t)d % WS) {
			while ((uintptr_t)d % WS) {
				if (!n--) return dest;
				*d++ = *s++;
			}
			for (; n>=WS; n-=WS, d+=WS, s+=WS) *(WT *)d = *(WT *)s;
		}
#endif
		for (; n; n--) *d++ = *s++;
	} else {
#if defined(__GNUC__) && __GNUC__ >= 4
		if ((uintptr_t)s % WS == (uintptr_t)d % WS) {
			while ((uintptr_t)(d+n) % WS) {
				if (!n--) return dest;
				d[n] = s[n];
			}
			while (n>=WS) n-=WS, *(WT *)(d+n) = *(WT *)(s+n);
		}
#endif
		while (n) n--, d[n] = s[n];
	}
#endif

	return dest;
}
#endif
