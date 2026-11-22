/*
 * Copyright 2014-2016 Haiku, Inc. All rights reserved.
 * Copyright 2013-2014, Fredrik Holmqvist, fredrik.holmqvist@gmail.com.
 * Copyright 2014, Henry Harrington, henry.harrington@gmail.com.
 * All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <algorithm>

#include <kernel.h>
#include <arch_kernel.h>
#include <boot/platform.h>
#include <boot/stage2.h>
#include <arch/x86/descriptors.h>

#include <efi/types.h>
#include <efi/boot-services.h>

#include "mmu.h"
#include "efi_platform.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "r5_efi_runtime.h"
// SPDX-SnippetEnd


//#define TRACE_MMU
#ifdef TRACE_MMU
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


//#define TRACE_MEMORY_MAP


extern uint64 gLongGDT;
extern uint64 gLongGDTR;
segment_descriptor gBootGDT[BOOT_GDT_SEGMENT_COUNT];


static void
long_gdt_init()
{
	STATIC_ASSERT(BOOT_GDT_SEGMENT_COUNT > KERNEL_CODE_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > KERNEL_DATA_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > USER_CODE_SEGMENT
		&& BOOT_GDT_SEGMENT_COUNT > USER_DATA_SEGMENT);

	clear_segment_descriptor(&gBootGDT[0]);

	// Set up code/data segments (TSS segments set up later in the kernel).
	set_segment_descriptor(&gBootGDT[KERNEL_CODE_SEGMENT], DT_CODE_EXECUTE_ONLY,
		DPL_KERNEL);
	set_segment_descriptor(&gBootGDT[KERNEL_DATA_SEGMENT], DT_DATA_WRITEABLE,
		DPL_KERNEL);
	set_segment_descriptor(&gBootGDT[USER_CODE_SEGMENT], DT_CODE_EXECUTE_ONLY,
		DPL_USER);
	set_segment_descriptor(&gBootGDT[USER_DATA_SEGMENT], DT_DATA_WRITEABLE,
		DPL_USER);

	// Used by long_enter_kernel().
	gLongGDT = (addr_t)gBootGDT + 0xFFFFFF0000000000;
	dprintf("GDT at 0x%lx\n", gLongGDT);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static bool
is_edk2_firmware()
{
	if (kSystemTable == NULL || kSystemTable->FirmwareVendor == NULL)
		return false;

	static const char16_t kEdk2Vendor[] = u"EDK II";
	const char16_t* vendor = kSystemTable->FirmwareVendor;
	for (size_t i = 0; i < sizeof(kEdk2Vendor) / sizeof(kEdk2Vendor[0]); i++) {
		if (vendor[i] != kEdk2Vendor[i])
			return false;
	}
	return true;
}


/*!	OVMF (edk2-stable202511+) places its fixed MEMFD at 0x00800000-0x00900000,
	leaving holes such as 0x0080c000-0x00814000 as EfiConventionalMemory.
	On a warm reset (RAM preserved), PlatformPei re-reads the IGVM parameter
	page at 0x00812000 (MemoryMap at +0x00, VpCount at +0xc0) and the IGVM HOB
	at 0x00813000 without clearing them first. If the OS allocated 0x00812000
	and left a value in 1..0xffff at 0x008120c0, PlatformPei skips fw_cfg and
	sets PcdCpuBootLogicalProcessorNumber to that value, hanging CpuMpPei in
	TimedWaitForApFinish (0xFFFFFFFF us). Exclude those conventional holes on
	OVMF so the kernel never writes to them.
*/
static void
reserve_ovmf_memfd_holes(size_t memoryMapSize,
	efi_memory_descriptor* memoryMap, size_t descriptorSize)
{
	if (!is_edk2_firmware())
		return;

	static const uint64_t kOvmfMemfdBase = 0x00800000;
	static const uint64_t kOvmfPeiFvBase = 0x00820000;

	bool hasLowNvs = false;
	bool hasPeiFvNvs = false;
	addr_t addr = (addr_t)memoryMap;
	for (size_t i = 0; i < memoryMapSize / descriptorSize; ++i) {
		efi_memory_descriptor* entry
			= (efi_memory_descriptor*)(addr + i * descriptorSize);
		if (entry->Type != EfiACPIMemoryNVS)
			continue;
		uint64_t base = entry->PhysicalStart;
		uint64_t end = base + entry->NumberOfPages * 4096;
		if (base <= kOvmfMemfdBase && end > kOvmfMemfdBase)
			hasLowNvs = true;
		if (base <= kOvmfPeiFvBase && end > kOvmfPeiFvBase)
			hasPeiFvNvs = true;
	}

	if (!hasLowNvs || !hasPeiFvNvs)
		return;

	uint64_t reservedBytes = 0;
	for (size_t i = 0; i < memoryMapSize / descriptorSize; ++i) {
		efi_memory_descriptor* entry
			= (efi_memory_descriptor*)(addr + i * descriptorSize);
		if (entry->Type != EfiConventionalMemory)
			continue;
		uint64_t base = entry->PhysicalStart;
		uint64_t end = base + entry->NumberOfPages * 4096;
		uint64_t start = max_c(base, kOvmfMemfdBase);
		uint64_t stop = min_c(end, kOvmfPeiFvBase);
		if (start < stop) {
			remove_physical_memory_range(start, stop - start);
			reservedBytes += stop - start;
		}
	}

	if (reservedBytes > 0) {
		dprintf("efi: reserved %" B_PRIu64 " KiB of OVMF MEMFD holes "
			"(0x00800000-0x00820000)\n", reservedBytes / 1024);
	}
}
// SPDX-SnippetEnd


