/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Architectures without an EFI runtime backend yet (arm64, riscv64).
	The module builds and does not load. What arm64 needs is in
	docs/EFI-RUNTIME.md.
*/


#include "efi_arch.h"


status_t
efi_arch_init(const r5_efi_runtime_args* args)
{
	return B_NOT_SUPPORTED;
}


void
efi_arch_uninit()
{
}


uint64
efi_arch_call(uint64 function, uint64 arg1, uint64 arg2, uint64 arg3,
	uint64 arg4, uint64 arg5)
{
	return EFI_ARCH_FAULTED;
}
