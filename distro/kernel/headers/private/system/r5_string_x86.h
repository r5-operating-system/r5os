/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Page-safe x86-64 string scans. A 16-byte load is aligned, so it stays
 * in the page of a byte we already know is mapped. SSE4.2 compares those
 * 16 bytes. AVX2 does the same 32 bytes at a time, and only after the
 * caller has already scanned the start.
 */


#ifndef _R5_STRING_X86_H
#define _R5_STRING_X86_H

/* The bootloader is not compiled as x86-64-v2. The functions that include
 * this header are. SSE4.2 covers the 16-byte scans; AVX2 functions opt in.
 */
#pragma GCC target("sse4.2")

#include <stdint.h>
#include <emmintrin.h>
#include <smmintrin.h>
#include <immintrin.h>
#include <tmmintrin.h>

#include <r5_x86_cpu.h>


/* Bytes scanned with the inline SSE path before the long-string pointer. */
#define R5_STR_INLINE 32


static inline unsigned
r5_page_room(const void* pointer)
{
	return (unsigned)(4096u - ((uintptr_t)pointer & 4095u));
}


static inline __m128i
r5_load16(const void* pointer)
{
	/* Callers guarantee r5_page_room(pointer) >= 16, so all 16 bytes lie in
	 * the current page and an unaligned SSE load cannot cross a page boundary.
	 */
	return _mm_loadu_si128((const __m128i*)pointer);
}


static inline int
r5_zero_in_block(const char* pointer, unsigned skip)
{
	__m128i chunk = _mm_load_si128(
		(const __m128i*)((uintptr_t)pointer & ~(uintptr_t)15));
	unsigned mask = (unsigned)_mm_movemask_epi8(
		_mm_cmpeq_epi8(chunk, _mm_setzero_si128()));
	mask &= (~0u << skip) & 0xffffu;
	return mask != 0 ? __builtin_ctz(mask) : 16;
}


__attribute__((target("avx2")))
static inline unsigned
r5_zero_mask32(const void* pointer)
{
	__m256i chunk = _mm256_load_si256((const __m256i*)pointer);
	__m256i eq = _mm256_cmpeq_epi8(chunk, _mm256_setzero_si256());
	return (unsigned)_mm256_movemask_epi8(eq);
}


#endif
