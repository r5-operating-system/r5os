/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef EFI_ARCH_H
#define EFI_ARCH_H


#include <SupportDefs.h>

#include <boot/r5_efi_runtime.h>


// Returned by efi_arch_call() when the firmware faulted. Not a UEFI status.
#define EFI_ARCH_FAULTED	0xffffffffffffffffULL


/*!	Builds the address space the firmware runs in from the runtime ranges.
	B_NOT_SUPPORTED on an architecture without a backend.
*/
status_t	efi_arch_init(const r5_efi_runtime_args* args);
void		efi_arch_uninit();

/*!	Calls "function" (an identity-mapped firmware address) with up to five
	arguments in the firmware calling convention. The caller holds the module
	lock. The arguments must point into kernel memory that stays mapped and
	wired. Returns the EFI status or EFI_ARCH_FAULTED.
*/
uint64		efi_arch_call(uint64 function, uint64 arg1, uint64 arg2,
				uint64 arg3, uint64 arg4, uint64 arg5);


#endif	// EFI_ARCH_H
