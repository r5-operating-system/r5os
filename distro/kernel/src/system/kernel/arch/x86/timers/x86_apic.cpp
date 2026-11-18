/*
 * Copyright 2025-2026, Haiku, Inc. All rights reserved.
 * Copyright 2008, Dustin Howett, dustin.howett@gmail.com. All rights reserved.
 * Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include <timer.h>
#include <arch/x86/timer.h>

#include <interrupts.h>
#include <arch/x86/apic.h>
#include <arch/x86/arch_system_info.h>

#include <arch/cpu.h>
#include <cpu.h>

#include "apic_timer.h"


//#define TRACE_APIC
#ifdef TRACE_APIC
#	define TRACE(x...) dprintf("apic: " x)
#else
#	define TRACE(x...) ;
#endif


static uint32 sApicTicsPerSec = 0;
static uint32 sApicTimerConfigBase = 0;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static bool sUseTscDeadline = false;

#ifndef IA32_FEATURE_EXT_TSC_DEADLINE
#define IA32_FEATURE_EXT_TSC_DEADLINE (1 << 24)
#endif
#ifndef IA32_MSR_TSC_DEADLINE
#define IA32_MSR_TSC_DEADLINE 0x000006e0
#endif
#ifndef APIC_LVT_TIMER_TSC_DEADLINE
#define APIC_LVT_TIMER_TSC_DEADLINE (2 << 17)
#endif

static inline uint64
read_tsc()
{
	uint32 lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64)hi << 32) | lo;
}


static bool
tsc_deadline_has_errata()
{
	if (x86_check_feature(IA32_FEATURE_EXT_HYPERVISOR, FEATURE_EXT))
		return false;

	cpu_ent* cpu = get_cpu_struct();
	if (cpu->arch.vendor != VENDOR_INTEL || cpu->arch.family != 6)
		return false;

	uint32 model = (cpu->arch.extended_model << 4) | cpu->arch.model;
	switch (model) {
		case 0x3A: case 0x3E: // Ivy Bridge
		case 0x3C: case 0x3F: case 0x45: case 0x46: // Haswell
		case 0x3D: case 0x47: case 0x4F: case 0x56: // Broadwell
		case 0x4E: case 0x55: case 0x5E: // Skylake
			return true;
		default:
			return false;
	}
}
// SPDX-SnippetEnd


static int
apic_timer_get_priority()
{
	return 3;
}


static int32
apic_timer_interrupt(void *data)
{
	return timer_interrupt();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static status_t
apic_timer_set_hardware_timer(bigtime_t relativeTimeout)
{
	if (relativeTimeout < 1)
		relativeTimeout = 1;

	if (sUseTscDeadline) {
		uint64 ticks = ((__uint128_t)relativeTimeout * gCPUClockSpeed) / 1000000;
		if (ticks == 0)
			ticks = 1;
		uint64 deadline = read_tsc() + ticks;
		if (deadline == 0)
			deadline = 1;
		// Serialize RDTSC/stores before the non-serializing WRMSR to IA32_MSR_TSC_DEADLINE.
		asm volatile("mfence; lfence" ::: "memory");
		x86_write_msr(IA32_MSR_TSC_DEADLINE, deadline);
		return B_OK;
	}

	uint64 ticks = ((relativeTimeout * sApicTicsPerSec) / 1000000);
	if (ticks == 0)
		ticks = 1;
	if (ticks > UINT32_MAX)
		ticks = UINT32_MAX;

	apic_set_lvt_initial_timer_count(ticks);
	return B_OK;
}


static status_t
apic_timer_clear_hardware_timer()
{
	if (sUseTscDeadline) {
		x86_write_msr(IA32_MSR_TSC_DEADLINE, 0);
		return B_OK;
	}

	apic_set_lvt_initial_timer_count(0);
	return B_OK;
}
// SPDX-SnippetEnd


//	#pragma mark - initialization


static uint32
calculate_apic_timer_conversion_factor()
{
	// setup the timer
	uint32 config = APIC_LVT_MASKED;
		// timer masked, vector 0
	apic_set_lvt_timer(config);

	apic_set_lvt_timer_divide_config(APIC_TIMER_DIVIDE_CONFIG_1);
		// divide clock by one

	apic_set_lvt_initial_timer_count(UINT32_MAX); // start the counter

	// Use CPUID as a fence (same as in TSC calibration).
	asm volatile ("cpuid" : : : "eax", "ebx", "ecx", "edx");
	int64 t1 = system_time_nsecs();
	uint32 startCount = apic_lvt_current_timer_count();

	spin(5000);

	asm volatile ("cpuid" : : : "eax", "ebx", "ecx", "edx");
	int64 t2 = system_time_nsecs();
	uint32 endCount = apic_lvt_current_timer_count();

	uint32 count = startCount - endCount;

	uint32 factor
		= (uint32)(((double(1) * 1000 * 1000 * 1000) / (t2 - t1)) * count);

	dprintf("APIC timer frequency: %d\n", factor);

	return factor;
}


static uint32
determine_apic_timer_conversion_factor()
{
	if (x86_check_feature(IA32_FEATURE_EXT_HYPERVISOR, FEATURE_EXT)) {
		cpuid_info info;
		get_current_cpuid(&info, 0x40000000, 0);
		const uint32 maxVMM = info.regs.eax;
		if (maxVMM >= 0x40000010) {
			get_current_cpuid(&info, 0x40000010, 0);

			uint32 freq = info.regs.ebx * 1000;
			dprintf("APIC timer frequency (from hypervisor CPUID leaf): %d\n", freq);
			return freq;
		}
	}

	return calculate_apic_timer_conversion_factor();
}


static status_t
apic_timer_init(struct kernel_args *args)
{
	if (!apic_available())
		return B_ERROR;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	sUseTscDeadline = x86_check_feature(IA32_FEATURE_EXT_TSC_DEADLINE, FEATURE_EXT)
		&& gCPUClockSpeed > 0 && !tsc_deadline_has_errata();
	if (!sUseTscDeadline)
		sApicTicsPerSec = determine_apic_timer_conversion_factor();

	reserve_io_interrupt_vectors(1, 0xfb - ARCH_INTERRUPT_BASE,
		INTERRUPT_TYPE_LOCAL_IRQ);
	install_io_interrupt_handler(0xfb - ARCH_INTERRUPT_BASE,
		&apic_timer_interrupt, NULL, B_NO_LOCK_VECTOR);

	sApicTimerConfigBase = 0xfb; // one-shot mode, vector 0xfb
	if (sUseTscDeadline)
		sApicTimerConfigBase |= APIC_LVT_TIMER_TSC_DEADLINE;

	dprintf("APIC timer: using %s mode\n",
		sUseTscDeadline ? "TSC-deadline" : "one-shot count");

	apic_timer_per_cpu_init(args, 0);
// SPDX-SnippetEnd
		// will be called on non-boot CPUs by apic_per_cpu_init()

	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
apic_timer_per_cpu_init(struct kernel_args *args, int32 cpu)
{
	apic_set_lvt_timer(sApicTimerConfigBase);
	if (sUseTscDeadline) {
		x86_write_msr(IA32_MSR_TSC_DEADLINE, 0);
	} else {
		apic_set_lvt_initial_timer_count(0);
		apic_set_lvt_timer_divide_config(APIC_TIMER_DIVIDE_CONFIG_1);
	}
	return B_OK;
}
// SPDX-SnippetEnd


struct timer_info gAPICTimer = {
	"APIC",
	&apic_timer_get_priority,
	&apic_timer_set_hardware_timer,
	&apic_timer_clear_hardware_timer,
	&apic_timer_init
};
