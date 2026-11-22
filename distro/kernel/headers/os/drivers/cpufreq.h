/*
 * Copyright 2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */
#ifndef _CPUFREQ_H
#define _CPUFREQ_H


#include <module.h>
#include <scheduler.h>


#define CPUFREQ_MODULES_PREFIX	"power/cpufreq"


const int kCPUPerformanceScaleMax = 1000;

typedef struct cpufreq_module_info {
	module_info		info;

	float			rank;

	void			(*cpufreq_set_scheduler_mode)(enum scheduler_mode mode);

	status_t		(*cpufreq_increase_performance)(int delta);
	status_t		(*cpufreq_decrease_performance)(int delta);
} cpufreq_module_info;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT

// The second version of the interface (docs/POWER.md, section 4). A module
// that offers it publishes a name ending in CPUFREQ_MODULE_V2_SUFFIX, and its
// module_info is a cpufreq_module_info_v2. The first part is the
// cpufreq_module_info above, unchanged, so the kernel uses a version 1 module
// as before. A module that only has version 1 keeps following the scheduler
// mode.
#define CPUFREQ_MODULE_V2_SUFFIX	"/v2"

typedef struct cpufreq_module_info_v2 {
	cpufreq_module_info	v1;

	// Applies a profile (R5_POWER_PROFILE_* in <r5_power_profile.h>) on
	// every CPU before it returns. Called from a thread, with interrupts on.
	status_t		(*cpufreq_set_profile)(int32 profile);

	// How this module sets the speed (R5_POWER_METHOD_*).
	uint32			(*cpufreq_method)(void);
} cpufreq_module_info_v2;
// SPDX-SnippetEnd


#endif	// _CPUFREQ_H
