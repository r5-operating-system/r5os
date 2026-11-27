/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_REAL_TIME_DATA_H
#define _KERNEL_ARCH_REAL_TIME_DATA_H

#include <StorageDefs.h>
#include <SupportDefs.h>


struct arch_real_time_data {
	bigtime_t	system_time_offset;
	uint32		system_time_conversion_factor;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	uint32		_r5_padding;
	// R5: the TSC value that system_time() counts from. 0 when the kernel
	// reset the TSCs at boot; the firmware's value when it kept them because
	// they were already in sync (arch_cpu.cpp, x86_tsc_warp_check). Set once
	// at boot, never changed.
	uint64		system_time_tsc_base;
// SPDX-SnippetEnd
};

#endif	/* _KERNEL_ARCH_REAL_TIME_DATA_H */