// Called after EFI boot services exit.
// Currently assumes that the memory map is sane... Sorted and no overlapping
// regions.
void
arch_mmu_post_efi_setup(size_t memory_map_size,
	efi_memory_descriptor *memory_map, size_t descriptor_size,
	uint32_t descriptor_version)
{
	// Add physical memory to the kernel args and update virtual addresses for
	// EFI regions.
	addr_t addr = (addr_t)memory_map;
	gKernelArgs.num_physical_memory_ranges = 0;

	// First scan: Add all usable ranges
	for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
		efi_memory_descriptor *entry
			= (efi_memory_descriptor *)(addr + i * descriptor_size);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Every runtime range is identity mapped for SetVirtualAddressMap,
		// not only the code and data types: the variable services also use
		// runtime MMIO ranges (the flash), which kept the firmware's
		// VirtualStart.
		if ((entry->Attribute & EFI_MEMORY_RUNTIME) != 0)
			entry->VirtualStart = entry->PhysicalStart;
		// SPDX-SnippetEnd
		switch (entry->Type) {
		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory: {
			// Usable memory.
			// Ignore memory below 1MB and above 512GB.
			uint64_t base = entry->PhysicalStart;
			uint64_t end = entry->PhysicalStart + entry->NumberOfPages * 4096;
			uint64_t originalSize = end - base;
			if (base < 0x100000)
				base = 0x100000;
			if (end > (512ull * 1024 * 1024 * 1024))
				end = 512ull * 1024 * 1024 * 1024;

			gKernelArgs.ignored_physical_memory
				+= originalSize - (max_c(end, base) - base);

			if (base >= end)
				break;
			uint64_t size = end - base;

			insert_physical_memory_range(base, size);
			// LoaderData memory is bootloader allocated memory, possibly
			// containing the kernel or loaded drivers.
			if (entry->Type == EfiLoaderData)
				ASSERT_ALWAYS(insert_physical_allocated_range(base, size) == B_OK);
			break;
		}
		case EfiACPIReclaimMemory:
			// ACPI reclaim -- physical memory we could actually use later
			break;
		case EfiRuntimeServicesCode:
		case EfiRuntimeServicesData:
			entry->VirtualStart = entry->PhysicalStart;
			break;
		}
	}

	uint64_t initialPhysicalMemory = total_physical_memory();

	// Second scan: Remove everything reserved that may overlap
	for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
		efi_memory_descriptor *entry
			= (efi_memory_descriptor *)(addr + i * descriptor_size);
		switch (entry->Type) {
		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory:
			break;
		default:
			uint64_t base = entry->PhysicalStart;
			uint64_t end = entry->PhysicalStart + entry->NumberOfPages * 4096;
			remove_physical_memory_range(base, end - base);
		}
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	reserve_ovmf_memfd_holes(memory_map_size, memory_map, descriptor_size);
	// SPDX-SnippetEnd

	gKernelArgs.ignored_physical_memory
		+= initialPhysicalMemory - total_physical_memory();

	// Sort the address ranges.
	sort_address_ranges(gKernelArgs.physical_memory_range,
		gKernelArgs.num_physical_memory_ranges);
	sort_address_ranges(gKernelArgs.physical_allocated_range,
		gKernelArgs.num_physical_allocated_ranges);
	sort_address_ranges(gKernelArgs.virtual_allocated_range,
		gKernelArgs.num_virtual_allocated_ranges);

	// Switch EFI to virtual mode, using the kernel pmap.
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	efi_status setVirtualStatus =
	// SPDX-SnippetEnd
	kRuntimeServices->SetVirtualAddressMap(memory_map_size, descriptor_size,
		descriptor_version, memory_map);
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	r5_efi_runtime_set_virtual_status(setVirtualStatus);
	// SPDX-SnippetEnd

