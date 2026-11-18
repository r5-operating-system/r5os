#define _DEFAULT_SOURCE
#include <string.h>
#include <stdint.h>
#include <limits.h>
#undef strchrnul

#if !defined(__x86_64__)

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1/UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX/2+1))
#define HASZERO(x) (((x)-ONES) & ~(x) & HIGHS)

char *strchrnul(const char *s, int c)
{
	c = (unsigned char)c;
	if (!c) return (char *)s + strlen(s);

#if defined(__GNUC__) && __GNUC__ >= 4
	typedef size_t __attribute__((__may_alias__)) word;
	const word *w;
	for (; (uintptr_t)s % ALIGN; s++)
		if (!*s || *(unsigned char *)s == c) return (char *)s;
	size_t k = ONES * c;
	for (w = (void *)s; !HASZERO(*w) && !HASZERO(*w^k); w++);
	s = (void *)w;
#endif
	for (; *s && *(unsigned char *)s != c; s++);
	return (char *)s;
}

#else

/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <r5_string_x86.h>

static int
block_either(const char *s, unsigned skip, int c)
{
	__m128i chunk = _mm_load_si128(
		(const __m128i*)((uintptr_t)s & ~(uintptr_t)15));
	int fill = c == 1 ? 2 : 1;
	chunk = r5_hide_prefix(chunk, skip, fill);
	int zeroAt = r5_index16(_mm_setzero_si128(), chunk);
	int charAt = r5_index16(_mm_set1_epi8((char)c), chunk);
	return zeroAt < charAt ? zeroAt : charAt;
}


static char *
strchrnul_sse(const char *s, int c)
{
	for (;;) {
		unsigned skip = (unsigned)(uintptr_t)s & 15u;
		int index = block_either(s, skip, c);
		if (index < 16)
			return (char *)(s - skip) + index;
		s += 16 - skip;
	}
}


__attribute__((target("avx2")))
static char *
strchrnul_avx2(const char *s, int c)
{
	while (((uintptr_t)s & 31u) != 0) {
		if (*s == '\0' || (unsigned char)*s == (unsigned char)c)
			return (char *)s;
		s++;
	}
	__m256i needle = _mm256_set1_epi8((char)c);
	__m256i zero = _mm256_setzero_si256();
	for (;;) {
		__m256i chunk = _mm256_load_si256((const __m256i*)s);
		unsigned zeros = (unsigned)_mm256_movemask_epi8(
			_mm256_cmpeq_epi8(chunk, zero));
		unsigned chars = (unsigned)_mm256_movemask_epi8(
			_mm256_cmpeq_epi8(chunk, needle));
		unsigned mask = zeros | chars;
		if (mask != 0)
			return (char *)s + (unsigned)__builtin_ctz(mask);
		s += 32;
	}
}


static char *(*sStrchrnulLong)(const char *, int) = strchrnul_sse;
static int sStrchrnulChosen;


char *
strchrnul(const char *s, int c)
{
	c = (unsigned char)c;
	if (c == 0)
		return (char *)s + strlen(s);

	const char *p = s;
	size_t seen = 0;
	while (seen < R5_STR_INLINE) {
		unsigned skip = (unsigned)(uintptr_t)p & 15u;
		int index = block_either(p, skip, c);
		if (index < 16)
			return (char *)(p - skip) + index;
		unsigned step = 16u - skip;
		p += step;
		seen += step;
	}
	if (__atomic_load_n(&sStrchrnulChosen, __ATOMIC_ACQUIRE) == 0) {
		if ((r5_x86_cpu_flags() & R5_CPU_AVX2) != 0)
			sStrchrnulLong = strchrnul_avx2;
		__atomic_store_n(&sStrchrnulChosen, 1, __ATOMIC_RELEASE);
	}
	return sStrchrnulLong(p, c);
}

/* SPDX-SnippetEnd */

#endif
