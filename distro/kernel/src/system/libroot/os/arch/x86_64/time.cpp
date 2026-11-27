/*
 * Copyright 2004, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */


#include <libroot_private.h>
#include <real_time_data.h>
#include <arch_cpu.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
extern "C" void __x86_setup_system_time_base(uint64_t base);
// SPDX-SnippetEnd


void
__arch_init_time(real_time_data* data, bool setDefaults)
{
	uint32 conversionFactor;
	uint64 conversionFactorNsecs;

	if (setDefaults) {
		data->arch_data.system_time_offset = 0;
		data->arch_data.system_time_conversion_factor = 100000;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		data->arch_data.system_time_tsc_base = 0;
// SPDX-SnippetEnd
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// R5: the TSC value the kernel's system_time() counts from, so both
	// count from the same moment (system_time.cpp).
	__x86_setup_system_time_base(data->arch_data.system_time_tsc_base);
// SPDX-SnippetEnd

	// TODO: this should only store a pointer to that value
	// When resolving this TODO, also resolve the one in the Jamfile.

	conversionFactor = data->arch_data.system_time_conversion_factor;
	conversionFactorNsecs = (uint64)conversionFactor * 1000;

	// The x86_64 system_time() implementation uses 64-bit multiplication and
	// therefore shifting is not necessary for low frequencies (it's also not
	// too likely that there'll be any x86_64 CPUs clocked under 1GHz).
	__x86_setup_system_time((uint64)conversionFactor << 32,
		conversionFactorNsecs);
}


bigtime_t
__arch_get_system_time_offset(struct real_time_data *data)
{
	//we don't use atomic_get64 because memory is read-only, maybe find another way to lock
	return data->arch_data.system_time_offset;
}

