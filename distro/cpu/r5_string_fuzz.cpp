/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compare strlen, strchr, strchrnul, memchr, strcmp and memcmp with the
 * byte versions. Each function runs for the given number of seconds
 * (default 60). Strings are placed at the end of a page so a load that
 * crosses into the next page faults.
 */


#define _DEFAULT_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__HAIKU__)
#include <sys/mman.h>
#else
#include <sys/mman.h>
#endif


static uint8_t* gPage;
static const size_t kPage = 4096;
static int gSeconds = 60;
static const char* gName = "";


static void
fault(int)
{
	printf("r5_string_fuzz FAIL %s page fault\n", gName);
	_exit(1);
}


static size_t
ref_strlen(const char* s)
{
	size_t n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}


static const char*
ref_strchrnul(const char* s, int c)
{
	c = (unsigned char)c;
	while (*s != '\0' && (unsigned char)*s != (unsigned char)c)
		s++;
	return s;
}


static const char*
ref_strchr(const char* s, int c)
{
	const char* r = ref_strchrnul(s, c);
	return (unsigned char)*r == (unsigned char)c ? r : 0;
}


static void*
ref_memchr(const void* src, int c, size_t n)
{
	const unsigned char* s = (const unsigned char*)src;
	c = (unsigned char)c;
	size_t i;
	for (i = 0; i < n; i++) {
		if (s[i] == (unsigned char)c)
			return (void*)(s + i);
	}
	return 0;
}


