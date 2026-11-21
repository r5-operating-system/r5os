/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef KERNEL_BOOT_R5_EFI_RUNTIME_H
#define KERNEL_BOOT_R5_EFI_RUNTIME_H


/*!	Hand-over of the EFI runtime services from the EFI boot loader to the
	kernel (docs/EFI-RUNTIME.md).

	The loader allocates one block in kernel_args memory before it exits the
	boot services and stores its physical address in the boot volume message
	under R5_EFI_RUNTIME_BOOT_KEY. After SetVirtualAddressMap it fills in the
	runtime descriptors and sets R5_EFI_RUNTIME_COMPLETE. The kernel copies the
	block into its heap and publishes it as the boot item
	R5_EFI_RUNTIME_BOOT_ITEM, which the generic/efi module reads. A BIOS boot
	has no key, so the module is absent.

	Layout: this header, then "attribute_count" descriptors of the EFI memory
	attributes table (each "attribute_descriptor_size" bytes) at
	"attribute_offset", then "descriptor_count" runtime descriptors of the
	memory map (each "descriptor_size" bytes) at "descriptor_offset".
*/


#include <SupportDefs.h>


#define R5_EFI_RUNTIME_BOOT_KEY		"_boot_efi r5 runtime"
#define R5_EFI_RUNTIME_BOOT_ITEM	"R5_EFI_RUNTIME"
#define R5_EFI_RUNTIME_MAGIC		0x72356566	// 'r5ef'
#define R5_EFI_RUNTIME_VERSION		1
#define R5_EFI_RUNTIME_BLOCK_SIZE	(24 * 1024)
	// stays below half a kernel_args chunk

enum {
	R5_EFI_RUNTIME_COMPLETE			= 0x01,
		// the runtime descriptors are filled in and none was left out
	R5_EFI_RUNTIME_IDENTITY			= 0x02,
		// SetVirtualAddressMap succeeded with VirtualStart == PhysicalStart
	R5_EFI_RUNTIME_ATTRIBUTES		= 0x04,
		// the EFI memory attributes table was found and copied
	R5_EFI_RUNTIME_PROPERTIES		= 0x08,
		// the EFI_RT_PROPERTIES_TABLE was found; "supported" is valid
};

// EFI_RT_PROPERTIES_TABLE bits (UEFI 2.8+) used by R5.
enum {
	R5_EFI_RT_GET_TIME				= 0x0001,
	R5_EFI_RT_GET_VARIABLE			= 0x0010,
	R5_EFI_RT_GET_NEXT_VARIABLE_NAME = 0x0020,
	R5_EFI_RT_SET_VARIABLE			= 0x0040,
	R5_EFI_RT_QUERY_VARIABLE_INFO	= 0x2000,
};


typedef struct r5_efi_runtime_args {
	uint32	magic;
	uint32	version;
	uint32	flags;
	uint32	size;
		// bytes used, header included
	uint64	runtime_services;
		// address of the efi_runtime_services table (identity mapped)
	uint32	supported;
		// R5_EFI_RT_* mask; all bits set without a properties table
	uint32	descriptor_size;
	uint32	descriptor_version;
	uint32	descriptor_count;
	uint32	descriptor_offset;
	uint32	attribute_descriptor_size;
	uint32	attribute_count;
	uint32	attribute_offset;
	uint64	set_virtual_status;
		// what SetVirtualAddressMap returned
} r5_efi_runtime_args;


#endif	// KERNEL_BOOT_R5_EFI_RUNTIME_H
