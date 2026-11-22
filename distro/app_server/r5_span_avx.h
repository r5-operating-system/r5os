/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Row copy and the window-buffer blend, chosen once. AVX2 on AMD64 when
 * the CPU has it. Every other architecture uses memcpy and the C loop.
 */

#ifndef R5_SPAN_AVX_H
#define R5_SPAN_AVX_H

#include <stdint.h>
#include <string.h>

#if defined(__x86_64__)
#include <immintrin.h>

#include <r5_x86_cpu.h>


static inline void __attribute__((target("avx2")))
r5_span_copy_avx(void* dst, const void* src, size_t bytes)
{
	uint8_t* out = (uint8_t*)dst;
	const uint8_t* in = (const uint8_t*)src;
	size_t done = 0;
	for (; done + 32 <= bytes; done += 32) {
		__m256i block = _mm256_loadu_si256(
			(const __m256i*)(in + done));
		_mm256_storeu_si256((__m256i*)(out + done), block);
	}
	if (done < bytes)
		memcpy(out + done, in + done, bytes - done);
}


static inline void
r5_span_copy_c(void* dst, const void* src, size_t bytes)
{
	memcpy(dst, src, bytes);
}


static inline void
r5_span_copy(void* dst, const void* src, size_t bytes)
{
	static void (*copy)(void*, const void*, size_t);
	void (*use)(void*, const void*, size_t)
		= __atomic_load_n(&copy, __ATOMIC_RELAXED);
	if (use == NULL) {
		use = (r5_x86_cpu_flags() & R5_CPU_AVX2) != 0
			? r5_span_copy_avx : r5_span_copy_c;
		__atomic_store_n(&copy, use, __ATOMIC_RELAXED);
	}
	use(dst, src, bytes);
}


static inline void __attribute__((target("avx2")))
r5_span_blend_avx(uint32_t* bits, uint64_t pixels)
{
	const __m256i mask = _mm256_set1_epi32(0x7f7f7f7f);
	const __m256i add = _mm256_set1_epi32(0x20202020);
	uint64_t done = 0;
	for (; done + 8 <= pixels; done += 8) {
		__m256i block = _mm256_loadu_si256(
			(const __m256i*)(bits + done));
		block = _mm256_srli_epi32(block, 1);
		block = _mm256_and_si256(block, mask);
		block = _mm256_add_epi32(block, add);
		_mm256_storeu_si256((__m256i*)(bits + done), block);
	}
	for (; done < pixels; done++) {
		uint32_t value = bits[done];
		bits[done] = ((value >> 1) & 0x7f7f7f7fu) + 0x20202020u;
	}
}


static inline void
r5_span_blend(uint32_t* bits, uint64_t pixels)
{
	static int useAvx = -1;
	int use = __atomic_load_n(&useAvx, __ATOMIC_RELAXED);
	if (use < 0) {
		use = (r5_x86_cpu_flags() & R5_CPU_AVX2) != 0 ? 1 : 0;
		__atomic_store_n(&useAvx, use, __ATOMIC_RELAXED);
	}
	if (use) {
		r5_span_blend_avx(bits, pixels);
		return;
	}
	for (uint64_t i = 0; i < pixels; i++) {
		uint32_t value = bits[i];
		bits[i] = ((value >> 1) & 0x7f7f7f7fu) + 0x20202020u;
	}
}
#else
static inline void
r5_span_copy(void* dst, const void* src, size_t bytes)
{
	memcpy(dst, src, bytes);
}


static inline void
r5_span_blend(uint32_t* bits, uint64_t pixels)
{
	for (uint64_t i = 0; i < pixels; i++) {
		uint32_t value = bits[i];
		bits[i] = ((value >> 1) & 0x7f7f7f7fu) + 0x20202020u;
	}
}
#endif

#endif
