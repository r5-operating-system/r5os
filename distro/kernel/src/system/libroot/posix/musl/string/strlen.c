#include <string.h>
#include <stdint.h>
#include <limits.h>
#undef strlen

#if !defined(__x86_64__) || defined(_BOOT_MODE) || defined(_KERNEL_MODE)

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1/UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX/2+1))
#define HASZERO(x) (((x)-ONES) & ~(x) & HIGHS)

size_t strlen(const char *s)
{
	const char *a = s;
#if defined(__GNUC__) && __GNUC__ >= 4
	typedef size_t __attribute__((__may_alias__)) word;
	const word *w;
	for (; (uintptr_t)s % ALIGN; s++) if (!*s) return s-a;
	for (w = (const void *)s; !HASZERO(*w); w++);
	s = (const void *)w;
#endif
	for (; *s; s++);
	return s-a;
}

#else

/* SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <r5_string_x86.h>

static size_t
strlen_sse(const char *s)
{
	const char *start = s;
	for (;;) {
		unsigned skip = (unsigned)(uintptr_t)s & 15u;
		int index = r5_zero_in_block(s, skip);
		if (index < 16)
			return (size_t)((s - skip) + index - start);
		s += 16 - skip;
	}
}


__attribute__((target("avx2")))
static size_t
strlen_avx2(const char *s)
{
	const char *start = s;
	while (((uintptr_t)s & 31u) != 0) {
		if (*s == '\0')
			return (size_t)(s - start);
		s++;
	}
	for (;;) {
		unsigned mask = r5_zero_mask32(s);
		if (mask != 0)
			return (size_t)(s - start) + (unsigned)__builtin_ctz(mask);
		s += 32;
	}
}


static size_t (*sStrlenLong)(const char *);


size_t
strlen(const char *s)
{
	const char *p = s;
	size_t seen = 0;
	while (seen < R5_STR_INLINE) {
		unsigned skip = (unsigned)(uintptr_t)p & 15u;
		int index = r5_zero_in_block(p, skip);
		if (index < 16)
			return seen + (size_t)(index - (int)skip);
		unsigned step = 16u - skip;
		p += step;
		seen += step;
	}
	size_t (*fn)(const char *) = __atomic_load_n(&sStrlenLong, __ATOMIC_ACQUIRE);
	if (fn == NULL) {
		fn = (r5_x86_cpu_flags() & R5_CPU_AVX2) != 0
			? strlen_avx2 : strlen_sse;
		__atomic_store_n(&sStrlenLong, fn, __ATOMIC_RELEASE);
	}
	return seen + fn(p);
}

/* SPDX-SnippetEnd */

#endif
