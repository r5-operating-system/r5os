/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * CPPC through the ACPI _CPC registers, for AMD processors without the CPPC
 * MSR (firmware that only offers shared-memory CPPC). docs/POWER.md,
 * section 4. Written from the ACPI specification (6.x, "Collaborative
 * Processor Performance Control" and "Platform Communications Channel").
 */
#ifndef AMD_CPPC_ACPI_H
#define AMD_CPPC_ACPI_H


#include <SupportDefs.h>


// B_OK when every CPU has a _CPC package whose registers R5 can reach
// (system memory or a generic PCC subspace). Thread context.
status_t	amd_cppc_acpi_init();
void		amd_cppc_acpi_uninit();

// Writes the profile's limits and energy preference for every CPU.
// Thread context, interrupts on. Logs one "R5 CPPC:" line per CPU.
status_t	amd_cppc_acpi_set_profile(int32 profile);


#endif	// AMD_CPPC_ACPI_H