#ifdef TRACE_MEMORY_MAP
	dprintf("phys memory ranges:\n");
	for (uint32_t i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
		uint64 start = gKernelArgs.physical_memory_range[i].start;
		uint64 size = gKernelArgs.physical_memory_range[i].size;
		dprintf("    0x%08" B_PRIx64 "-0x%08" B_PRIx64 ", length 0x%08" B_PRIx64 "\n",
			start, start + size, size);
	}

	dprintf("allocated phys memory ranges:\n");
	for (uint32_t i = 0; i < gKernelArgs.num_physical_allocated_ranges; i++) {
		uint64 start = gKernelArgs.physical_allocated_range[i].start;
		uint64 size = gKernelArgs.physical_allocated_range[i].size;
		dprintf("    0x%08" B_PRIx64 "-0x%08" B_PRIx64 ", length 0x%08" B_PRIx64 "\n",
			start, start + size, size);
	}

	dprintf("allocated virt memory ranges:\n");
	for (uint32_t i = 0; i < gKernelArgs.num_virtual_allocated_ranges; i++) {
		uint64 start = gKernelArgs.virtual_allocated_range[i].start;
		uint64 size = gKernelArgs.virtual_allocated_range[i].size;
		dprintf("    0x%08" B_PRIx64 "-0x%08" B_PRIx64 ", length 0x%08" B_PRIx64 "\n",
			start, start + size, size);
	}
#endif
}



