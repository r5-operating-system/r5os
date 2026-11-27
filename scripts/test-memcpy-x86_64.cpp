// SPDX-FileCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
//
// Host test for distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp.
// Compiles memcpy.cpp with R5_HOST_TEST and verifies every length 0..4096 plus
// streaming-size copies (> 0.75 * LLC) with every tail length 0..63 and every
// destination alignment 0..31 against a byte-by-byte reference loop.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R5_HOST_TEST 1
#define memcpy r5_test_memcpy

static uint32_t sMockFlags = 0;
static uint32_t sMockLlc = 64 * 1024; // 64 KiB -> streaming threshold 48 KiB

extern "C" {
#define R5_CPU_ERMS (1u << 0)
#define R5_CPU_FSRM (1u << 1)
#define R5_CPU_AVX2 (1u << 2)
#define R5_CPU_LLC_FALLBACK (16u * 1024u * 1024u)

static inline uint32_t r5_x86_cpu_flags(void) { return sMockFlags; }
static inline uint32_t r5_x86_cpu_llc(void) { return sMockLlc; }
}

#define _R5_X86_CPU_H
#include "../distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp"
#undef memcpy

static void
check_copy(uint8_t* dstBuf, const uint8_t* srcBuf, size_t dstOff, size_t srcOff,
	size_t len, const char* label)
{
	const uint8_t kGuard = 0xA5;
	size_t guardPre = 64;
	size_t guardPost = 64;
	size_t total = guardPre + dstOff + len + guardPost;
	memset(dstBuf, kGuard, total);

	uint8_t* dst = dstBuf + guardPre + dstOff;
	const uint8_t* src = srcBuf + guardPre + srcOff;

	void* ret = r5_test_memcpy(dst, src, len);
	if (ret != dst) {
		fprintf(stderr, "FAIL %s: wrong return value\n", label);
		exit(1);
	}
	for (size_t i = 0; i < guardPre + dstOff; i++) {
		if (dstBuf[i] != kGuard) {
			fprintf(stderr, "FAIL %s len=%zu dstOff=%zu srcOff=%zu: pre-guard clobbered at %zu\n",
				label, len, dstOff, srcOff, i);
			exit(1);
		}
	}
	for (size_t i = 0; i < len; i++) {
		if (dst[i] != src[i]) {
			fprintf(stderr, "FAIL %s len=%zu dstOff=%zu srcOff=%zu: mismatch at byte %zu (got %02x want %02x)\n",
				label, len, dstOff, srcOff, i, dst[i], src[i]);
			exit(1);
		}
	}
	for (size_t i = 0; i < guardPost; i++) {
		if (dst[len + i] != kGuard) {
			fprintf(stderr, "FAIL %s len=%zu dstOff=%zu srcOff=%zu: post-guard clobbered at +%zu\n",
				label, len, dstOff, srcOff, i);
			exit(1);
		}
	}
}

static void
run_suite(uint32_t flags, const char* label)
{
	sMockFlags = flags;
	sMemcpyChosen = 0;

	const size_t kMax = 128 * 1024;
	uint8_t* src = (uint8_t*)malloc(kMax + 256);
	uint8_t* dst = (uint8_t*)malloc(kMax + 256);
	if (src == NULL || dst == NULL) {
		fprintf(stderr, "OOM\n");
		exit(1);
	}
	for (size_t i = 0; i < kMax + 256; i++)
		src[i] = (uint8_t)((i * 131u + 17u) ^ (i >> 5));

	// 1. Every length from 0 to 4096 with multiple alignments (including 4K-aliased).
	const size_t offs[] = { 0, 1, 7, 15, 16, 17, 31 };
	for (size_t len = 0; len <= 4096; len++) {
		check_copy(dst, src, 0, 0, len, label);
		if (len <= 256 || (len % 64) < 33) {
			for (size_t d : offs)
				check_copy(dst, src, d, (d + 19) & 31, len, label);
		}
	}

	// 2. Streaming-size copies (> 0.75 * 64 KiB = 48 KiB) with EVERY tail 0..63
	//    and EVERY destination alignment 0..31.
	const size_t baseStream = 64 * 1024;
	for (size_t dstOff = 0; dstOff < 32; dstOff++) {
		for (size_t tail = 0; tail < 64; tail++) {
			check_copy(dst, src, dstOff, (dstOff + 13) & 31, baseStream + tail, label);
		}
	}

	free(src);
	free(dst);
	printf("PASS %s\n", label);
}

int
main()
{
	run_suite(0, "SSE");
	run_suite(R5_CPU_ERMS, "ERMS+SSE");
	run_suite(R5_CPU_ERMS | R5_CPU_FSRM, "ERMS+FSRM+SSE");
#if defined(__AVX2__)
	run_suite(R5_CPU_AVX2, "AVX2");
	run_suite(R5_CPU_AVX2 | R5_CPU_ERMS, "AVX2+ERMS");
	run_suite(R5_CPU_AVX2 | R5_CPU_ERMS | R5_CPU_FSRM, "AVX2+ERMS+FSRM");
#endif
	printf("ALL MEMCPY TESTS PASSED\n");
	return 0;
}
