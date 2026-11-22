/*
 * Copyright 2020-2022, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, <pdziepak@quarnos.org>
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */


#include <cpufreq.h>
#include <KernelExport.h>

#include <arch_cpu.h>
#include <cpu.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <kscheduler.h>
#include <r5_power_profile.h>

#include "amd_cppc_acpi.h"
// SPDX-SnippetEnd
#include <smp.h>
#include <util/AutoLock.h>


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Version 2 of the cpufreq interface: the profile (docs/POWER.md, section 4)
#define AMD_PSTATES_MODULE_NAME \
	CPUFREQ_MODULES_PREFIX "/amd_pstates" CPUFREQ_MODULE_V2_SUFFIX


static int32 sProfile = R5_POWER_PROFILE_PERFORMANCE;
static int32 sSavedProfile = R5_POWER_PROFILE_PERFORMANCE;
static bool sUseACPI = false;
	// no CPPC MSR: the ACPI _CPC registers (amd_cppc_acpi.cpp)

// The energy preference per profile: the values the AMD documentation
// names (arch_cpu.h).
static const uint32 kProfileEPP[R5_POWER_PROFILE_COUNT] = {
	AMD_CPPC_EPP_BALANCE_POWERSAVE,
	AMD_CPPC_EPP_BALANCE_PERFORMANCE,
	AMD_CPPC_EPP_PERFORMANCE
};


static int32
profile_for_mode(scheduler_mode mode)
{
	// what the scheduler modes always meant here: power saving keeps the
	// processor below its boost, low latency allows it with EPP performance
	return mode == SCHEDULER_MODE_POWER_SAVING
		? R5_POWER_PROFILE_SAVER : R5_POWER_PROFILE_PERFORMANCE;
}
// SPDX-SnippetEnd


static void set_normal_pstate(void* /* dummy */, int cpu);


static void
pstates_set_scheduler_mode(scheduler_mode mode)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	atomic_set(&sProfile, profile_for_mode(mode));
	if (sUseACPI) {
		// The registers may sit behind a PCC mailbox that waits for the
		// firmware. This hook is only called from a thread
		// (_user_set_scheduler_mode()), never from the scheduler itself.
		amd_cppc_acpi_set_profile(atomic_get(&sProfile));
		return;
	}
// SPDX-SnippetEnd
	call_all_cpus(set_normal_pstate, NULL);
}


static status_t
pstates_increase_performance(int delta)
{
	return B_NOT_SUPPORTED;
}


static status_t
pstates_decrease_performance(int delta)
{
	return B_NOT_SUPPORTED;
}


static bool
is_cpu_model_supported(cpu_ent* cpu)
{
	if (cpu->arch.vendor != VENDOR_AMD)
		return false;

	return true;
}


static void
set_normal_pstate(void* /* dummy */, int cpu)
{
	x86_write_msr(MSR_AMD_CPPC_ENABLE, 1);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// This runs on every CPU at the same time (call_all_cpus_sync), and the
	// cores do not all report the same values: keep them local, so no CPU
	// takes another one's highest or guaranteed level.
	uint64 cap1 = x86_read_msr(MSR_AMD_CPPC_CAP1);
	uint32 efficient = AMD_CPPC_LOWNONLIN_PERF(cap1);
	uint32 guaranteed = AMD_CPPC_NOMINAL_PERF(cap1);
	uint32 highest = AMD_CPPC_HIGHEST_PERF(cap1);

	// docs/POWER.md, section 4: Power saver stays at the nominal level and
	// leans toward energy; Balanced and Performance allow the boost.
	int32 profile = atomic_get(&sProfile);
	if (profile < 0 || profile >= R5_POWER_PROFILE_COUNT)
		profile = R5_POWER_PROFILE_PERFORMANCE;

	uint64 request = AMD_CPPC_MIN_PERF(efficient);
	request |= AMD_CPPC_MAX_PERF(
		profile == R5_POWER_PROFILE_SAVER ? guaranteed : highest);
	request |= AMD_CPPC_EPP_PERF(kProfileEPP[profile]);
	x86_write_msr(MSR_AMD_CPPC_REQ, request & 0xffffffff);

	// The request holds max in bits 0-7, min in 8-15 and EPP in 24-31.
	// (Until 22 Nov 2026 this line printed min and max the wrong way round.)
	uint64 actualReq = x86_read_msr(MSR_AMD_CPPC_REQ);
	dprintf("R5 CPPC: cpu %d profile %" B_PRId32 " cap1 0x%08" B_PRIx64
		" request 0x%08" B_PRIx64 " epp %" B_PRIu64 " min %" B_PRIu64
		" max %" B_PRIu64 "\n",
		cpu, profile, cap1 & 0xffffffff, actualReq & 0xffffffff,
		(actualReq >> 24) & 0xff, (actualReq >> 8) & 0xff, actualReq & 0xff);
// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static status_t
pstates_set_profile(int32 profile)
{
	if (profile < 0 || profile >= R5_POWER_PROFILE_COUNT)
		return B_BAD_VALUE;

	atomic_set(&sProfile, profile);
	if (sUseACPI)
		return amd_cppc_acpi_set_profile(profile);

	call_all_cpus_sync(set_normal_pstate, NULL);
	return B_OK;
}


static uint32
pstates_method()
{
	return sUseACPI
		? R5_POWER_METHOD_AMD_CPPC_ACPI : R5_POWER_METHOD_AMD_CPPC_MSR;
}
// SPDX-SnippetEnd


static status_t
init_pstates()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	int32 cpuCount = smp_get_num_cpus();
	for (int32 i = 0; i < cpuCount; i++) {
		if (!is_cpu_model_supported(&gCPU[i]))
			return B_ERROR;
	}

	sSavedProfile = profile_for_mode(
		static_cast<scheduler_mode>(_user_get_scheduler_mode()));
	atomic_set(&sProfile, sSavedProfile);

	if (!x86_check_feature(IA32_FEATURE_CPPC, FEATURE_EXT_8_EBX)) {
		// No CPPC MSR: firmware that offers CPPC only through the ACPI
		// _CPC registers (shared memory). Without those, no module.
		if (amd_cppc_acpi_init() != B_OK)
			return B_ERROR;
		sUseACPI = true;
		amd_cppc_acpi_set_profile(sSavedProfile);
		return B_OK;
	}
// SPDX-SnippetEnd

	dprintf("using AMD P-States (capabilities: 0x%08" B_PRIx64 ")\n",
		x86_read_msr(MSR_AMD_CPPC_CAP1));

	call_all_cpus_sync(set_normal_pstate, NULL);
	return B_OK;
}


static status_t
uninit_pstates()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	atomic_set(&sProfile, sSavedProfile);
	if (sUseACPI) {
		amd_cppc_acpi_set_profile(sSavedProfile);
		amd_cppc_acpi_uninit();
		sUseACPI = false;
		return B_OK;
	}
// SPDX-SnippetEnd
	call_all_cpus_sync(set_normal_pstate, NULL);

	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return init_pstates();

		case B_MODULE_UNINIT:
			uninit_pstates();
			return B_OK;
	}

	return B_ERROR;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static cpufreq_module_info_v2 sAMDPStates = {
	{
		{
			AMD_PSTATES_MODULE_NAME,
			0,
			std_ops,
		},

		1.0f,

		pstates_set_scheduler_mode,

		pstates_increase_performance,
		pstates_decrease_performance,
	},

	pstates_set_profile,
	pstates_method
};
// SPDX-SnippetEnd


module_info* modules[] = {
	(module_info*)&sAMDPStates,
	NULL
};