static int
ref_strcmp(const char* a, const char* b)
{
	while (*a == *b && *a != '\0') {
		a++;
		b++;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}


static int
ref_memcmp(const void* va, const void* vb, size_t n)
{
	const unsigned char* a = (const unsigned char*)va;
	const unsigned char* b = (const unsigned char*)vb;
	size_t i;
	for (i = 0; i < n; i++) {
		if (a[i] != b[i])
			return (int)a[i] - (int)b[i];
	}
	return 0;
}


static void
put_string(uint8_t* dest, size_t n, int fill)
{
	size_t i;
	for (i = 0; i < n; i++)
		dest[i] = (uint8_t)(fill + (int)i * 17);
	if (n > 0 && dest[n - 1] == 0)
		dest[n - 1] = 1;
}


static int
same_int(int a, int b)
{
	return (a < 0 && b < 0) || (a > 0 && b > 0) || (a == 0 && b == 0);
}


static void
check_one(const uint8_t* s, size_t n, const uint8_t* t, size_t m)
{
	if (strlen((const char*)s) != ref_strlen((const char*)s)) {
		printf("r5_string_fuzz FAIL strlen n=%zu\n", n);
		_exit(1);
	}
	int ch;
	for (ch = 0; ch < 256; ch += 17) {
		const char* a = strchr((const char*)s, ch);
		const char* b = ref_strchr((const char*)s, ch);
		if (a != b) {
			printf("r5_string_fuzz FAIL strchr n=%zu c=%d\n", n, ch);
			_exit(1);
		}
		if (strchrnul((const char*)s, ch) != ref_strchrnul((const char*)s, ch)) {
			printf("r5_string_fuzz FAIL strchrnul n=%zu c=%d\n", n, ch);
			_exit(1);
		}
		if (memchr(s, ch, n) != ref_memchr(s, ch, n)) {
			printf("r5_string_fuzz FAIL memchr n=%zu c=%d\n", n, ch);
			_exit(1);
		}
	}
	if (strcmp((const char*)s, (const char*)t)
			!= ref_strcmp((const char*)s, (const char*)t)) {
		printf("r5_string_fuzz FAIL strcmp n=%zu m=%zu\n", n, m);
		_exit(1);
	}
	size_t common = n < m ? n : m;
	if (memcmp(s, t, common) != ref_memcmp(s, t, common)) {
		printf("r5_string_fuzz FAIL memcmp n=%zu\n", common);
		_exit(1);
	}
}


static void
sweep(void)
{
	size_t n, align;
	for (n = 0; n <= 128; n++) {
		for (align = 0; align < 64; align++) {
			uint8_t* s = gPage + 2048 + align;
			uint8_t* t = gPage + 512 + align;
			put_string(s, n, 3);
			s[n] = 0;
			put_string(t, n, 3);
			t[n] = 0;
			check_one(s, n, t, n);
			if (n > 0) {
				t[n - 1] = (uint8_t)(s[n - 1] + 1);
				t[n] = 0;
				check_one(s, n, t, n);
			}
			uint8_t* end = gPage + kPage - n - 1;
			put_string(end, n, 5);
			end[n] = 0;
			check_one(end, n, t, n);
		}
	}
}


static void
timed(const char* name, void (*fn)(void))
{
	gName = name;
	time_t start = time(NULL);
	uint64_t rounds = 0;
	do {
		fn();
		rounds++;
	} while (time(NULL) - start < gSeconds);
	printf("r5_string_fuzz PASS %s rounds=%llu seconds=%ld\n",
		name, (unsigned long long)rounds, (long)(time(NULL) - start));
}


static void
run_strlen(void)
{
	size_t n;
	for (n = 0; n <= 128; n++) {
		uint8_t* s = gPage + kPage - n - 1;
		if (strlen((const char*)s) != ref_strlen((const char*)s)) {
			printf("r5_string_fuzz FAIL strlen page n=%zu\n", n);
			_exit(1);
		}
	}
}


static void
run_strchr(void)
{
	size_t n;
	for (n = 0; n <= 96; n++) {
		uint8_t* s = gPage + kPage - n - 1;
		if (strchr((const char*)s, 'q') != ref_strchr((const char*)s, 'q')) {
			printf("r5_string_fuzz FAIL strchr page n=%zu\n", n);
			_exit(1);
		}
	}
}


static void
run_strchrnul(void)
{
	size_t n;
	for (n = 0; n <= 96; n++) {
		uint8_t* s = gPage + kPage - n - 1;
		if (strchrnul((const char*)s, 'q') != ref_strchrnul((const char*)s, 'q')
			|| strchrnul((const char*)s, 0) != ref_strchrnul((const char*)s, 0)) {
			printf("r5_string_fuzz FAIL strchrnul page n=%zu\n", n);
			_exit(1);
		}
	}
}


static void
run_memchr(void)
{
	size_t n;
	for (n = 0; n <= 96; n++) {
		uint8_t* s = gPage + kPage - n;
		if (memchr(s, 0x5a, n) != ref_memchr(s, 0x5a, n)) {
			printf("r5_string_fuzz FAIL memchr page n=%zu\n", n);
			_exit(1);
		}
	}
}


static void
run_strcmp(void)
{
	size_t n;
	for (n = 0; n <= 96; n++) {
		uint8_t* a = gPage + kPage - n - 1;
		uint8_t* b = gPage + 64;
		if (strcmp((const char*)a, (const char*)b)
				!= ref_strcmp((const char*)a, (const char*)b)) {
			printf("r5_string_fuzz FAIL strcmp page n=%zu\n", n);
			_exit(1);
		}
	}
}


static void
run_memcmp(void)
{
	size_t n;
	for (n = 0; n <= 96; n++) {
		uint8_t* a = gPage + kPage - n;
		uint8_t* b = gPage + 128;
		if (memcmp(a, b, n) != ref_memcmp(a, b, n)) {
			printf("r5_string_fuzz FAIL memcmp page n=%zu\n", n);
			_exit(1);
		}
	}
}


int
main(int argc, char** argv)
{
	if (argc > 1)
		gSeconds = atoi(argv[1]);
	if (gSeconds < 1)
		gSeconds = 1;
	signal(SIGSEGV, fault);
	gPage = (uint8_t*)mmap(0, kPage * 2, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON, -1, 0);
	if (gPage == (uint8_t*)MAP_FAILED) {
		printf("r5_string_fuzz FAIL mmap\n");
		return 1;
	}
	if (mprotect(gPage + kPage, kPage, PROT_NONE) != 0) {
		printf("r5_string_fuzz FAIL mprotect\n");
		return 1;
	}
	memset(gPage, 0x2e, kPage);
	gPage[kPage - 1] = 0;
	sweep();
	timed("strlen", run_strlen);
	timed("strchr", run_strchr);
	timed("strchrnul", run_strchrnul);
	timed("memchr", run_memchr);
	timed("strcmp", run_strcmp);
	timed("memcmp", run_memcmp);
	printf("r5_string_fuzz PASS all\n");
	return 0;
}
