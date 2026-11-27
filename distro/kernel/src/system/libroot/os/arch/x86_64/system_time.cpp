/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>

#include <stdint.h>

#include <x86intrin.h>


static uint64_t cv_factor;
static uint64_t cv_factor_nsec;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// R5: the TSC value system_time() counts from. 0 when the kernel reset the
// TSCs at boot; the firmware's TSC at kernel start when the kernel kept them
// because they were already in sync. The kernel and libroot use the same
// value (the commpage's system_time_tsc_base).
static uint64_t cv_base;


static inline uint64_t
tsc_since_base(uint64_t tsc)
{
	// Never below the base: a read right at boot on a CPU a few cycles
	// behind gives 0, not a value near 2^64.
	return tsc > cv_base ? tsc - cv_base : 0;
}
// SPDX-SnippetEnd


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// R5: the four readers count from cv_base (tsc_since_base()).
static int64_t
__system_time_lfence()
{
	__builtin_ia32_lfence();
	__uint128_t time = static_cast<__uint128_t>(tsc_since_base(__rdtsc()))
		* cv_factor;
	return time >> 64;
}


static int64_t
__system_time_rdtscp()
{
	uint32_t aux;
	__uint128_t time
		= static_cast<__uint128_t>(tsc_since_base(__rdtscp(&aux))) * cv_factor;
	return time >> 64;
}


static int64_t
__system_time_nsecs_lfence()
{
	__builtin_ia32_lfence();
	__uint128_t t = static_cast<__uint128_t>(tsc_since_base(__rdtsc()))
		* cv_factor_nsec;
	return t >> 32;
}


static int64_t
__system_time_nsecs_rdtscp()
{
	uint32_t aux;
	__uint128_t t = static_cast<__uint128_t>(tsc_since_base(__rdtscp(&aux)))
		* cv_factor_nsec;
	return t >> 32;
}


// SPDX-SnippetEnd


static int64_t (*sSystemTime)(void) = __system_time_lfence;
static int64_t (*sSystemTimeNsecs)(void) = __system_time_nsecs_lfence;


// from kernel/arch/x86/arch_cpu.h
#define IA32_FEATURE_AMD_EXT_RDTSCP		(1 << 27) // rdtscp instruction


extern "C" void
__x86_setup_system_time(uint64_t cv, uint64_t cv_nsec)
{
	cv_factor = cv;
	cv_factor_nsec = cv_nsec;

	cpuid_info cpuInfo;
	get_cpuid(&cpuInfo, 0x80000000, 0);
	if (cpuInfo.eax_0.max_eax >= 0x80000001) {
		get_cpuid(&cpuInfo, 0x80000001, 0);
		if ((cpuInfo.regs.edx & IA32_FEATURE_AMD_EXT_RDTSCP)!= 0) {
			sSystemTime = __system_time_rdtscp;
			sSystemTimeNsecs = __system_time_nsecs_rdtscp;
		}
	}
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	R5: set once, before __x86_setup_system_time(): by the kernel from
	x86_init_tsc(), by libroot from the commpage (__arch_init_time()). Not
	exported from libroot.so.
*/
extern "C" __attribute__((visibility("hidden"))) void
__x86_setup_system_time_base(uint64_t base)
{
	cv_base = base;
}
// SPDX-SnippetEnd


extern "C" int64_t
system_time()
{
	return sSystemTime();
}



extern "C" int64_t
system_time_nsecs()
{
	return sSystemTimeNsecs();
}