uint64_t
arch_mmu_generate_post_efi_page_tables(size_t memory_map_size,
	efi_memory_descriptor *memory_map, size_t descriptor_size,
	uint32_t descriptor_version)
{
	// Generate page tables, matching bios_ia32/long.cpp.
	uint64_t *pdpt;
	uint64_t *pageDir;
	uint64_t *pageTable;

	// Allocate the top level PML4.
	// (It needs to be below 4GB, as phys_pgdr is a uint32,
	// and the SMP trampoline loads it in 32-bit mode.)
	uint64_t *pml4 = NULL;
	if (platform_allocate_region_below((void**)&pml4, B_PAGE_SIZE, UINT32_MAX) != B_OK)
		panic("Failed to allocate PML4.");
	gKernelArgs.arch_args.phys_pgdir = (uint32_t)(addr_t)pml4;
	memset(pml4, 0, B_PAGE_SIZE);
	platform_bootloader_address_to_kernel_address(pml4,
		&gKernelArgs.arch_args.vir_pgdir);

	// Store the virtual memory usage information.
	gKernelArgs.virtual_allocated_range[0].start = KERNEL_LOAD_BASE;
	gKernelArgs.virtual_allocated_range[0].size
		= get_current_virtual_address() - KERNEL_LOAD_BASE;
	gKernelArgs.num_virtual_allocated_ranges = 1;
	gKernelArgs.arch_args.virtual_end = ROUNDUP(KERNEL_LOAD_BASE
		+ gKernelArgs.virtual_allocated_range[0].size, 0x200000);

	// Find the highest physical memory address. We map all physical memory
	// into the kernel address space, so we want to make sure we map everything
	// we have available.
	uint64 maxAddress = 0;
	for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
		efi_memory_descriptor *entry
			= (efi_memory_descriptor *)((addr_t)memory_map + i * descriptor_size);
		switch (entry->Type) {
		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory:
		case EfiRuntimeServicesCode:
		case EfiRuntimeServicesData:
		case EfiPersistentMemory:
		case EfiACPIReclaimMemory:
		case EfiACPIMemoryNVS:
			maxAddress = std::max(maxAddress,
					      entry->PhysicalStart + entry->NumberOfPages * 4096);
			break;
		default:
			break;
		}
	}

	// Want to map at least 4GB, there may be stuff other than usable RAM that
	// could be in the first 4GB of physical address space.
	maxAddress = std::max(maxAddress, (uint64)0x100000000ll);
	maxAddress = ROUNDUP(maxAddress, 0x40000000);

	// Currently only use 1 PDPT (512GB). This will need to change if someone
	// wants to use Haiku on a box with more than 512GB of RAM but that's
	// probably not going to happen any time soon.
	if (maxAddress / 0x40000000 > 512)
		panic("Can't currently support more than 512GB of RAM!");

	// Create page tables for the physical map area. Also map this PDPT
	// temporarily at the bottom of the address space so that we are identity
	// mapped.

	pdpt = (uint64*)mmu_allocate_page();
	memset(pdpt, 0, B_PAGE_SIZE);
	pml4[510] = (addr_t)pdpt | kTableMappingFlags;
	pml4[0] = (addr_t)pdpt | kTableMappingFlags;

	for (uint64 i = 0; i < maxAddress; i += 0x40000000) {
		pageDir = (uint64*)mmu_allocate_page();
		memset(pageDir, 0, B_PAGE_SIZE);
		pdpt[i / 0x40000000] = (addr_t)pageDir | kTableMappingFlags;

		for (uint64 j = 0; j < 0x40000000; j += 0x200000) {
			pageDir[j / 0x200000] = (i + j) | kLargePageMappingFlags;
		}
	}

	// Allocate tables for the kernel mappings.

	pdpt = (uint64*)mmu_allocate_page();
	memset(pdpt, 0, B_PAGE_SIZE);
	pml4[511] = (addr_t)pdpt | kTableMappingFlags;

	pageDir = (uint64*)mmu_allocate_page();
	memset(pageDir, 0, B_PAGE_SIZE);
	pdpt[510] = (addr_t)pageDir | kTableMappingFlags;

	// We can now allocate page tables and duplicate the mappings across from
	// the 32-bit address space to them.
	pageTable = NULL; // shush, compiler.
	for (uint32 i = 0; i < gKernelArgs.virtual_allocated_range[0].size
			/ B_PAGE_SIZE; i++) {
		if ((i % 512) == 0) {
			pageTable = (uint64*)mmu_allocate_page();
			memset(pageTable, 0, B_PAGE_SIZE);
			pageDir[i / 512] = (addr_t)pageTable | kTableMappingFlags;
		}

		// Get the physical address to map.
		void *phys;
		if (platform_kernel_address_to_bootloader_address(
			KERNEL_LOAD_BASE + (i * B_PAGE_SIZE), &phys) != B_OK) {
			continue;
		}

		pageTable[i % 512] = (addr_t)phys | kPageMappingFlags;
	}

	return (uint64)pml4;
}


void
arch_mmu_init()
{
	long_gdt_init();
}
