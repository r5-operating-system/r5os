/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The processor profile (docs/POWER.md, section 4): shared by the kernel
 * (_user_r5_set_power_profile(), _user_r5_get_power_info()), the power
 * service and libr5power.so. Only the power service sets the profile; any
 * program may read the information block.
 *
 * A value that shipped does not change. New fields go at the end of
 * r5_power_info; "size" tells the kernel how much the caller has room for.
 */
#ifndef _R5_POWER_PROFILE_H
#define _R5_POWER_PROFILE_H


#include <SupportDefs.h>


enum {
	R5_POWER_PROFILE_SAVER			= 0,
	R5_POWER_PROFILE_BALANCED		= 1,
	R5_POWER_PROFILE_PERFORMANCE	= 2,

	R5_POWER_PROFILE_COUNT			= 3
};

// How the processor's speed is set (r5_power_info.method).
enum {
	R5_POWER_METHOD_NONE			= 0,
		// no cpufreq module: the processor runs as the firmware starts it
	R5_POWER_METHOD_SCHEDULER		= 1,
		// a cpufreq module without profiles: only the scheduler mode
	R5_POWER_METHOD_AMD_CPPC_MSR	= 2,
	R5_POWER_METHOD_AMD_CPPC_ACPI	= 3,
		// shared-memory CPPC through the ACPI _CPC registers
	R5_POWER_METHOD_INTEL_HWP		= 4,
	R5_POWER_METHOD_INTEL_PSTATE	= 5
		// Intel without HWP: the P-state ratio, boost or not
};

// r5_power_info.flags
enum {
	R5_POWER_F_PROFILES				= 1u << 0,
		// the processor lets R5 change its speed: the profile menus work
	R5_POWER_F_S0IX					= 1u << 1,
		// the firmware offers suspend to idle (FADT low-power S0 + LPS0)
	R5_POWER_F_S3					= 1u << 2,
		// the firmware offers suspend to RAM (_S3_)
	R5_POWER_F_SLEEP_WORKS			= 1u << 3
		// the offered state passed its test on R5 (docs/POWER.md,
		// section 6); not set before step 4
};

typedef struct r5_power_info {
	uint32	size;
		// the caller sets it to sizeof(r5_power_info); the kernel fills in
		// at most that much and writes back what it filled
	uint32	profile;
	uint32	method;
	uint32	flags;
	uint32	reserved[4];
} r5_power_info;


#endif	/* _R5_POWER_PROFILE_H */
