/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_EFI_RUNTIME_LOADER_H
#define R5_EFI_RUNTIME_LOADER_H


#include <SupportDefs.h>

#include <efi/boot-services.h>


// Before ExitBootServices: allocate the hand-over block, copy the memory
// attributes table and the runtime properties, and store the block in the
// boot volume message. Must run before the loader clones that message.
void r5_efi_runtime_init();

// What SetVirtualAddressMap returned (set in arch_mmu.cpp).
void r5_efi_runtime_set_virtual_status(efi_status status);

// After SetVirtualAddressMap: copy the runtime descriptors of the final
// memory map into the block and mark it complete.
void r5_efi_runtime_handoff(const efi_memory_descriptor* memoryMap,
	size_t memoryMapSize, size_t descriptorSize, uint32 descriptorVersion);


#endif	// R5_EFI_RUNTIME_LOADER_H
