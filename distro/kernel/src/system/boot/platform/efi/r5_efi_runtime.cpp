/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Hands the EFI runtime services to the kernel (docs/EFI-RUNTIME.md).
	The block format is in <boot/r5_efi_runtime.h>.
*/


#include "r5_efi_runtime.h"

#include <string.h>

#include <boot/r5_efi_runtime.h>
#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/stdio.h>

#include "efi_platform.h"



struct efi_memory_attributes_table {
	uint32	version;
	uint32	number_of_entries;
	uint32	descriptor_size;
	uint32	flags;
	// descriptors follow
};

struct efi_rt_properties_table {
	uint16	version;
	uint16	length;
	uint32	runtime_services_supported;
};


static const efi_guid kMemoryAttributesTableGuid = { 0xdcfa911d, 0x26eb,
	0x469f, { 0xa2, 0x20, 0x38, 0xb7, 0xdc, 0x46, 0x12, 0x20 } };
static const efi_guid kRtPropertiesTableGuid = { 0xeb66918a, 0x7eef, 0x402a,
	{ 0x84, 0x2e, 0x93, 0x1d, 0x21, 0xc3, 0x8a, 0xe9 } };


static r5_efi_runtime_args* sArgs = NULL;
static efi_status sSetVirtualStatus = EFI_UNSUPPORTED;
static bool sSetVirtualCalled = false;


static void
copy_attributes_table(const efi_memory_attributes_table* table)
{
	if (table->descriptor_size < sizeof(efi_memory_descriptor)
		|| table->number_of_entries == 0) {
		dprintf("efi runtime: memory attributes table is empty\n");
		return;
	}

	size_t bytes = (size_t)table->number_of_entries * table->descriptor_size;
	if (sArgs->size + bytes > R5_EFI_RUNTIME_BLOCK_SIZE * 2 / 3) {
		// Leave a third of the block for the runtime descriptors.
		dprintf("efi runtime: memory attributes table too large (%" B_PRIu32
			" entries), ignored\n", table->number_of_entries);
		return;
	}

	sArgs->attribute_offset = sArgs->size;
	sArgs->attribute_descriptor_size = table->descriptor_size;
	sArgs->attribute_count = table->number_of_entries;
	memcpy((uint8*)sArgs + sArgs->size, table + 1, bytes);
	sArgs->size += bytes;
	sArgs->flags |= R5_EFI_RUNTIME_ATTRIBUTES;
}


void
r5_efi_runtime_init()
{
	if (kRuntimeServices == NULL
		|| kRuntimeServices->Hdr.Signature != EFI_RUNTIME_SERVICES_SIGNATURE) {
		dprintf("efi runtime: no runtime services table\n");
		return;
	}

	sArgs = (r5_efi_runtime_args*)kernel_args_malloc(
		R5_EFI_RUNTIME_BLOCK_SIZE, 8);
	if (sArgs == NULL) {
		dprintf("efi runtime: no memory for the hand-over block\n");
		return;
	}

	memset(sArgs, 0, sizeof(r5_efi_runtime_args));
	sArgs->magic = R5_EFI_RUNTIME_MAGIC;
	sArgs->version = R5_EFI_RUNTIME_VERSION;
	sArgs->size = sizeof(r5_efi_runtime_args);
	sArgs->runtime_services = (addr_t)kRuntimeServices;
	sArgs->supported = 0xffffffff;

	const efi_configuration_table* table = kSystemTable->ConfigurationTable;
	for (size_t i = 0; i < kSystemTable->NumberOfTableEntries; i++) {
		if (table[i].VendorGuid.equals(kMemoryAttributesTableGuid)) {
			copy_attributes_table(
				(const efi_memory_attributes_table*)table[i].VendorTable);
		} else if (table[i].VendorGuid.equals(kRtPropertiesTableGuid)) {
			const efi_rt_properties_table* properties
				= (const efi_rt_properties_table*)table[i].VendorTable;
			if (properties->length >= sizeof(efi_rt_properties_table)) {
				sArgs->supported = properties->runtime_services_supported;
				sArgs->flags |= R5_EFI_RUNTIME_PROPERTIES;
			}
		}
	}

	// The loader stores the address it sees; it runs identity mapped, so
	// that is the physical address the kernel reads the block from.
	gBootParams.SetInt64(R5_EFI_RUNTIME_BOOT_KEY, (addr_t)sArgs);
	dprintf("efi runtime: hand-over block at %p, revision %#" B_PRIx32
		", attributes table %s, properties %#" B_PRIx32 "\n", sArgs,
		kRuntimeServices->Hdr.Revision,
		(sArgs->flags & R5_EFI_RUNTIME_ATTRIBUTES) != 0 ? "yes" : "no",
		sArgs->supported);
}


void
r5_efi_runtime_set_virtual_status(efi_status status)
{
	sSetVirtualStatus = status;
	sSetVirtualCalled = true;
}


void
r5_efi_runtime_handoff(const efi_memory_descriptor* memoryMap,
	size_t memoryMapSize, size_t descriptorSize, uint32 descriptorVersion)
{
	// Runs after ExitBootServices: no allocation, no firmware console.
	if (sArgs == NULL || descriptorSize < sizeof(efi_memory_descriptor))
		return;

	sArgs->set_virtual_status = sSetVirtualStatus;
	sArgs->descriptor_size = descriptorSize;
	sArgs->descriptor_version = descriptorVersion;
	sArgs->descriptor_offset = sArgs->size;
	sArgs->descriptor_count = 0;

	bool complete = true;
	bool identity = true;
	for (size_t i = 0; i < memoryMapSize / descriptorSize; i++) {
		const efi_memory_descriptor* entry = (const efi_memory_descriptor*)
			((addr_t)memoryMap + i * descriptorSize);
		if ((entry->Attribute & EFI_MEMORY_RUNTIME) == 0)
			continue;

		if (sArgs->size + descriptorSize > R5_EFI_RUNTIME_BLOCK_SIZE) {
			complete = false;
			break;
		}
		if (entry->VirtualStart != entry->PhysicalStart)
			identity = false;

		memcpy((uint8*)sArgs + sArgs->size, entry, descriptorSize);
		sArgs->size += descriptorSize;
		sArgs->descriptor_count++;
	}

	if (complete && sArgs->descriptor_count > 0)
		sArgs->flags |= R5_EFI_RUNTIME_COMPLETE;
	if (identity && sSetVirtualCalled && sSetVirtualStatus == EFI_SUCCESS)
		sArgs->flags |= R5_EFI_RUNTIME_IDENTITY;

	dprintf("efi runtime: %" B_PRIu32 " runtime ranges, flags %#" B_PRIx32
		", SetVirtualAddressMap %#lx\n", sArgs->descriptor_count, sArgs->flags,
		(unsigned long)sSetVirtualStatus);
}
