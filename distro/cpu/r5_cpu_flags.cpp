/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Print the user-space CPU dispatch flags and the last-level cache size.
 * x86-64-v2 is the baseline and is not listed.
 */


#include <stdio.h>
#include <stdint.h>

#include <r5_x86_cpu.h>


int
main(void)
{
	uint32_t flags = r5_x86_cpu_flags();
	printf("r5_cpu flags=0x%08x llc=%u avx=%d avx2=%d fma=%d "
		"bmi1=%d bmi2=%d erms=%d fsrm=%d\n",
		flags, r5_x86_cpu_llc(),
		(flags & R5_CPU_AVX) != 0,
		(flags & R5_CPU_AVX2) != 0,
		(flags & R5_CPU_FMA) != 0,
		(flags & R5_CPU_BMI1) != 0,
		(flags & R5_CPU_BMI2) != 0,
		(flags & R5_CPU_ERMS) != 0,
		(flags & R5_CPU_FSRM) != 0);
	return 0;
}
