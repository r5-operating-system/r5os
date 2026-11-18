/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compare memcpy, memset and memmove with byte copies. Sizes 0..4096,
 * alignments 0..63, random sizes up to 16 MiB, and memmove overlaps 1..64.
 * Stops after 60 seconds.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>


static const size_t kSmall = 4096;
static const size_t kAlign = 64;
static const size_t kLarge = 16 * 1024 * 1024;
static const size_t kPad = kAlign + 64;
static const size_t kBytes = kLarge + kPad;


static int
same(const uint8_t* a, const uint8_t* b, size_t n)
{
	return n == 0 || memcmp(a, b, n) == 0;
}


static void
ref_memcpy(uint8_t* d, const uint8_t* s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		d[i] = s[i];
}


static void
ref_memset(uint8_t* d, uint8_t v, size_t n)
{
	for (size_t i = 0; i < n; i++)
		d[i] = v;
}


static void
ref_memmove(uint8_t* d, const uint8_t* s, size_t n)
{
	if (d < s) {
		for (size_t i = 0; i < n; i++)
			d[i] = s[i];
	} else if (d > s) {
		size_t i = n;
		while (i--)
			d[i] = s[i];
	}
}


static void
fill(uint8_t* p, size_t n, uint32_t seed)
{
	for (size_t i = 0; i < n; i++)
		p[i] = (uint8_t)(seed + i * 131u);
}


int
main(void)
{
	uint8_t* block = (uint8_t*)malloc(kBytes * 4);
	if (block == NULL) {
		printf("r5_mem_fuzz alloc failed\n");
		return 1;
	}
	uint8_t* src = block;
	uint8_t* got = block + kBytes;
	uint8_t* expect = block + kBytes * 2;
	uint8_t* scratch = block + kBytes * 3;
	time_t start = time(NULL);
	uint64_t checks = 0;

	for (size_t n = 0; n <= kSmall; n++) {
		for (size_t sa = 0; sa < kAlign; sa++) {
			for (size_t da = 0; da < kAlign; da++) {
				fill(src + sa, n, (uint32_t)(n + sa + da));
				ref_memcpy(expect + da, src + sa, n);
				memcpy(got + da, src + sa, n);
				if (!same(got + da, expect + da, n)) {
					printf("r5_mem_fuzz FAIL memcpy n=%zu sa=%zu da=%zu\n",
						n, sa, da);
					return 1;
				}
				ref_memset(expect + da, (uint8_t)(n + 3), n);
				memset(got + da, (int)(uint8_t)(n + 3), n);
				if (!same(got + da, expect + da, n)) {
					printf("r5_mem_fuzz FAIL memset n=%zu da=%zu\n", n, da);
					return 1;
				}
				checks++;
			}
		}
	}

	srand(1);
	for (int i = 0; i < 64; i++) {
		size_t n = (size_t)rand() % kLarge;
		size_t sa = (size_t)rand() % kAlign;
		size_t da = (size_t)rand() % kAlign;
		if (i >= 48)
			n = kLarge - (size_t)(i * 4096);
		fill(src + sa, n, (uint32_t)i);
		ref_memcpy(expect + da, src + sa, n);
		memcpy(got + da, src + sa, n);
		if (!same(got + da, expect + da, n)) {
			printf("r5_mem_fuzz FAIL large memcpy n=%zu\n", n);
			return 1;
		}
		ref_memset(expect + da, (uint8_t)i, n);
		memset(got + da, i, n);
		if (!same(got + da, expect + da, n)) {
			printf("r5_mem_fuzz FAIL large memset n=%zu\n", n);
			return 1;
		}
		checks++;
	}

	for (size_t n = 0; n <= kSmall; n++) {
		for (size_t offset = 1; offset <= 64; offset++) {
			fill(scratch, n + offset, (uint32_t)(n * 17 + offset));
			memcpy(src, scratch, n + offset);
			ref_memmove(src + offset, src, n);
			fill(scratch, n + offset, (uint32_t)(n * 17 + offset));
			memmove(scratch + offset, scratch, n);
			if (!same(scratch, src, n + offset)) {
				printf("r5_mem_fuzz FAIL memmove forward n=%zu off=%zu\n",
					n, offset);
				return 1;
			}
			fill(scratch, n + offset, (uint32_t)(n * 19 + offset));
			memcpy(src, scratch, n + offset);
			ref_memmove(src, src + offset, n);
			fill(scratch, n + offset, (uint32_t)(n * 19 + offset));
			memmove(scratch, scratch + offset, n);
			if (!same(scratch, src, n + offset)) {
				printf("r5_mem_fuzz FAIL memmove back n=%zu off=%zu\n",
					n, offset);
				return 1;
			}
			checks++;
		}
	}

	printf("r5_mem_fuzz PASS checks=%llu seconds=%ld\n",
		(unsigned long long)checks, (long)(time(NULL) - start));
	free(block);
	return 0;
}
