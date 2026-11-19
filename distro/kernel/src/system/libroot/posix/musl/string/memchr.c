#include <string.h>
#include <stdint.h>
#include <limits.h>
#undef memchr

#if !defined(__x86_64__) || defined(_BOOT_MODE)

#define SS (sizeof(size_t))
#define ALIGN (sizeof(size_t)-1)
#define ONES ((size_t)-1/UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX/2+1))
#define HASZERO(x) (((x)-ONES) & ~(x) & HIGHS)

void *memchr(const void *src, int c, size_t n)
{
	const unsigned char *s = src;
	c = (unsigned char)c;
#if defined(__GNUC__) && __GNUC__ >= 4
	for (; ((uintptr_t)s & ALIGN) && n && *s != c; s++, n--);
	if (n && *s != c) {
		typedef size_t __attribute__((__may_alias__)) word;
		const word *w;
		size_t k = ONES * c;
		for (w = (const void *)s; n>=SS && !HASZERO(*w^k); w++, n-=SS);
		s = (const void *)w;
	}
#endif
	for (; n && *s != c; s++, n--);
	return n ? (void *)s : 0;
}

#else

/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <r5_string_x86.h>

static const unsigned char *
memchr_sse(const unsigned char *s, int c, size_t n)
{
	__m128i needle = _mm_set1_epi8((char)c);
	while (n > 0) {
		if (n >= 16 && r5_page_room(s) >= 16) {
			int index = r5_index16(needle, r5_load16(s));
			if (index < 16)
				return s + index;
			s += 16;
			n -= 16;
			continue;
		}
		if (*s == (unsigned char)c)
			return s;
		s++;
		n--;
	}
	return 0;
}


__attribute__((target("avx2")))
static const unsigned char *
memchr_avx2(const unsigned char *s, int c, size_t n)
{
	while (n > 0 && ((uintptr_t)s & 31u) != 0) {
		if (*s == (unsigned char)c)
			return s;
		s++;
		n--;
	}
	__m256i needle = _mm256_set1_epi8((char)c);
	while (n >= 32 && r5_page_room(s) >= 32) {
		__m256i chunk = _mm256_load_si256((const __m256i*)s);
		unsigned mask = (unsigned)_mm256_movemask_epi8(
			_mm256_cmpeq_epi8(chunk, needle));
		if (mask != 0)
			return s + (unsigned)__builtin_ctz(mask);
		s += 32;
		n -= 32;
	}
	return memchr_sse(s, c, n);
}


static const unsigned char *(*sMemchrLong)(const unsigned char *, int, size_t);


void *
memchr(const void *src, int c, size_t n)
{
	const unsigned char *s = src;
	c = (unsigned char)c;
	if (n < R5_STR_INLINE)
		return (void *)memchr_sse(s, c, n);
	const unsigned char *(*fn)(const unsigned char *, int, size_t)
		= __atomic_load_n(&sMemchrLong, __ATOMIC_ACQUIRE);
	if (fn == NULL) {
		fn = (r5_x86_cpu_flags() & R5_CPU_AVX2) != 0
			? memchr_avx2 : memchr_sse;
		__atomic_store_n(&sMemchrLong, fn, __ATOMIC_RELEASE);
	}
	return (void *)fn(s, c, n);
}

/* SPDX-SnippetEnd */

#endif
