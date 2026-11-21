/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <arch/platform.h>
#include <apm.h>
#include <boot_item.h>
#include <boot/stage2.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <stdlib.h>

#include <boot/r5_efi_runtime.h>
#include <vm/vm.h>
// SPDX-SnippetEnd


static phys_addr_t sACPIRootPointer = 0;
static phys_addr_t sSMBIOSv2RootPointer = 0;
static phys_addr_t sSMBIOSv3RootPointer = 0;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	Copies the EFI runtime hand-over block of the EFI boot loader into the
	kernel heap and publishes it for the generic/efi module
	(docs/EFI-RUNTIME.md). A BIOS boot has no block.
*/
static void
r5_publish_efi_runtime(KMessage& bootVolume)
{
	phys_addr_t block = bootVolume.GetInt64(R5_EFI_RUNTIME_BOOT_KEY, 0);
	if (block == 0)
		return;

	r5_efi_runtime_args header;
	if (vm_memcpy_from_physical(&header, block, sizeof(header), false) != B_OK
		|| header.magic != R5_EFI_RUNTIME_MAGIC
		|| header.version != R5_EFI_RUNTIME_VERSION
		|| header.size < sizeof(header)
		|| header.size > R5_EFI_RUNTIME_BLOCK_SIZE) {
		dprintf("efi runtime: hand-over block at %#" B_PRIxPHYSADDR
			" is not valid\n", block);
		return;
	}

	void* copy = malloc(header.size);
	if (copy == NULL
		|| vm_memcpy_from_physical(copy, block, header.size, false) != B_OK) {
		free(copy);
		return;
	}

	// Kept for the life of the system: the module reads it when loaded.
	add_boot_item(R5_EFI_RUNTIME_BOOT_ITEM, copy, header.size);
}
// SPDX-SnippetEnd


status_t
arch_platform_init(struct kernel_args *args)
{
	return B_OK;
}


status_t
arch_platform_init_post_vm(struct kernel_args *args)
{
	// Now we can add boot items; pass on the ACPI root pointer
	sACPIRootPointer = args->arch_args.acpi_root.Get();
	add_boot_item("ACPI_ROOT_POINTER",
		&sACPIRootPointer, sizeof(sACPIRootPointer));

	KMessage bootVolume;
	bootVolume.SetTo(args->boot_volume, args->boot_volume_size);
	sSMBIOSv2RootPointer = bootVolume.GetInt64(BOOT_EFI_SMBIOS_V2_ROOT, 0);
	if (sSMBIOSv2RootPointer != 0) {
		add_boot_item("SMBIOSv2_ROOT_POINTER",
			&sSMBIOSv2RootPointer, sizeof(sSMBIOSv2RootPointer));
	}
	sSMBIOSv3RootPointer = bootVolume.GetInt64(BOOT_EFI_SMBIOS_V3_ROOT, 0);
	if (sSMBIOSv3RootPointer != 0) {
		add_boot_item("SMBIOSv3_ROOT_POINTER",
			&sSMBIOSv3RootPointer, sizeof(sSMBIOSv3RootPointer));
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	r5_publish_efi_runtime(bootVolume);
	// SPDX-SnippetEnd

	return B_OK;
}


status_t
arch_platform_init_post_thread(struct kernel_args *args)
{
	// APM is not supported on x86_64.
#ifndef __x86_64__
	apm_init(args);
#endif
	return B_OK;
}

