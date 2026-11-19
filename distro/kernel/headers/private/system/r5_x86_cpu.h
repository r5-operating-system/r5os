/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * One place that reads CPUID for user-space dispatch. x86-64-v2 is the
 * build baseline and is not reported here. AVX, AVX2 and FMA count only
 * when the OS enabled XSAVE and both XMM and YMM in XCR0. The first call
 * in a translation unit reads the leaves; later calls reuse that value.
 * There is no new exported libroot symbol.
 */


#ifndef _R5_X86_CPU_H
#define _R5_X86_CPU_H

#include <stdint.h>


#define R5_CPU_AVX	(1u << 0)
#define R5_CPU_AVX2	(1u << 1)
#define R5_CPU_FMA	(1u << 2)
#define R5_CPU_BMI1	(1u << 3)
#define R5_CPU_BMI2	(1u << 4)
#define R5_CPU_ERMS	(1u << 5)
#define R5_CPU_FSRM	(1u << 6)
#define R5_CPU_FMA4	(1u << 7)

/* Last-level cache when leaf 4 / 0x8000001D is missing or unusable. */
#define R5_CPU_LLC_FALLBACK	(8u * 1024u * 1024u)


static inline void
r5_x86_cpuid(uint32_t leaf, uint32_t sub, uint32_t* eax, uint32_t* ebx,
	uint32_t* ecx, uint32_t* edx)
{
	__asm__ __volatile__("cpuid"
		: "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
		: "a" (leaf), "c" (sub));
}


static inline uint64_t
r5_x86_xcr0(void)
{
	uint32_t eax, edx;
	__asm__ __volatile__("xgetbv"
		: "=a" (eax), "=d" (edx)
		: "c" (0));
	return ((uint64_t)edx << 32) | eax;
}


/* Low 32 bits: feature flags. High 32 bits: last-level cache, in bytes. */
static inline uint64_t
r5_x86_cpu_pack(void)
{
	static uint64_t pack;
	uint64_t ready = __atomic_load_n(&pack, __ATOMIC_ACQUIRE);
	if (ready != 0)
		return ready;

	uint32_t eax, ebx, ecx, edx;
	uint32_t flags = 0;
	r5_x86_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	const int osxsave = (ecx & (1u << 27)) != 0;
	const int avx = (ecx & (1u << 28)) != 0;
	const int fma = (ecx & (1u << 12)) != 0;
	int ymm = 0;
	if (osxsave) {
		uint64_t xcr0 = r5_x86_xcr0();
		/* XMM is bit 1, YMM is bit 2. Both must be set. */
		ymm = ((xcr0 & 0x6) == 0x6);
	}
	if (avx && ymm)
		flags |= R5_CPU_AVX;
	if (fma && ymm)
		flags |= R5_CPU_FMA;

	r5_x86_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
	uint32_t maxLeaf = eax;
	/* GenuineIntel and AuthenticAMD, in ebx/edx/ecx order. */
	const int intel = ebx == 0x756e6547u && edx == 0x49656e69u
		&& ecx == 0x6c65746eu;
	const int amd = ebx == 0x68747541u && edx == 0x69746e65u
		&& ecx == 0x444d4163u;

	if (maxLeaf >= 7) {
		r5_x86_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
		if ((ebx & (1u << 5)) != 0 && ymm)
			flags |= R5_CPU_AVX2;
		if ((ebx & (1u << 3)) != 0)
			flags |= R5_CPU_BMI1;
		if ((ebx & (1u << 8)) != 0)
			flags |= R5_CPU_BMI2;
		if ((ebx & (1u << 9)) != 0)
			flags |= R5_CPU_ERMS;
		if ((edx & (1u << 4)) != 0)
			flags |= R5_CPU_FSRM;
	}

	/* FMA4 is AMD's earlier fused multiply-add. It is not FMA3. */
	{
		uint32_t extEax, extEbx, extEcx, extEdx;
		r5_x86_cpuid(0x80000000, 0, &extEax, &extEbx, &extEcx, &extEdx);
		if (extEax >= 0x80000001) {
			r5_x86_cpuid(0x80000001, 0, &extEax, &extEbx, &extEcx, &extEdx);
			if ((extEcx & (1u << 16)) != 0 && ymm)
				flags |= R5_CPU_FMA4;
		}
	}

	uint32_t llc = R5_CPU_LLC_FALLBACK;
	uint32_t cacheLeaf = 0;
	if (intel && maxLeaf >= 4)
		cacheLeaf = 4;
	else if (amd) {
		r5_x86_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
		if (eax >= 0x8000001D)
			cacheLeaf = 0x8000001D;
	}
	if (cacheLeaf != 0) {
		uint32_t bestLevel = 0;
		for (uint32_t sub = 0; sub < 16; sub++) {
			r5_x86_cpuid(cacheLeaf, sub, &eax, &ebx, &ecx, &edx);
			uint32_t type = eax & 0x1f;
			if (type == 0)
				break;
			uint32_t level = (eax >> 5) & 7;
			/* Data (1) or unified (3). Keep the highest of those. */
			if (type != 1 && type != 3)
				continue;
			if (level < bestLevel)
				continue;
			uint32_t line = (ebx & 0xfff) + 1;
			uint32_t parts = ((ebx >> 12) & 0x3ff) + 1;
			uint32_t ways = ((ebx >> 22) & 0x3ff) + 1;
			uint32_t sets = ecx + 1;
			uint64_t bytes = (uint64_t)ways * parts * line * sets;
			if (bytes == 0 || bytes > 0xffffffffu)
				continue;
			bestLevel = level;
			llc = (uint32_t)bytes;
		}
	}

	ready = ((uint64_t)llc << 32) | flags;
	if (ready == 0)
		ready = (uint64_t)R5_CPU_LLC_FALLBACK << 32;
	__atomic_store_n(&pack, ready, __ATOMIC_RELEASE);
	return ready;
}


static inline uint32_t
r5_x86_cpu_flags(void)
{
	return (uint32_t)r5_x86_cpu_pack();
}


static inline uint32_t
r5_x86_cpu_llc(void)
{
	return (uint32_t)(r5_x86_cpu_pack() >> 32);
}

#endif
